#include <string.h>
#include <stdint.h>

#ifdef PRINTDEBUG
#include <stdio.h>
#endif

void *my_memset(void *dest, int c, size_t n);

void my_bzero(void *s, size_t n)
{
#ifdef PRINTDEBUG
    static int printdebugnum = 0;
    printf("[bzero : %6d]\n", printdebugnum++);
#endif

	my_memset(s, 0, n);
}
