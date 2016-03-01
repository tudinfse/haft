#include <ctype.h>

int my_isupper(int c);

int my_tolower(int c)
{
	if (my_isupper(c)) return c | 32;
	return c;
}

