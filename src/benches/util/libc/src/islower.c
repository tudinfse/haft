#include <ctype.h>
#undef islower

int my_islower(int c)
{
	return (unsigned)c-'a' < 26;
}

