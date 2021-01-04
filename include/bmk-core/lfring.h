/*-
 * Copyright (c) 2019 Ruslan Nikolaev.  All Rights Reserved.
 *
 * A scalable SCQ ring buffer (DISC '19)
 * Derived from https://github.com/rusnikola/lfqueue
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

#ifndef __LFRING_H
#define __LFRING_H	1

#include <stdatomic.h>
#include <stdbool.h>

#include "lf/lf.h"

#if LFATOMIC_WIDTH == 32
# define LFRING_MIN	(LF_CACHE_SHIFT - 2)
#elif LFATOMIC_WIDTH == 64
# define LFRING_MIN	(LF_CACHE_SHIFT - 3)
#elif LFATOMIC_WIDTH == 128
# define LFRING_MIN	(LF_CACHE_SHIFT - 4)
#else
# error "Unsupported LFATOMIC_WIDTH."
#endif

#define LFRING_ALIGN	(_Alignof(struct __lfring))
#define LFRING_SIZE(o)	\
	(offsetof(struct __lfring, array) + (sizeof(lfatomic_t) << ((o) + 1)))

#define LFRING_EMPTY	(~(size_t) 0U)

#define __lfring_cmp(x, op, y)	((lfsatomic_t) ((x) - (y)) op 0)

#if LFRING_MIN != 0
static inline size_t __lfring_raw_map(lfatomic_t idx, size_t order, size_t n)
{
	return (size_t) (((idx & (n - 1)) >> (order - LFRING_MIN)) |
			((idx << LFRING_MIN) & (n - 1)));
}
#else
static inline size_t __lfring_raw_map(lfatomic_t idx, size_t order, size_t n)
{
	return (size_t) (idx & (n - 1));
}
#endif

static inline size_t __lfring_map(lfatomic_t idx, size_t order, size_t n)
{
	return __lfring_raw_map(idx, order + 1, n);
}

#define __lfring_threshold3(half, n) ((long) ((half) + (n) - 1))

static inline size_t lfring_pow2(size_t order)
{
	return (size_t) 1U << order;
}

struct __lfring {
	_Alignas(LF_CACHE_BYTES) LFATOMIC(lfatomic_t) head;
	_Alignas(LF_CACHE_BYTES) LFATOMIC(lfsatomic_t) threshold;
	_Alignas(LF_CACHE_BYTES) LFATOMIC(lfatomic_t) tail;
	_Alignas(LF_CACHE_BYTES) LFATOMIC(lfatomic_t) array[1];
};

struct lfring;

static inline void lfring_init_empty(struct lfring * ring, size_t order)
{
	struct __lfring * q = (struct __lfring *) ring;
	size_t i, n = lfring_pow2(order + 1);

	for (i = 0; i != n; i++)
		atomic_init(&q->array[i], (lfsatomic_t) -1);

	atomic_init(&q->head, 0);
	atomic_init(&q->threshold, -1);
	atomic_init(&q->tail, 0);
}

static inline void lfring_init_full(struct lfring * ring, size_t order)
{
	struct __lfring * q = (struct __lfring *) ring;
	size_t i, half = lfring_pow2(order), n = half * 2;

	for (i = 0; i != half; i++)
		atomic_init(&q->array[__lfring_map(i, order, n)], __lfring_raw_map(n + i, order, half));
	for (; i != n; i++)
		atomic_init(&q->array[__lfring_map(i, order, n)], (lfsatomic_t) -1);

	atomic_init(&q->head, 0);
	atomic_init(&q->threshold, __lfring_threshold3(half, n));
	atomic_init(&q->tail, half);
}

static inline void lfring_init_fill(struct lfring * ring,
		size_t s, size_t e, size_t order)
{
	struct __lfring * q = (struct __lfring *) ring;
	size_t i, half = lfring_pow2(order), n = half * 2;

	for (i = 0; i != s; i++)
		atomic_init(&q->array[__lfring_map(i, order, n)], 2 * n - 1);
	for (; i != e; i++)
		atomic_init(&q->array[__lfring_map(i, order, n)], n + i);
	for (; i != n; i++)
		atomic_init(&q->array[__lfring_map(i, order, n)], (lfsatomic_t) -1);

	atomic_init(&q->head, s);
	atomic_init(&q->threshold, __lfring_threshold3(half, n));
	atomic_init(&q->tail, e);
}

static inline bool lfring_enqueue(struct lfring * ring, size_t order,
		size_t eidx, bool nonempty)
{
	struct __lfring * q = (struct __lfring *) ring;
	size_t tidx, half = lfring_pow2(order), n = half * 2;
	lfatomic_t tail, entry, ecycle, tcycle;

	eidx ^= (n - 1);

	while (1) {
		tail = atomic_fetch_add_explicit(&q->tail, 1, memory_order_acq_rel);
		tcycle = (tail << 1) | (2 * n - 1);
		tidx = __lfring_map(tail, order, n);
		entry = atomic_load_explicit(&q->array[tidx], memory_order_acquire);
retry:
		ecycle = entry | (2 * n - 1);
		if (__lfring_cmp(ecycle, <, tcycle) && ((entry == ecycle) ||
				((entry == (ecycle ^ n)) &&
				 __lfring_cmp(atomic_load_explicit(&q->head,
				  memory_order_acquire), <=, tail)))) {

			if (!atomic_compare_exchange_weak_explicit(&q->array[tidx],
					&entry, tcycle ^ eidx,
					memory_order_acq_rel, memory_order_acquire))
				goto retry;

			if (!nonempty && (atomic_load(&q->threshold) != __lfring_threshold3(half, n)))
				atomic_store(&q->threshold, __lfring_threshold3(half, n));
			return true;
		}
	}
}

static inline void __lfring_catchup(struct lfring * ring,
	lfatomic_t tail, lfatomic_t head)
{
	struct __lfring * q = (struct __lfring *) ring;

	while (!atomic_compare_exchange_weak_explicit(&q->tail, &tail, head,
			memory_order_acq_rel, memory_order_acquire)) {
		head = atomic_load_explicit(&q->head, memory_order_acquire);
		tail = atomic_load_explicit(&q->tail, memory_order_acquire);
		if (__lfring_cmp(tail, >=, head))
			break;
	}
}

static inline size_t lfring_dequeue(struct lfring * ring, size_t order,
		bool nonempty)
{
	struct __lfring * q = (struct __lfring *) ring;
	size_t hidx, n = lfring_pow2(order + 1);
	lfatomic_t head, entry, entry_new, ecycle, hcycle, tail;

	if (!nonempty && atomic_load_explicit(&q->threshold, memory_order_acquire) < 0) {
		return LFRING_EMPTY;
	}

	while (1) {
		head = atomic_fetch_add_explicit(&q->head, 1, memory_order_acq_rel);
		hcycle = (head << 1) | (2 * n - 1);
		hidx = __lfring_map(head, order, n);
		entry = atomic_load_explicit(&q->array[hidx], memory_order_acquire);

		do {
			ecycle = entry | (2 * n - 1);
			if (ecycle == hcycle) {
				atomic_fetch_or_explicit(&q->array[hidx], (n - 1),
						memory_order_acq_rel);
				return (size_t) (entry & (n - 1));
			}

			if ((entry | n) != ecycle) {
				entry_new = entry & ~(lfatomic_t) n;
				if (entry == entry_new)
					break;
			} else {
				entry_new = hcycle ^ ((~entry) & n);
			}
		} while (__lfring_cmp(ecycle, <, hcycle) &&
					!atomic_compare_exchange_weak_explicit(&q->array[hidx],
					&entry, entry_new,
					memory_order_acq_rel, memory_order_acquire));

		if (!nonempty) {
			tail = atomic_load_explicit(&q->tail, memory_order_acquire);
			if (__lfring_cmp(tail, <=, head + 1)) {
				__lfring_catchup(ring, tail, head + 1);
				atomic_fetch_sub_explicit(&q->threshold, 1,
					memory_order_acq_rel);
				return LFRING_EMPTY;
			}

			if (atomic_fetch_sub_explicit(&q->threshold, 1,
					memory_order_acq_rel) <= 0)
				return LFRING_EMPTY;
		}
	}
}

#endif	/* !__LFRING_H */

/* vi: set tabstop=4: */
