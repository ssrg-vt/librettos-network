/*-
 * Copyright (c) 2020 Ruslan Nikolaev.  All Rights Reserved.
 *
 * A markable ABA-safe M&S queue.
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

#ifndef __LFQUEUE_H
#define __LFQUEUE_H	1

#include <stdbool.h>

#include "lf/lf.h"

struct lfqueue_node {
	LFATOMIC(lfatomic_aba_t) next;
	void *object;
	unsigned long index; /* Used outside. */
};

struct lfqueue {
	_Alignas(LF_CACHE_BYTES) LFATOMIC(lfatomic_aba_t) head;
	_Alignas(LF_CACHE_BYTES) LFATOMIC(lfatomic_aba_t) tail;
	_Alignas(LF_CACHE_BYTES) char _pad[0];
};

#define __LFQ_CNT	0x1U /* When used as a counter. */
#define __LFQ_INC	0x2U /* A step for the markable queue counter. */

#define __LFQ_NULL	((void *) -1L)

static inline void lfqueue_init(struct lfqueue *queue,
		struct lfqueue_node *node)
{
	lfatomic_aba_t next = { .value = __LFQ_NULL, .stamp = 0 };
	lfatomic_aba_t value = { .value = node, .stamp = 0 };

	__lfaba_init(&node->next, next);
	__lfaba_init(&queue->head, value);
	__lfaba_init(&queue->tail, value);
}

static inline struct lfqueue_node *lfqueue_sentinel(struct lfqueue *queue)
{
	return __lfaba_load_value(&queue->head, memory_order_acquire);
}

static inline bool lfqueue_enqueue(struct lfqueue *queue,
		struct lfqueue_node *node, bool mark)
{
	lfatomic_aba_t tail, next, value;
	struct lfqueue_node *curr;

	__lfaba_store_value(&node->next, __LFQ_NULL, memory_order_relaxed);

	tail = __lfaba_load(&queue->tail, memory_order_acquire);
	while (1) {
		curr = (struct lfqueue_node *) tail.value;
		next = __lfaba_load(&curr->next, memory_order_acquire);
		while (1) {
			if (!((unsigned long) next.value & __LFQ_CNT)) {
				value.value = next.value;
				value.stamp = tail.stamp + 1;
				if (__lfaba_cmpxchg_weak(&queue->tail, &tail, value,
						memory_order_acq_rel, memory_order_acquire))
					tail = value;
				break;
			}
			if (!__lfaba_reload(&queue->tail, &tail, memory_order_acquire))
				break;
			if (mark && next.value != __LFQ_NULL) {
				value.value = (char *) next.value + __LFQ_INC;
				value.stamp = next.stamp + 1;
				curr = (struct lfqueue_node *) tail.value;
				if (__lfaba_cmpxchg_weak(&curr->next, &next, value,
						memory_order_acq_rel, memory_order_acquire))
					return false;
				continue;
			}
			value.value = node;
			value.stamp = next.stamp + 1;
			curr = (struct lfqueue_node *) tail.value;
			if (__lfaba_cmpxchg_weak(&curr->next, &next, value,
					memory_order_acq_rel, memory_order_acquire)) {
				value.value = node;
				value.stamp = tail.stamp + 1;
				__lfaba_cmpxchg_weak(&queue->tail, &tail, value,
					memory_order_acq_rel, memory_order_relaxed);
				return true;
			}
		}
	}
}

static inline struct lfqueue_node *__lfqueue_return(struct lfqueue_node *)
	__attribute__((nonnull));

static inline struct lfqueue_node *__lfqueue_return(struct lfqueue_node *node)
{
	return node;
}

static inline struct lfqueue_node *lfqueue_dequeue(struct lfqueue *queue,
		bool mark)
{
	lfatomic_aba_t head, tail, next, value;
	struct lfqueue_node *curr;
	void *object;

	head = __lfaba_load(&queue->head, memory_order_acquire);
	while (1) {
		tail = __lfaba_load(&queue->tail, memory_order_acquire);
		curr = (struct lfqueue_node *) head.value;
		next = __lfaba_load(&curr->next, memory_order_acquire);
repeat:
		if (!__lfaba_reload(&queue->head, &head, memory_order_acquire))
			continue;
		if ((unsigned long) next.value & __LFQ_CNT) {
			value.value = (char *) next.value - __LFQ_INC;
			value.stamp = next.stamp + 1;
			curr = (struct lfqueue_node *) head.value;
			if (!mark || __lfaba_cmpxchg_weak(&curr->next, &next, value,
							memory_order_acq_rel, memory_order_acquire))
				return NULL;
			goto repeat;
		}
		if (head.value == tail.value) {
			value.value = next.value;
			value.stamp = tail.stamp + 1;
			__lfaba_cmpxchg_strong(&queue->tail, &tail,
				value, memory_order_acq_rel, memory_order_relaxed);
		}
		curr = (struct lfqueue_node *) next.value;
		object = curr->object;
		value.value = next.value;
		value.stamp = head.stamp + 1;
		if (__lfaba_cmpxchg_weak(&queue->head, &head, value,
				memory_order_acq_rel, memory_order_acquire)) {
			curr = (struct lfqueue_node *) head.value;
			curr->object = object;
			return __lfqueue_return(curr);
		}
	}
}

#endif  /* !__LFQUEUE_H */

/* vi: set tabstop=4: */
