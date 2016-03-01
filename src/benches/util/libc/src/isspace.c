#include <ctype.h>
#undef isspace

int my_isspace(int c)
{
	return c == ' ' || (unsigned)c-'\t' < 5;
}

