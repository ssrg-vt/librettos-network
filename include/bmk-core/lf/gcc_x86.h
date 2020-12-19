/*-
 * Copyright (c) 2020 Ruslan Nikolaev.  All Rights Reserved.
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

#ifndef __BITS_LF_GCC_X86_H
#define __BITS_LF_GCC_X86_H 1

#include <stdatomic.h>
#include <stdbool.h>

#define LFATOMIC(x)				_Atomic(x)
#define LFATOMIC_VAR_INIT(x)	ATOMIC_VAR_INIT(x)

typedef struct lfatomic_aba {
	_Alignas(sizeof(long) * 2) unsigned long stamp;
	void *value;
} lfatomic_aba_t;

struct __lfatomic_pair {
	_Alignas(sizeof(long) * 2) _Atomic(unsigned long) stamp;
	_Atomic(void *) value;
};

static inline void __lfaba_init(_Atomic(lfatomic_aba_t) * obj,
		lfatomic_aba_t val)
{
	*((volatile lfatomic_aba_t *) ((unsigned long) obj)) = val;
}

/* A stamp must always go first for split loads. */
static inline lfatomic_aba_t __lfaba_load(_Atomic(lfatomic_aba_t) * obj,
		memory_order order)
{
	volatile struct __lfatomic_pair *pair;
	lfatomic_aba_t res;
	pair = (struct __lfatomic_pair *) ((unsigned long) obj);
	res.stamp = pair->stamp;
	res.value = pair->value;
	return res;
}

static inline bool  __lfaba_reload(_Atomic(lfatomic_aba_t) * obj,
		lfatomic_aba_t *value, memory_order order)
{
	volatile struct __lfatomic_pair *pair;
	lfatomic_aba_t res;
	pair = (struct __lfatomic_pair *) ((unsigned long) obj);
	res.stamp = pair->stamp;
	if (res.stamp == value->stamp)
		return true;
	res.value = pair->value;
	*value = res;
	return false;
}

static inline void *__lfaba_load_value(_Atomic(lfatomic_aba_t) * obj,
		memory_order order)
{
	return ((volatile lfatomic_aba_t *) ((unsigned long) obj))->value;
}

static inline void __lfaba_store_value(_Atomic(lfatomic_aba_t) * obj,
		void *value, memory_order order)
{
	struct __lfatomic_pair *pair;
	pair = (struct __lfatomic_pair *) ((unsigned long) obj);
	/* Update the stamp first. */
	atomic_fetch_add(&pair->stamp, 1);
	/* Only then store the value. */
	atomic_store_explicit(&pair->value, value, order);
}

static inline bool __lfaba_cmpxchg_strong(_Atomic(lfatomic_aba_t) * obj,
		lfatomic_aba_t * expected, lfatomic_aba_t desired,
		memory_order succ, memory_order fail)
{
	bool result;
#if defined(__x86_64__)
# define __LFX86_CMPXCHG "cmpxchg16b"
#elif defined(__i386__)
# define __LFX86_CMPXCHG "cmpxchg8b"
#endif
	__asm__ __volatile__ ("lock " __LFX86_CMPXCHG " %0"
						  : "+m" (*obj), "=@ccz" (result), "+A" (*expected)
						  : "b" (desired.stamp), "c" (desired.value)
	);
#undef __LFX86_CMPXCHG
	return result;
}

static inline bool __lfaba_cmpxchg_weak(_Atomic(lfatomic_aba_t) * obj,
		lfatomic_aba_t * expected, lfatomic_aba_t desired,
		memory_order succ, memory_order fail)
{
	return __lfaba_cmpxchg_strong(obj, expected, desired, succ, fail);
}

#endif /* !__BITS_LF_GGC_X86_H */

/* vi: set tabstop=4: */
