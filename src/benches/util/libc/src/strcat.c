#include <stdlib.h>

char *my_strcpy(char *restrict dest, const char *restrict src);
size_t my_strlen(const char *s);

char *my_strcat(char *restrict dest, const char *restrict src)
{
	my_strcpy(dest + my_strlen(dest), src);
	return dest;
}
