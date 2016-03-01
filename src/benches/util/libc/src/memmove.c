#include <string.h>
#include <stdint.h>

#ifdef PRINTDEBUG
#include <stdio.h>
#endif

#define WT size_t
#define WS (sizeof(WT))

void *my_memcpy(void *dest, const void *src, size_t n);

void *my_memmove(void *dest, const void *src, size_t n)
{
#ifdef PRINTDEBUG
    static int printdebugnum = 0;
    printf("[memmove : %6d]\n", printdebugnum++);
#endif

	char *d = dest;
	const char *s = src;

	if (d==s) return d;
	if (s+n <= d || d+n <= s) return my_memcpy(d, s, n);

	if (d<s) {
		if ((uintptr_t)s % WS == (uintptr_t)d % WS) {
			while ((uintptr_t)d % WS) {
				if (!n--) return dest;
				*d++ = *s++;
			}
			for (; n>=WS; n-=WS, d+=WS, s+=WS) *(WT *)d = *(WT *)s;
		}
		for (; n; n--) *d++ = *s++;
	} else {
		if ((uintptr_t)s % WS == (uintptr_t)d % WS) {
			while ((uintptr_t)(d+n) % WS) {
				if (!n--) return dest;
				d[n] = s[n];
			}
			while (n>=WS) n-=WS, *(WT *)(d+n) = *(WT *)(s+n);
		}
		while (n) n--, d[n] = s[n];
	}

	return dest;
}
