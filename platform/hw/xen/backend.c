/*
 * Copyright (c) 2018 Mincheol Sung.  All Rights Reserved.
 * Copyright (c) 2018 Ruslan Nikolaev.  All Rights Reserved.
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
#include <mini-os/events.h>
#include <mini-os/gntmap.h>
#include <mini-os/hypervisor.h>
#include <mini-os/types.h>

#include <bmk-core/errno.h>
#include <bmk-core/memalloc.h>
#include <bmk-core/string.h>
#include <bmk-core/sched.h>
#include <bmk-core/platform.h>
#include <bmk-core/printf.h>

#include <bmk-rumpuser/core_types.h>
#include <bmk-rumpuser/rumpuser.h>

#include <rumprun-base/rumprun.h>

#include <xen/network.h>
#include <xen/network_ring.h>
#include <xen/network_hypercall.h>

#include <mini-os/semaphore.h>

#include "../librumpnet_xenif/if_virt_user.h"

#define TCP 6
#define UDP 17

static pool_entry_t ip_addrpool[RUMPRUN_SERVICE_IPS+1];

static backend_connect_t network_dom_info;
static frontend_connect_t app_dom_info[RUMPRUN_NUM_OF_APPS];
static uint32_t reconnect_welcome_port[RUMPRUN_NUM_OF_APPS];
static struct gntmap backend_map[RUMPRUN_NUM_OF_APPS];

static struct netdom_aring *tx_aring[RUMPRUN_NUM_OF_APPS];
static struct netdom_aring *rx_aring[RUMPRUN_NUM_OF_APPS];
static struct netdom_fring *tx_fring[RUMPRUN_NUM_OF_APPS] = \
				{ [0 ... RUMPRUN_NUM_OF_APPS-1] = NULL };
static struct netdom_fring *rx_fring[RUMPRUN_NUM_OF_APPS];
static void *tx_buf[RUMPRUN_NUM_OF_APPS];
static void *rx_buf[RUMPRUN_NUM_OF_APPS];

static struct ifnet *backend_ifp;
static _Atomic(unsigned int) frontend_dom = ATOMIC_VAR_INIT(0);
static _Atomic(unsigned int) reconnection = ATOMIC_VAR_INIT(0);

struct backend_thread {
	_Alignas(LF_CACHE_BYTES) struct bmk_thread * thread;
	_Atomic(unsigned int) state;
	unsigned int dom;
	_Alignas(LF_CACHE_BYTES) char pad[0];
};

static portmap_entry_t *tcp_portmap;
static portmap_entry_t *udp_portmap;

static struct backend_thread rx_threads[RUMPRUN_NUM_OF_APPS];

static struct bmk_thread *ip_register_thread;
static void init_portmap(void)
{
	tcp_portmap = (portmap_entry_t *)HYPERVISOR_netdom_map;
	udp_portmap = (portmap_entry_t *)HYPERVISOR_netdom_map + 65536U;
}

static inline void backend_interrupt_handler(unsigned int dom,
		struct pt_regs *regs, void *data)
{
	if (atomic_exchange(&rx_aring[dom]->readers, 1) == 0)
		bmk_sched_wake(rx_threads[dom].thread);
}

#define BACKEND_INTERRUPT(dom)						\
static void _backend_interrupt_handler_##dom(evtchn_port_t port,	\
		struct pt_regs *regs, void * data)			\
{									\
	return backend_interrupt_handler(dom, regs, data);		\
}

typedef void (*backend_interrupt_t) (unsigned int, struct pt_regs *, void *);

BACKEND_INTERRUPT(0)
BACKEND_INTERRUPT(1)
BACKEND_INTERRUPT(2)
BACKEND_INTERRUPT(3)
BACKEND_INTERRUPT(4)
BACKEND_INTERRUPT(5)
BACKEND_INTERRUPT(6)
BACKEND_INTERRUPT(7)
BACKEND_INTERRUPT(8)
BACKEND_INTERRUPT(9)
BACKEND_INTERRUPT(10)
BACKEND_INTERRUPT(11)
BACKEND_INTERRUPT(12)
BACKEND_INTERRUPT(13)
BACKEND_INTERRUPT(14)
BACKEND_INTERRUPT(15)

static backend_interrupt_t backend_interrupt_handlers[RUMPRUN_NUM_OF_APPS] = {
	_backend_interrupt_handler_0,
	_backend_interrupt_handler_1,
	_backend_interrupt_handler_2,
	_backend_interrupt_handler_3,
	_backend_interrupt_handler_4,
	_backend_interrupt_handler_5,
	_backend_interrupt_handler_6,
	_backend_interrupt_handler_7,
	_backend_interrupt_handler_8,
	_backend_interrupt_handler_9,
	_backend_interrupt_handler_10,
	_backend_interrupt_handler_11,
	_backend_interrupt_handler_12,
	_backend_interrupt_handler_13,
	_backend_interrupt_handler_14,
	_backend_interrupt_handler_15
};

static void backend_register_ip_addrpool(void)
{
	int err, i;

	/* Init the IP address pool */
	for (i = 0; i < RUMPRUN_SERVICE_IPS; i++) {
		ip_addrpool[i].ipaddr = ifconfigd_ipaddr + ((uint32_t) (i + 1) << 24); /* IP + i + 1*/
		ip_addrpool[i].netmask = ifconfigd_netmask;
		ip_addrpool[i].mtu = ifconfigd_mtu;
	}

	/* Shared IP address and MTU */
	ip_addrpool[RUMPRUN_SERVICE_IPS].ipaddr = ifconfigd_ipaddr;
	ip_addrpool[RUMPRUN_SERVICE_IPS].netmask = ifconfigd_netmask;
	ip_addrpool[RUMPRUN_SERVICE_IPS].mtu = ifconfigd_mtu;

	err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER_IP, 0, ip_addrpool);
	if (err) bmk_platform_halt("HYP register IP address fails\n");
}

static struct semaphore sleeper_sem;

static void sleeper_up(void)
{
	up(&sleeper_sem);
}

static void backend_ip_register(void *arg)
{
	/* Give us a rump kernel context */
	rumpuser__hyp.hyp_schedule();
	rumpuser__hyp.hyp_lwproc_newlwp(0);
	rumpuser__hyp.hyp_unschedule();

	/* After this, ifconfig of backend set the shared
	   IP address, netmask, and MTU */
	down(&sleeper_sem);

	/* Register the shared IP address and MTU */
	backend_register_ip_addrpool();
}

static void backend_welcome_handler(evtchn_port_t port, struct pt_regs *regs,
		void *data)
{
	backend_connect(port);
}

void backend_init(struct ifnet *ifp)
{
	static _Atomic(int) init = 0;
	int init_old = 0;
	int err = 0;
	unsigned int i;
	frontend_connect_t reconnect_app_dom_info[RUMPRUN_NUM_OF_APPS];

	/* allow only one thread to enter, just one interface for now */
	if (!atomic_compare_exchange_strong(&init, &init_old, 1))
		return;

	bmk_printf("Initializing netdom-backend...\n");

	/* allocate port table in the backend_connect_t */
	network_dom_info.port = bmk_memalloc(sizeof(*network_dom_info.port) * RUMPRUN_NUM_OF_APPS, 0, BMK_MEMWHO_RUMPKERN);
	if (network_dom_info.port == NULL)
		bmk_platform_halt("Network_dom_info.port fails\n");

	/* init portmap */
	init_portmap();

	/* register interface */
	backend_ifp = ifp;

	err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_RECONNECT, 0, reconnect_app_dom_info);
	if (err) bmk_printf("HYP fetch fails in backend_init\n");

	for (i = 0; i < RUMPRUN_NUM_OF_APPS; i++)
	{
		if (reconnect_app_dom_info[i].status == RUMPRUN_FRONTEND_ACTIVE)
		{
			bmk_printf("Reconnect routine\n");

			atomic_fetch_add(&reconnection, 1);
			/* reconnect routine */
			err = minios_evtchn_bind_interdomain(reconnect_app_dom_info[i].domid,
					reconnect_app_dom_info[i].hello_port, backend_welcome_handler,
					"reconnect", &reconnect_welcome_port[i]);
			if (err) bmk_printf("\nBinding welcome port fails, plase do clean up\n");

			minios_clear_evtchn(reconnect_welcome_port[i]);
			minios_unmask_evtchn(reconnect_welcome_port[i]);
		}
	}
	if (atomic_load(&reconnection))
		goto backend_register;

	bmk_printf("Regular routine\n");

	/* assign welcome port */
	err = minios_evtchn_alloc_unbound(DOMID_BACKEND, backend_welcome_handler,
			"regular", &network_dom_info.welcome_port);
	if (err) bmk_printf("Alloc welcome port fails\n");

	minios_unmask_evtchn(network_dom_info.welcome_port);

backend_register:
	/* register backend */
	err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER, 0, &network_dom_info);
	if (err) bmk_printf("HYP register fails\n");

	if (atomic_load(&reconnection))
	{
		for (i = 0; i < RUMPRUN_NUM_OF_APPS; i++)
		{
			if (reconnect_app_dom_info[i].status == RUMPRUN_FRONTEND_ACTIVE)
			{
				err = minios_notify_remote_via_evtchn(reconnect_welcome_port[i]);
				if (err) bmk_printf("Notify to frontend fails on dom %d, err: %d\n", i, err);
			}
		}
	}

	/* Create a thread doing IP pool registration */
	init_SEMAPHORE(&sleeper_sem, 0);
	ifconfigd_up = sleeper_up;
	ip_register_thread = bmk_sched_create("backend_ip_register",
			NULL, 1, -1, backend_ip_register, NULL, NULL, 0);
	if (ip_register_thread == NULL)
		bmk_platform_halt("fatal thread creation failure: ip_register\n");
}

struct receiver_block_data {
	struct bmk_block_data header;
	unsigned int dom;
};

static void
receiver_callback(struct bmk_thread *prev, struct bmk_block_data *_block)
{
	struct receiver_block_data *block =
		(struct receiver_block_data *) _block;
	unsigned int dom = block->dom;
	long old = -1;
	if (!atomic_compare_exchange_strong(&rx_aring[dom]->readers, &old, 0))
		bmk_sched_wake(rx_threads[dom].thread);
}

static void backend_forward_receiver(void *arg)
{
	struct backend_thread * bt = arg;
	struct receiver_block_data data;
	unsigned int dom = bt->dom;
	unsigned long *slot, len;
	size_t idx, fails;

	data.header.callback = receiver_callback;
	data.dom = dom;

	/* Give us a rump kernel context */
	rumpuser__hyp.hyp_schedule();
	rumpuser__hyp.hyp_lwproc_newlwp(0);
	rumpuser__hyp.hyp_unschedule();

	atomic_store(&rx_aring[dom]->readers, 1);
start_over:
	fails = 0;
again:
	while ((idx = lfring_dequeue((struct lfring *) rx_aring[dom]->ring,
			NETDOM_RING_ORDER, false)) != LFRING_EMPTY) {
retry:
		fails = 0;
		slot = (unsigned long *) (rx_buf[dom] + idx * NETDOM_DATA_SIZE);
		len = slot[0];

		rumpuser__hyp.hyp_schedule();
		rump_virtif_pktforward(backend_ifp, slot + 1, len);
		rumpuser__hyp.hyp_unschedule();
		lfring_enqueue((struct lfring *) rx_fring[dom]->ring,
				NETDOM_RING_ORDER, idx, false);
	}
	if (++fails < 256) {
		bmk_sched_yield();
		goto again;
	}
	/* Shut down the thread */
	atomic_store(&rx_aring[dom]->readers, -1);
	idx = lfring_dequeue((struct lfring *) rx_aring[dom]->ring,
			NETDOM_RING_ORDER, false);
	if (idx != LFRING_EMPTY)
		goto retry;
	bmk_sched_blockprepare();
	bmk_sched_block(&data.header);
	goto start_over;
}

static void backend_forward_send(unsigned int dom, struct mbuf *m0)
{
	unsigned long * slot;
	void * data;
	struct mbuf *m;
	size_t idx;

	if (!tx_fring[dom]) {
		bmk_printf("back ring not yet set\n");
		return;
	}

	idx = lfring_dequeue((struct lfring *) tx_fring[dom]->ring,
			NETDOM_RING_ORDER, false);
	if (idx == LFRING_EMPTY)
		return;

	slot = (unsigned long *) (tx_buf[dom] + idx * NETDOM_DATA_SIZE);
	slot[0] = 0;
	data = (unsigned char *) (slot + 1);
	for (m = m0; m != NULL; m = m->m_next) {
		slot[0] += m->m_len;
		if (slot[0] > NETDOM_DATA_SIZE - sizeof(unsigned long))
			bmk_platform_halt("frontend_send: a packet is too large");
		data = bmk_mempcpy(data, mtod(m, void *), m->m_len);
	}

	lfring_enqueue((struct lfring *) tx_aring[dom]->ring,
			NETDOM_RING_ORDER, idx, false);
	/* Wake up the other side. */
	if (atomic_load(&tx_aring[dom]->readers) <= 0)
		minios_notify_remote_via_evtchn(network_dom_info.port[dom]);
}

static evtchn_port_t get_dom(evtchn_port_t port)
{
	unsigned int i;
	for (i = 0; i < RUMPRUN_NUM_OF_APPS; i++)
	{
		if(reconnect_welcome_port[i] == port)
			return i;
	}
	return -1;
}

void backend_connect(evtchn_port_t port)
{
	unsigned int dom;
	int err = 0;
	int ret = 1;
	struct netdom_fring *fring;
	frontend_grefs_t *rx_grefs, *tx_grefs;
	uint32_t domids[1];
	int _reconnection = 0;

	/*
	 * frontend_dom is an internal domid only used in this network driver.
	 * Note that domid in Xenstore is NOT related to the frontend_dom.
	 */

	if (atomic_load(&reconnection))
	{
		/* app_dom_info[] should be synchronized with app_data[] in Xen */
		dom = get_dom(port);
		if (dom == -1)
			bmk_platform_halt("reconnection fails on backend_connect\n");
	}
	else
	{
		dom = atomic_fetch_add(&frontend_dom, 1);
		if (dom > RUMPRUN_NUM_OF_APPS)
			bmk_platform_halt("Too many frontend domains\n");
	}

	ret = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_FETCH, 0, app_dom_info);
	if (ret)
		bmk_printf("HYP fetch fails\n");

	domids[0] = app_dom_info[dom].domid;

	gntmap_init(&backend_map[dom]);

	/* tx ring of network domain linked to rx ring of app domain */
	tx_grefs = gntmap_map_grant_refs(&backend_map[dom],
			2, domids, 0, app_dom_info[dom].grefs, 1);
	tx_aring[dom] = gntmap_map_grant_refs(&backend_map[dom],
			3, domids, 0, tx_grefs->aring_grefs, 1);
	fring = gntmap_map_grant_refs(&backend_map[dom],
			3, domids, 0, tx_grefs->fring_grefs, 1);
	tx_buf[dom] = gntmap_map_grant_refs(&backend_map[dom],
		NETDOM_RING_DATA_PAGES, domids, 0, tx_grefs->buf_grefs, 1);

	/* rx ring of network domain linked to tx ring of app domain */
	rx_grefs = gntmap_map_grant_refs(&backend_map[dom],
			2, domids, 0, tx_grefs->next_grefs, 1);
	rx_aring[dom] = gntmap_map_grant_refs(&backend_map[dom],
			3, domids, 0, rx_grefs->aring_grefs, 1);
	rx_fring[dom] = gntmap_map_grant_refs(&backend_map[dom],
			3, domids, 0, rx_grefs->fring_grefs, 1);
	rx_buf[dom] = gntmap_map_grant_refs(&backend_map[dom],
		NETDOM_RING_DATA_PAGES, domids, 0, rx_grefs->buf_grefs, 1);

	gntmap_munmap(&backend_map[dom], (unsigned long) tx_grefs, 2);
	gntmap_munmap(&backend_map[dom], (unsigned long) rx_grefs, 2);

	/* initialize TX free ring when everything is ready */
	__asm__ __volatile__("" ::: "memory");
	tx_fring[dom] = fring;

	/* create a receiver thread */
	rx_threads[dom].dom = dom;
	rx_threads[dom].thread = bmk_sched_create("backend_receiver", NULL, 1,
		-1, backend_forward_receiver, &rx_threads[dom], NULL, 0);
	if (rx_threads[dom].thread == NULL)
		bmk_platform_halt("fatal thread creation failure\n");

	__asm__ __volatile__("" ::: "memory");

	/* bind the port to the app dom */
	err = minios_evtchn_bind_interdomain(app_dom_info[dom].domid,
		app_dom_info[dom].port, backend_interrupt_handlers[dom], NULL,
		&network_dom_info.port[dom]);
	if (err)
		bmk_printf("Bind interdomain fails\n");

	minios_unmask_evtchn(network_dom_info.port[dom]);

	if ( (_reconnection = atomic_load(&reconnection)) <= 1)
	{
		/* assign new welcome port */
		err = minios_evtchn_alloc_unbound(DOMID_BACKEND,
			backend_welcome_handler, NULL, &network_dom_info.welcome_port);
		if (err)
			bmk_printf("Alloc welcome port fails\n");

		/* register backend */
		err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER, 0,
				&network_dom_info);
		if (err)
			bmk_printf("HYP register fails\n");

		minios_unmask_evtchn(network_dom_info.welcome_port);

		if (_reconnection == 0)
			goto out;
	}
	atomic_fetch_sub(&reconnection, 1);
out:
	bmk_printf("Connected netdom-frontend\n");
}

/* assume a little endian host */
#define _ETHERTYPE_SWAP(x)	((uint16_t)(((x) << 8) | ((x) >> 8)))

#define _ETHERTYPE_IP		0x0800
#define _ETHERTYPE_ARP		0x0806
#define ETHERTYPE_IP		_ETHERTYPE_SWAP(_ETHERTYPE_IP)
#define ETHERTYPE_ARP		_ETHERTYPE_SWAP(_ETHERTYPE_ARP)

int
backend_receive(struct mbuf *m0)
{
	portmap_entry_t *portmap;
	uint16_t type, port;
	unsigned char *data;

	data = mtod(m0, void *);
	if (m0->m_len < 38)
		return BMK_EINVAL;

	/* get an ethertype */
	type = *(uint16_t *) (data + 12);
	if (type == ETHERTYPE_IP) { /* IP */
		if (data[23] == 0x06) {
			portmap = tcp_portmap;
		} else if (data[23] == 0x11) {
			portmap = udp_portmap;
		} else {
			return BMK_ENOENT;
		}
	} else if (type == ETHERTYPE_ARP) { /* ARP */
		unsigned int i, count = atomic_load(&frontend_dom);
		for (i = 0; i < count; i++) {
			backend_forward_send(i, m0);
		}
		return BMK_ENOENT; /* let backend also get the packet */
	} else {
		return BMK_ENOENT;
	}

	/* get a TCP/UDP port number */
	port = *(uint16_t *) (data + 36);

	/* broadcast */
	if (port == 0 || port == 0xFFFF) {
		unsigned int i, count = atomic_load(&frontend_dom);
		for (i = 0; i < count; i++) {
			backend_forward_send(i, m0);
		}
		return BMK_ENOENT; /* let backend also get the packet */
	} else {
		/* port map here */
		unsigned int app_dom = portmap[port].dom;
		if ((int) app_dom == -1) {
			bmk_printf("target dom: %d failed  \n", app_dom);
			return BMK_ENOENT;
		}

		backend_forward_send(app_dom, m0);
	}

	return 0;
}
