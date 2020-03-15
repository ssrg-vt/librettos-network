#ifndef __NETWORK_RING_H__
#define __NETWORK_RING_H__

#include <bmk-core/types.h>
#include <bmk-core/lfring.h>
#include <bmk-pcpu/pcpu.h>

#define NETDOM_RING_ORDER	9
#define NETDOM_RING_SIZE	(1U << NETDOM_RING_ORDER)

#define NETDOM_DATA_SIZE	9216
#define NETDOM_RING_DATA_PAGES	((NETDOM_DATA_SIZE * NETDOM_RING_SIZE +	\
			BMK_PCPU_PAGE_SIZE - 1) / BMK_PCPU_PAGE_SIZE)

struct netdom_aring {
	_Alignas(LF_CACHE_BYTES) _Atomic(long) readers;
	_Alignas(LFRING_ALIGN) char ring[0];
};

struct netdom_fring {
	_Alignas(LFRING_ALIGN) char ring[0];
};

#endif
