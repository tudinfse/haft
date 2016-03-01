#include <ctype.h>

#ifdef PRINTDEBUG
#include <stdio.h>
#endif

int my_islower(int c);

int my_toupper(int c)
{
#ifdef PRINTDEBUG
    static int printdebugnum = 0;
    printf("[toupper : %6d]\n", printdebugnum++);
#endif

	if (my_islower(c)) return c & 0x5f;
	return c;
}

