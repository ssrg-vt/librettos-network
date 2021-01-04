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
#include <mini-os/gnttab.h>
#include <mini-os/gntmap.h>
#include <mini-os/semaphore.h>

#include <bmk-core/errno.h>
#include <bmk-core/memalloc.h>
#include <bmk-core/pgalloc.h>
#include <bmk-core/string.h>
#include <bmk-core/sched.h>
#include <bmk-core/platform.h>
#include <bmk-core/printf.h>

#include <bmk-rumpuser/core_types.h>
#include <bmk-rumpuser/rumpuser.h>

#include <xen/network.h>
#include <xen/network_ring.h>
#include <xen/network_hypercall.h>

#include "../librumpnet_xenif/if_virt_user.h"

#define frontend_virt_to_pfn(a)	((unsigned long) (a) >> PAGE_SHIFT)

static backend_connect_t network_dom_info;
static frontend_connect_t app_dom_info;

#define NETDOM_ARING(fring)	\
	((struct netdom_aring *) ((char *) fring + 3 * PAGE_SIZE))
#define NETDOM_BUF(fring)	\
	((char *) fring + 6 * PAGE_SIZE)

static struct netdom_fring *tx_fring = NULL;
static struct netdom_fring *rx_fring;
static struct bmk_thread *rx_thread;
static struct bmk_thread *reconnect_thread;
static struct bmk_thread *switch_thread;
static frontend_grefs_t *tx_grefs;
static frontend_grefs_t *rx_grefs;

static struct virtif_sc * frontend_vif_sc = NULL;

static _Atomic(int) frontend_terminating = ATOMIC_VAR_INIT(0);
static _Atomic(long) reconnecters;
static _Atomic(long) switchers;

int frontend_portbind(uint16_t *port, uint8_t protocol)
{
	bmk_printf("Binding port[%hx]...", *port);

	int res = HYPERVISOR_rumprun_port_bind(0, port, protocol);
	if (res < 0) bmk_printf("HYP port bind fails, err: %d\n", res);

	bmk_printf("done\n");

	return res;
}

static void frontend_hello_handler(evtchn_port_t port, struct pt_regs *regs,
		void *data)
{
	if (atomic_exchange(&reconnecters, 1) == 0)
		bmk_sched_wake(reconnect_thread);
}

static void frontend_switch_handler(evtchn_port_t port, struct pt_regs *regs,
		void *data)
{
	if (atomic_exchange(&switchers, 1) == 0)
		bmk_sched_wake(switch_thread);
}

static void frontend_interrupt_handler(evtchn_port_t port,
		struct pt_regs *regs, void *data)
{
	if (atomic_exchange(&NETDOM_ARING(rx_fring)->readers, 1) == 0)
		bmk_sched_wake(rx_thread);
}

static void frontend_terminate_ring(frontend_grefs_t **pgrefs, uint32_t *result)
{
	size_t i;
	frontend_grefs_t *grefs;

	result[0] = gnttab_end_access(result[0]);
	result[0] = gnttab_end_access(result[1]);

	grefs = *pgrefs;
	gnttab_end_access(grefs->aring_grefs[0]);
	gnttab_end_access(grefs->aring_grefs[1]);
	gnttab_end_access(grefs->aring_grefs[2]);

	gnttab_end_access(grefs->fring_grefs[0]);
	gnttab_end_access(grefs->fring_grefs[1]);
	gnttab_end_access(grefs->fring_grefs[2]);

	for (i = 0; i < NETDOM_RING_DATA_PAGES; i++)
		gnttab_end_access(grefs->buf_grefs[i]);
}

static struct netdom_fring * frontend_init_ring(frontend_grefs_t **pgrefs,
		uint32_t *result)
{
	struct netdom_aring *aring;
	struct netdom_fring *fring;
	size_t i;
	frontend_grefs_t *grefs;
	void *buf;

	grefs = bmk_pgalloc(1);
	if (!grefs)
		bmk_platform_halt("grefs shared page not allocated\n");
	*pgrefs = grefs;
	result[0] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn(grefs), 0);
	result[1] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn((char *) grefs + PAGE_SIZE), 0);

	fring = bmk_pgalloc(gntmap_map2order(6 + NETDOM_RING_DATA_PAGES));
	if (!fring)
		bmk_platform_halt("shared pages are not allocated\n");
	aring = NETDOM_ARING(fring);
	grefs->aring_grefs[0] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn(aring), 0);
	grefs->aring_grefs[1] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn((char *) aring + PAGE_SIZE), 0);
	grefs->aring_grefs[2] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn((char *) aring + 2 * PAGE_SIZE), 0);
	grefs->fring_grefs[0] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn(fring), 0);
	grefs->fring_grefs[1] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn((char *) fring + PAGE_SIZE), 0);
	grefs->fring_grefs[2] = gnttab_grant_access(network_dom_info.domid,
			frontend_virt_to_pfn((char *) fring + 2 * PAGE_SIZE), 0);
	buf = NETDOM_BUF(fring);
	lfring_init_empty((struct lfring *) aring->ring, NETDOM_RING_ORDER);
	lfring_init_full((struct lfring *) fring->ring, NETDOM_RING_ORDER);
	atomic_init(&aring->readers, 0);
	atomic_signal_fence(memory_order_seq_cst);
	for (i = 0; i < NETDOM_RING_DATA_PAGES; i++) {
		grefs->buf_grefs[i] = gnttab_grant_access(
			network_dom_info.domid,
			frontend_virt_to_pfn(buf + i * PAGE_SIZE), 0);
	}

	return fring;
}

static void
receiver_callback(struct bmk_thread *prev, struct bmk_block_data *_block)
{
	long old = -1;
	if (!atomic_compare_exchange_strong(&NETDOM_ARING(rx_fring)->readers, &old, 0))
		bmk_sched_wake(rx_thread);
}

static struct bmk_block_data receiver_data = { .callback = receiver_callback };

static void frontend_receiver(void *arg)
{

	unsigned long *slot, len;
	size_t idx, fails;

	if (atomic_load(&frontend_terminating))
		return;

	/* Give us a rump kernel context */
	rumpuser__hyp.hyp_schedule();
	rumpuser__hyp.hyp_lwproc_newlwp(0);
	rumpuser__hyp.hyp_unschedule();

	atomic_store(&NETDOM_ARING(rx_fring)->readers, 1);
start_over:
	fails = 0;
again:
	while ((idx = lfring_dequeue((struct lfring *) NETDOM_ARING(rx_fring)->ring,
			NETDOM_RING_ORDER, false)) != LFRING_EMPTY) {
retry:
		fails = 0;
		slot = (unsigned long *) (NETDOM_BUF(rx_fring) + idx * NETDOM_DATA_SIZE);
		len = slot[0];
		rumpuser__hyp.hyp_schedule();
		rump_virtif_pktdeliver(frontend_vif_sc, slot + 1, len);
		rumpuser__hyp.hyp_unschedule();
		lfring_enqueue((struct lfring *) rx_fring->ring,
				NETDOM_RING_ORDER, idx, false);
	}
	if (++fails < 256) {
		bmk_sched_yield();
		goto again;
	}

	/* Shut down the thread */
	atomic_store(&NETDOM_ARING(rx_fring)->readers, -1);
	idx = lfring_dequeue((struct lfring *) NETDOM_ARING(rx_fring)->ring,
			NETDOM_RING_ORDER, false);
	if (idx != LFRING_EMPTY)
		goto retry;
	bmk_sched_blockprepare();
	bmk_sched_block(&receiver_data);
	if (atomic_load(&frontend_terminating))
		return;
	goto start_over;
}

void frontend_send(struct mbuf *m0)
{
	unsigned long * slot;
	void * data;
	struct mbuf *m;
	size_t idx;

	if (!tx_fring)
		return;

	idx = lfring_dequeue((struct lfring *) tx_fring->ring,
		NETDOM_RING_ORDER, false);
	if (idx == LFRING_EMPTY)
		return;

	slot = (unsigned long *) (NETDOM_BUF(tx_fring) + idx * NETDOM_DATA_SIZE);
	slot[0] = 0;
	data = (unsigned char *) (slot + 1);
	for (m = m0; m != NULL; m = m->m_next) {
		slot[0] += m->m_len;
		if (slot[0] > NETDOM_DATA_SIZE - sizeof(unsigned long))
			bmk_platform_halt("frontend_send: a packet is too large");
		data = bmk_mempcpy(data, mtod(m, void *), m->m_len);
	}

	lfring_enqueue((struct lfring *) NETDOM_ARING(tx_fring)->ring,
			NETDOM_RING_ORDER, idx, false);

	/* Wake up the other side. */
	if (atomic_load(&NETDOM_ARING(tx_fring)->readers) <= 0)
		minios_notify_remote_via_evtchn(app_dom_info.port);
}

void frontend_terminate(void)
{
	atomic_store(&frontend_terminating, 1);
	return frontend_interrupt_handler(0, NULL, NULL);
}

void frontend_join(void)
{
	if (atomic_load(&frontend_terminating) != 1)
		bmk_platform_halt("frontend_terminating != 1\n");
	bmk_sched_join(rx_thread);
}

static void
reconnect_callback(struct bmk_thread *prev, struct bmk_block_data *_block)
{
	long old = -1;
	if (!atomic_compare_exchange_strong(&reconnecters, &old, 0))
		bmk_sched_wake(reconnect_thread);
}
static struct bmk_block_data reconnect_data = { .callback = reconnect_callback };

static void frontend_reconnecter(void *arg)
{
	int err;
	struct netdom_fring *_rx_fring, *_tx_fring;
	evtchn_port_t old_hello_port;

	/* Give us a rump kernel context */
	rumpuser__hyp.hyp_schedule();
	rumpuser__hyp.hyp_lwproc_newlwp(0);
	rumpuser__hyp.hyp_unschedule();

	while (1)
	{
		atomic_store(&reconnecters, -1);
		bmk_sched_blockprepare();
		bmk_sched_block(&reconnect_data);

		bmk_printf("Frontend-reconnecter wakes up\n");

		frontend_terminate_ring(&tx_grefs, rx_grefs->next_grefs);
		frontend_terminate_ring(&rx_grefs, app_dom_info.grefs);

		err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_QUERY, 0,
			&network_dom_info);
		if (err)
			bmk_platform_halt("HYP query fails");

		/* init the front-end rx_ring buffer */
		_rx_fring = frontend_init_ring(&rx_grefs, app_dom_info.grefs);

		/* init the front-end tx_ring buffer */
		_tx_fring = frontend_init_ring(&tx_grefs, rx_grefs->next_grefs);

		/* end of list */
		tx_grefs->next_grefs[0] = -1;
		tx_grefs->next_grefs[1] = -1;

		/* initialize TX free ring when everything is ready */
		__asm__ __volatile__("" ::: "memory");
		rx_fring = _rx_fring;
		tx_fring = _tx_fring;

		/* create an main event channel with port*/
		err = minios_evtchn_alloc_unbound(network_dom_info.domid, frontend_interrupt_handler,
				NULL, &app_dom_info.port);
		if (err)
			bmk_platform_halt("main event channel alloc fails");

		old_hello_port = app_dom_info.hello_port;

		err = minios_evtchn_alloc_unbound(DOMID_BACKEND, frontend_hello_handler,
				NULL, &app_dom_info.hello_port);
		if (err)
			bmk_platform_halt("main event channel alloc fails");

		/* register frontend */
		if (dynamic_mode == 1) {
			err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER_APP_DYNAMIC, 0, &app_dom_info);
			if (err) bmk_platform_halt("HYP register app dynamic fails\n");
		}
		else
		{
			err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER_APP, 0, &app_dom_info);
			if (err) bmk_platform_halt("HYP register app fails\n");
		}

		/* say hello to backend */
		err = minios_notify_remote_via_evtchn(old_hello_port);
		if (err)
			bmk_printf("notify welcome port fails\n");

		minios_unmask_evtchn(app_dom_info.port);
		minios_unmask_evtchn(app_dom_info.hello_port);
	}
}

static void
switch_callback(struct bmk_thread *prev, struct bmk_block_data *_block)
{
	long old = -1;
	if (!atomic_compare_exchange_strong(&switchers, &old, 0))
		bmk_sched_wake(switch_thread);
}
static struct bmk_block_data switch_data = { .callback = switch_callback };

static void frontend_switcher(void *arg)
{
	/* Give us a rump kernel context */
	rumpuser__hyp.hyp_schedule();
	rumpuser__hyp.hyp_lwproc_newlwp(0);
	rumpuser__hyp.hyp_unschedule();

	while (1)
	{
		atomic_store(&switchers, -1);
		bmk_sched_blockprepare();
		bmk_sched_block(&switch_data);

		rump_virtif_switch();
	}
}

static inline void frontend_delay(bmk_time_t delay)
{
	bmk_time_t start = bmk_platform_cpu_clock_monotonic();
	while (bmk_platform_cpu_clock_monotonic() - start < delay)
		;
}

void frontend_init(struct virtif_sc * vif_sc)
{
	struct netdom_fring *_tx_fring, *_rx_fring;
	int err = 0;
	evtchn_port_t old_hello_port;
	pool_entry_t ip;

	frontend_vif_sc = vif_sc;

	bmk_printf("Initializing netdom-frontend...\n");
	err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_QUERY, 0,
			&network_dom_info);
	if (err)
		bmk_platform_halt("HYP query fails");

	/* init the front-end rx_ring buffer */
	_rx_fring = frontend_init_ring(&rx_grefs, app_dom_info.grefs);

	/* init the front-end tx_ring buffer */
	_tx_fring = frontend_init_ring(&tx_grefs, rx_grefs->next_grefs);

	/* end of list */
	tx_grefs->next_grefs[0] = -1;
	tx_grefs->next_grefs[1] = -1;

	/* initialize TX free ring when everything is ready */
	__asm__ __volatile__("" ::: "memory");
	rx_fring = _rx_fring;
	tx_fring = _tx_fring;

	rx_thread = bmk_sched_create("frontend_receiver",
		NULL, 1, -1, frontend_receiver, NULL, NULL, 0);

	atomic_init(&reconnecters, 1);

	reconnect_thread = bmk_sched_create("frontend_reconnecter",
		NULL, 1, -1, frontend_reconnecter, NULL, NULL, 0);

	atomic_init(&switchers, 1);

	switch_thread = bmk_sched_create("frontend_switcher",
		NULL, 1, -1, frontend_switcher, NULL, NULL, 0);

	__asm__ __volatile__("" ::: "memory");

	/* bind the channel to backend's welcome port */
	err = minios_evtchn_bind_interdomain(network_dom_info.domid,
			network_dom_info.welcome_port, frontend_hello_handler,
			NULL, &app_dom_info.hello_port);
	if (err)
		bmk_platform_halt("bind interdomain fails");

	/* create an main event channel with port*/
	err = minios_evtchn_alloc_unbound(network_dom_info.domid,
			frontend_interrupt_handler, NULL, &app_dom_info.port);
	if (err)
		bmk_platform_halt("main event channel alloc fails");

	old_hello_port = app_dom_info.hello_port;

	err = minios_evtchn_alloc_unbound(DOMID_BACKEND,
			frontend_hello_handler, NULL, &app_dom_info.hello_port);
	if (err)
		bmk_platform_halt("main event channel alloc fails");

	app_dom_info.status = RUMPRUN_FRONTEND_ACTIVE;

	/* register frontend */
	if (dynamic_mode == 1) {
		err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER_APP_DYNAMIC, 0, &app_dom_info);
		if (err) bmk_platform_halt("HYP register app dynamic fails\n");
	} else {
		err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_REGISTER_APP, 0, &app_dom_info);
		if (err) bmk_platform_halt("HYP register app fails\n");
	}

	/* say hello to backend */
	err = minios_notify_remote_via_evtchn(old_hello_port);
	if (err)
		bmk_printf("notify welcome port fails\n");

	/* mode switch event */
	evtchn_port_t mode_switch_port;
	err = minios_evtchn_alloc_unbound(0,
	frontend_switch_handler, NULL, &mode_switch_port);
	if (err)
		bmk_platform_halt("mode switch event channel alloc fails");
	bmk_printf("mode switch event port: %d\n", mode_switch_port);

	minios_unmask_evtchn(app_dom_info.port);
	minios_unmask_evtchn(app_dom_info.hello_port);
	minios_unmask_evtchn(mode_switch_port);

	bmk_printf("fetching an IP address...\n");
	while (!ifconfigd_ipaddr) {
		/* Fetch an IP address, netmask, and MTU from Xen */
		err = HYPERVISOR_rumprun_service_op(RUMPRUN_SERVICE_FETCH_IP, 0, &ip);
		if (err)
			bmk_platform_halt("HYP fetch ip fails\n");
		ifconfigd_ipaddr = ip.ipaddr;
		ifconfigd_netmask = ip.netmask;
		ifconfigd_mtu = ip.mtu;
		frontend_delay(200 * 1000000ULL);
	}
}
