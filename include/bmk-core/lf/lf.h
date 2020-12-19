#ifndef __BITS_LF_H
#define __BITS_LF_H 1

#include <bmk-pcpu/pcpu.h>

/* For the following architectures, it is cheaper to use split (word-atomic)
   loads whenever possible. */
#if defined(__i386__) || defined(__x86_64__) ||	defined (__arm__) ||	\
	defined (__aarch64__)
# define __LFABA_LOAD_SPLIT	1
#else
# define __LFABA_LOAD_SPLIT	0
#endif

#define LFATOMIC_WIDTH	__LONG_WIDTH__

typedef unsigned long lfatomic_t;
typedef long lfsatomic_t;

#define LF_CACHE_SHIFT	BMK_PCPU_L1_SHIFT
#define LF_CACHE_BYTES	BMK_PCPU_L1_SIZE

/* GCC does not have a sane implementation of wide atomics for x86-64
   in recent versions, so use inline assembly workarounds whenever possible.
   No aarch64 support in GCC for right now. */
#if (defined(__i386__) || defined(__x86_64__)) && defined(__GNUC__) &&  \
    !defined(__llvm__) && defined(__GCC_ASM_FLAG_OUTPUTS__)
# include "gcc_x86.h"
#else
# include "c11.h"
#endif

#endif  /* !__BITS_LF_H */

/* vi: set tabstop=4: */
