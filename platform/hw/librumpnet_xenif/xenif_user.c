/*
 * Copyright (c) 2013 Antti Kantee.  All Rights Reserved.
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

#include <mini-os/os.h>

#include <bmk-core/errno.h>
#include <bmk-core/memalloc.h>
#include <bmk-core/string.h>
#include <bmk-core/sched.h>
#include <bmk-core/platform.h>
#include <bmk-core/printf.h>

#include <bmk-rumpuser/core_types.h>
#include <bmk-rumpuser/rumpuser.h>

#include <xen/network.h>

#include "if_virt.h"
#include "if_virt_user.h"

#ifndef NETDOM_FRONTEND
int rumpuser_network_receive(struct mbuf *m)
{
#ifdef NETDOM_DIRECT
	return 1;
#else
	return backend_receive(m);
#endif
}

void rumpuser_network_init(const char *name, struct ifnet *ifp)
{
#ifdef NETDOM_DIRECT
	return;
#else
	if (bmk_strncmp(name, VIF_NAME, sizeof(VIF_NAME) - 1) && !(name[0] == 'l' && name[1] == 'o')) {
		backend_init(ifp);
	}
#endif
}

int rumpuser_network_portbind(uint16_t *port, uint8_t protocol)
{
	return 0;
}

#else
/* Functions for the frontend */
int dynamic_mode = 0;

struct virtif_user {
	struct virtif_sc *viu_vifsc;
};

static struct ifnet *phys_ifp = NULL;
static struct ifnet *virt_ifp = NULL;
static int direct_access = 0;

int rumpuser_network_portbind(uint16_t *port, uint8_t protocol)
{
	return frontend_portbind(port, protocol);
}

int rumpuser_network_receive(struct mbuf *m)
{
	if (direct_access) {
		rump_virtif_pktdeliver_direct(virt_ifp, m);
		return -1;
	}

	return BMK_ENOENT;
}

void rumpuser_network_init(const char *name, struct ifnet *ifp)
{
	if (!bmk_strncmp(name, VIF_NAME, sizeof(VIF_NAME) - 1)) {
		virt_ifp = ifp;
		return;
	}

	if (!(name[0] == 'l' && name[1] == 'o')) {
		phys_ifp = ifp;
		dynamic_mode = 1;
		return;
	}
}

void rump_virtif_switch(void)
{
	direct_access^=1;
	ifconfigd_waker(ifconfigd_get_lid());

	bmk_printf("Switching to %s\n", direct_access ? "direct access" : "network server");
}

int
VIFHYPER_SEND(struct virtif_user *viu, struct mbuf *m)
{
	int nlocks;
	if (direct_access) {
		rump_virtif_pktforward_direct(phys_ifp, m);
		return -1;
	}

	rumpkern_unsched(&nlocks, NULL);
	frontend_send(m);
	rumpkern_sched(nlocks, NULL);

	return 0;
}
#endif

int VIFHYPER_CREATE(int devnum, struct virtif_sc *vif_sc, uint8_t *enaddr, struct virtif_user **viup)
{
#ifdef NETDOM_FRONTEND
	struct virtif_user *viu = NULL;
	int rv, nlocks;

	rumpkern_unsched(&nlocks, NULL);

	viu = bmk_memalloc(sizeof(*viu), 0, BMK_MEMWHO_RUMPKERN);
	if (viu == NULL) {
		rv = BMK_ENOMEM;
		goto out;
	}
	bmk_memset(viu, 0, sizeof(*viu));
	viu->viu_vifsc = vif_sc;

	/* frontend init */
	frontend_init(vif_sc);

	rv = 0;

 out:
	rumpkern_sched(nlocks, NULL);

	*viup = viu;

	return rv;
#else
	return 0;
#endif
}

void
VIFHYPER_DYING(struct virtif_user *viu)
{
#ifdef NETDOM_FRONTEND
	frontend_terminate();
#endif
}

void
VIFHYPER_DESTROY(struct virtif_user *viu)
{
#ifdef NETDOM_FRONTEND
	frontend_join();
	bmk_memfree(viu, BMK_MEMWHO_RUMPKERN);
#endif
}
