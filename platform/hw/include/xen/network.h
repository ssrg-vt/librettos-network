#ifndef __NETWORK_H__
#define __NETWORK_H__

#include <xen/_rumprun.h>
#include <bmk-core/types.h>

#include "../../../../ifconfig/ifconfig.h"

typedef uint32_t evtchn_port_t;
typedef uint32_t grant_ref_t;

typedef struct frontend_grefs {
	grant_ref_t next_grefs[2];
	grant_ref_t aring_grefs[2];
	grant_ref_t fring_grefs[2];
	grant_ref_t buf_grefs[0];
} frontend_grefs_t;

typedef struct frontend_control_packet {
	uint32_t magic;
	uint16_t port;
	uint8_t protocol;   /* TCP: 6, UDP: 17 */
} frontend_control_packet_t;

struct virtif_sc;
struct ifnet;

/* Mirrors mbuf definition in NetBSD */
#define mtod(m, t) ((t)((m)->m_data))
struct mbuf {
	struct mbuf *m_next;
	struct mbuf *m_nextpkt;
	char *m_data;
	void *m_owner;
	int m_len;
	int m_flags;
	/* Other fields follow */
};

void frontend_init(struct virtif_sc *);
void frontend_terminate(void);
void frontend_join(void);
void frontend_send(struct mbuf *m0);
int frontend_portbind(uint16_t *port, uint8_t protocol);

/* backend driver */
void backend_init(struct ifnet *ifp);
void backend_connect(evtchn_port_t port);
int backend_receive(struct mbuf *m0);

extern uint32_t *HYPERVISOR_netdom_map;

#endif
