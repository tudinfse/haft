#include <ctype.h>
#undef isdigit

int my_isdigit(int c)
{
	return (unsigned)c-'0' < 10;
}

