#include <string.h>

size_t my_strcspn(const char *s, const char *c);

char *my_strpbrk(const char *s, const char *b)
{
	s += my_strcspn(s, b);
	return *s ? (char *)s : 0;
}
