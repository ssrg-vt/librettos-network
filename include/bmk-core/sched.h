/*-
 * Copyright (c) 2015 Antti Kantee.  All Rights Reserved.
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

#ifndef _BMK_CORE_SCHED_H_
#define _BMK_CORE_SCHED_H_

#include <bmk-core/types.h>
#include <bmk-pcpu/pcpu.h>

#define BMK_TLS_EXTRA (6 * sizeof(unsigned long))
struct bmk_tcb {
	unsigned long btcb_sp;		/* stack pointer	*/

	unsigned long btcb_tp;		/* tls pointer		*/
#if 0
	unsigned long btcb_tpsize;	/* tls area length	*/
#endif
};

#define BMK_MAX_THREADS_ORDER	15
#define BMK_MAX_THREADS		(1UL << BMK_MAX_THREADS_ORDER)

#define BMK_MAX_BLOCKQ_ORDER	17 /* >= BMK_MAX_THREADS_ORDER */
#define BMK_MAX_BLOCKQ		(1UL << BMK_MAX_BLOCKQ_ORDER)

struct bmk_thread;

struct bmk_block_data {
	void (*callback) (struct bmk_thread *, struct bmk_block_data *);
};

void	bmk_sched_init(void);
void	bmk_sched_startmain(void (*)(void *), void *)
		__attribute__((noreturn));

void	bmk_sched_yield(void);

void	bmk_sched_dumpqueue(void);

struct bmk_thread *bmk_sched_create(const char *, void *, int,
				    int, void (*)(void *), void *,
				    void *, unsigned long);
struct bmk_thread *bmk_sched_create_withtls(const char *, void *, int,
				    int, void (*)(void *), void *,
				    void *, unsigned long, void *);
void	bmk_sched_join(struct bmk_thread *);
void	bmk_sched_exit(void) __attribute__((__noreturn__));
void	bmk_sched_exit_withtls(void) __attribute__((__noreturn__));

void	bmk_sched_blockprepare(void);
#define BMK_SCHED_BLOCK_INFTIME -1
void	bmk_sched_blockprepare_timeout(bmk_time_t,
				void (*) (struct bmk_thread *));
int	bmk_sched_block(struct bmk_block_data *);

void	bmk_sched_wake(struct bmk_thread *);
void	bmk_sched_wake_timeq(struct bmk_thread *);

void	bmk_insert_timeq(struct bmk_thread *);

void	bmk_sched_suspend(struct bmk_thread *);
void	bmk_sched_unsuspend(struct bmk_thread *);


void	*bmk_sched_tls_alloc(void);
void	bmk_sched_tls_free(void *);

void	*bmk_sched_gettcb(void);

void	bmk_cpu_sched_create(struct bmk_thread *, struct bmk_tcb *,
			     void (*)(void *), void *,
			     void *, unsigned long);

void	bmk_sched_set_hook(void (*)(void *, void *));
struct bmk_thread *bmk_sched_init_mainlwp(void *);

extern __thread struct bmk_thread *bmk_current;

int *bmk_sched_geterrno(void);
const char 	*bmk_sched_threadname(struct bmk_thread *);

void	bmk_cpu_sched_bouncer(void);
void	bmk_cpu_sched_switch(void *, struct bmk_block_data *, void *);

void	bmk_platform_cpu_sched_settls(struct bmk_tcb *);

struct bmk_block_queue {
	/* cannot use struct lfqueue here */
	__attribute__ ((aligned(BMK_PCPU_L1_SIZE))) char _queue[2*BMK_PCPU_L1_SIZE];
	struct bmk_block_data header;
};

void	bmk_block_queue_init(struct bmk_block_queue *);
void	bmk_block_queue_destroy(struct bmk_block_queue *);
void	bmk_block_queue_wake(struct bmk_block_queue *);

#endif /* _BMK_CORE_SCHED_H_ */
