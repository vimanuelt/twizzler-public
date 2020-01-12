#pragma once

#include <immintrin.h>

#define __CL_SIZE 64

#ifdef __CLWB__
#define __HAVE_CLWB
#endif

static inline void _clwb(const void *p)
{
#ifdef __HAVE_CLWB
	//_mm_clwb(p);
#else
	//_mm_clflushopt(p);
#endif
}

static inline void _clwb_len(const void *p, size_t len)
{
	return;
	char *l = p;
	long long rem = len;
	while(rem > 0) {
		_clwb(l);
		size_t off = (uintptr_t)l & (__CL_SIZE - 1);
		l += (__CL_SIZE - off);
		rem -= (__CL_SIZE - off);
	}
}

static inline void _pfence(void)
{
	// asm volatile("sfence;" ::: "memory");
}
