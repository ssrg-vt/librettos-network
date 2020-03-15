/*	$NetBSD: if_virt.c,v 1.36 2013/07/04 11:46:51 pooka Exp $	*/

/*
 * Copyright (c) 2008, 2013 Antti Kantee.  All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__KERNEL_RCSID(0, "$NetBSD: if_virt.c,v 1.36 2013/07/04 11:46:51 pooka Exp $");

#include <sys/param.h>
#include <sys/condvar.h>
#include <sys/fcntl.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/kthread.h>
#include <sys/mutex.h>
#include <sys/poll.h>
#include <sys/sockio.h>
#include <sys/socketvar.h>
#include <sys/cprng.h>

#include <net/bpf.h>
#include <net/if.h>
#include <net/if_ether.h>
#include <net/if_tap.h>

#include <netinet/in.h>
#include <netinet/in_var.h>

#include <rump/rump.h>

#include "rump_private.h"
#include "rump_net_private.h"

#include "if_virt.h"
#include "if_virt_user.h"

/*
 * Virtual interface.  Uses hypercalls to shovel packets back
 * and forth.  The exact method for shoveling depends on the
 * hypercall implementation.
 */

static int	virtif_init(struct ifnet *);
static int	virtif_ioctl(struct ifnet *, u_long, void *);
static void	virtif_start(struct ifnet *);
static void	virtif_stop(struct ifnet *, int);

struct virtif_sc {
	struct ethercom sc_ec;
	struct virtif_user *sc_viu;
};

static int  virtif_clone(struct if_clone *, int);
static int  virtif_unclone(struct ifnet *);

struct if_clone VIF_CLONER =
    IF_CLONE_INITIALIZER(VIF_NAME, virtif_clone, virtif_unclone);

static int
virtif_clone(struct if_clone *ifc, int num)
{
	struct virtif_sc *sc;
	struct virtif_user *viu;
	struct ifnet *ifp;
	uint8_t enaddr[ETHER_ADDR_LEN] = { 0x00, 0x1b, 0x21, 0x73, 0xea, 0x84 };
	char enaddrstr[3*ETHER_ADDR_LEN];
	int error = 0;

	if (num >= 0x100)
		return E2BIG;

//	enaddr[2] = cprng_fast32() & 0xff;
//	enaddr[5] = num;

	sc = kmem_zalloc(sizeof(*sc), KM_SLEEP);

	if ((error = VIFHYPER_CREATE(num, sc, enaddr, &viu)) != 0) {
		kmem_free(sc, sizeof(*sc));
		return error;
	}
	sc->sc_viu = viu;

	/* allow jumbo MTU */
	sc->sc_ec.ec_capabilities = ETHERCAP_JUMBO_MTU;
	sc->sc_ec.ec_capenable = sc->sc_ec.ec_capabilities;

	ifp = &sc->sc_ec.ec_if;
	snprintf(ifp->if_xname, sizeof(ifp->if_xname), "%s%d", VIF_NAME, num);
	ifp->if_softc = sc;
	ifp->if_flags = IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST;
//	ifp->if_csum_flags_tx = M_CSUM_IPv4 | M_CSUM_TCPv4 | M_CSUM_UDPv4 |
//				M_CSUM_TCPv6 | M_CSUM_UDPv6;
//	ifp->if_csum_flags_rx = M_CSUM_IPv4 | M_CSUM_TCPv4 | M_CSUM_UDPv4 |
//				M_CSUM_TCPv6 | M_CSUM_UDPv6;
	ifp->if_init = virtif_init;
	ifp->if_ioctl = virtif_ioctl;
	ifp->if_start = virtif_start;
	ifp->if_stop = virtif_stop;
	IFQ_SET_READY(&ifp->if_snd);

	if_attach(ifp);
	ether_ifattach(ifp, enaddr);

	/* override MTU size */
	ifp->if_mtu = ETHERMTU_JUMBO;

	ether_snprintf(enaddrstr, sizeof(enaddrstr), enaddr);
	aprint_normal_ifnet(ifp, "Ethernet address %s\n", enaddrstr);

	if (error) {
		virtif_unclone(ifp);
	}

	return error;
}

static int
virtif_unclone(struct ifnet *ifp)
{
	struct virtif_sc *sc = ifp->if_softc;

	VIFHYPER_DYING(sc->sc_viu);

	virtif_stop(ifp, 1);
	if_down(ifp);

	VIFHYPER_DESTROY(sc->sc_viu);

	kmem_free(sc, sizeof(*sc));

	ether_ifdetach(ifp);
	if_detach(ifp);

	return 0;
}

static int
virtif_init(struct ifnet *ifp)
{

	ifp->if_flags |= IFF_RUNNING;
	return 0;
}

static int
virtif_ioctl(struct ifnet *ifp, u_long cmd, void *data)
{
	int s, rv;

	s = splnet();
	rv = ether_ioctl(ifp, cmd, data);
	if (rv == ENETRESET)
		rv = 0;
	splx(s);

	return rv;
}

/*
 * Output packets in-context until outgoing queue is empty.
 * Assume that VIFHYPER_SEND() is fast enough to not make it
 * necessary to drop kernel_lock.
 */
static void
virtif_start(struct ifnet *ifp)
{
#ifndef NETDOM_FRONTEND
	return;
#else
	struct virtif_sc *sc = ifp->if_softc;
	struct mbuf *m;

	ifp->if_flags |= IFF_OACTIVE;

	for (;;) {
		IF_DEQUEUE(&ifp->if_snd, m);
		if (!m) {
			break;
		}

		bpf_mtap(ifp, m);

		if (VIFHYPER_SEND(sc->sc_viu, m) == 0)
			m_freem(m);
	}

	ifp->if_flags &= ~IFF_OACTIVE;
#endif
}

static void
virtif_stop(struct ifnet *ifp, int disable)
{
#ifndef NETDOM_FRONTEND
	return;
#else
	ifp->if_flags &= ~IFF_RUNNING;
#endif
}

/* frontend driver -> NetBSD network stack */
void
rump_virtif_pktdeliver(struct virtif_sc *sc, const void *data, size_t len)
{
	struct ifnet *ifp = &sc->sc_ec.ec_if;
	struct mbuf *m;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return; /* drop packet */

	m->m_len = m->m_pkthdr.len = 0;
	m_copyback(m, 0, len, data);
	if (len != m->m_pkthdr.len) {
		aprint_normal_ifnet(ifp, "m_copyback failed\n");
		m_freem(m);
		return;
	}

#if __NetBSD_Prereq__(7,99,31)
	m_set_rcvif(m, ifp);
#else
	m->m_pkthdr.rcvif = ifp;
#endif

	KERNEL_LOCK_UNLESS_IFP_MPSAFE(ifp);
	bpf_mtap(ifp, m);
	ether_input(ifp, m);
	KERNEL_UNLOCK_UNLESS_IFP_MPSAFE(ifp);
}

/* backend driver -> NIC */
void
rump_virtif_pktforward(struct ifnet *ifp, const void *data, size_t len)
{
	struct mbuf *m;
	int ret;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

	m = m_gethdr(M_NOWAIT, MT_DATA);
	if (m == NULL)
		return; /* drop packet */

	m->m_len = m->m_pkthdr.len = 0;
	m_copyback(m, 0, len, data);
	if (len != m->m_pkthdr.len) {
		aprint_normal_ifnet(ifp, "m_copyback failed\n");
		m_freem(m);
		return;
	}

#if __NetBSD_Prereq__(7,99,31)
	m_set_rcvif(m, ifp);
#else
	m->m_pkthdr.rcvif = ifp;
#endif

	/* clean in-bound checksum flags */
	m->m_pkthdr.csum_flags = 0;

	/* send mbuf here */
	ret = if_transmit_lock(ifp, m);
	if (ret != 0) {
		ifp->if_oerrors++;
		aprint_normal("if_transmit_lock returns %d\n", ret);
		return;
	}

	ifp->if_opackets++;
}

#ifdef NETDOM_FRONTEND
/* NIC -> NetBSD network stack */
void rump_virtif_pktdeliver_direct(struct ifnet *ifp, struct mbuf *m)
{
	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

#if __NetBSD_Prereq__(7,99,31)
	m_set_rcvif(m, ifp);
#else
	m->m_pkthdr.rcvif = ifp;
#endif
	KERNEL_LOCK_UNLESS_IFP_MPSAFE(ifp);
	bpf_mtap(ifp, m);
	ether_input(ifp, m);
	KERNEL_UNLOCK_UNLESS_IFP_MPSAFE(ifp);
}

/* NetBSD network stack -> NIC */
void rump_virtif_pktforward_direct(struct ifnet *ifp, struct mbuf *m)
{
	int ret;

	if ((ifp->if_flags & IFF_RUNNING) == 0)
		return;

#if __NetBSD_Prereq__(7,99,31)
	m_set_rcvif(m, ifp);
#else
	m->m_pkthdr.rcvif = ifp;
#endif

	/* clean in-bound checksum flags */
	m->m_pkthdr.csum_flags = 0;

	/* send mbuf here */
	ret = if_transmit_lock(ifp, m);
	if (ret != 0) {
		ifp->if_oerrors++;
		aprint_normal("if_transmit_lock returns %d\n", ret);
		return;
	}

	ifp->if_opackets++;
}
#endif
