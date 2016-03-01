#include <ctype.h>
#undef isupper

int my_isupper(int c)
{
	return (unsigned)c-'A' < 26;
}

