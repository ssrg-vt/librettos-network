#ifndef __HYPERCALL_H__
#define __HYPERCALL_H__

#include <mini-os/hypervisor.h>

#ifndef __HYPERVISOR_rumprun_service_op
# define __HYPERVISOR_rumprun_service_op	42
#endif

#ifndef __HYPERVISOR_rumprun_port_bind
# define __HYPERVISOR_rumprun_port_bind		43
#endif

#ifndef DOMID_BACKEND
# define DOMID_BACKEND	xen_mk_uint(0x7FFA)
#endif

static inline int
HYPERVISOR_rumprun_service_op(
		int op, int sysid, void *ptr)
{
	return _hypercall3(int, rumprun_service_op, op, sysid, ptr);
}

static inline int
HYPERVISOR_rumprun_port_bind(
		int sysid, uint16_t *port, uint8_t protocol)
{
	return _hypercall3(int, rumprun_port_bind, sysid, port, protocol);
}
#endif /* __HYPERCALL__ */
