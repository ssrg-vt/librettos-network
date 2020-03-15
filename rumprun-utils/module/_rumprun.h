/*
 * LibrettOS Network Server
 *
 * Copyright (c) 2018-2019 Mincheol Sung <mincheol@vt.edu>
 * Copyright (c) 2018-2019 Ruslan Nikolaev <rnikola@vt.edu>
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

#ifndef _RUMPRUN_COMMON_H
#define _RUMPRUN_COMMON_H 1

/* Hypercall operations */
#define RUMPRUN_SERVICE_CLEANUP			0
#define RUMPRUN_SERVICE_REGISTER		1
#define RUMPRUN_SERVICE_UNREGISTER		2
#define RUMPRUN_SERVICE_REGISTER_APP		3
#define RUMPRUN_SERVICE_REGISTER_APP_DYNAMIC	4
#define RUMPRUN_SERVICE_QUERY			5
#define RUMPRUN_SERVICE_FETCH			6
#define RUMPRUN_SERVICE_RECONNECT		7
#define RUMPRUN_SERVICE_REGISTER_IP		8
#define RUMPRUN_SERVICE_FETCH_IP		9

#define RUMPRUN_SYSIDS				3

#define RUMPRUN_FRONTEND_DEAD			0
#define RUMPRUN_FRONTEND_ACTIVE			1

#define RUMPRUN_NUM_OF_APPS			16
#define RUMPRUN_SERVICE_IPS			20

#ifdef __cplusplus
extern "C" {
#endif

typedef struct portmap_entry {
	union {
		struct {
			int32_t dom;
			uint32_t ipaddr; /* big endian */
		};
		uint64_t full;
	};
} portmap_entry_t;

typedef struct pool_entry {
	uint32_t ipaddr; /* big endian */
	uint32_t netmask; /* big endian */
	uint32_t mtu;
} pool_entry_t;

typedef struct frontend_connect {
	uint32_t	domid;
	uint32_t 	port;
	uint32_t 	hello_port;
	uint32_t 	status;
	uint32_t	grefs[2];
} frontend_connect_t;

typedef struct backend_connect {
	uint32_t	domid;
	uint32_t	welcome_port;
	uint32_t 	*port;
} backend_connect_t;

#ifdef __cplusplus
}
#endif

#endif /* !_RUMPRUN_COMMON_H */
