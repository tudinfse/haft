#include <string.h>

#ifdef PRINTDEBUG
#include <stdio.h>
#endif

int my_strcmp(const char *l, const char *r)
{
#ifdef PRINTDEBUG
    static int printdebugnum = 0;
    printf("[strcmp : %6d]\n", printdebugnum++);
#endif

	for (; *l==*r && *l; l++, r++);
	return *(unsigned char *)l - *(unsigned char *)r;
}
