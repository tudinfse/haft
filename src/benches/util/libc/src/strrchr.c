#include <stdlib.h>

size_t my_strlen(const char *s);

void *__my_memrchr(const void *m, int c, size_t n)
{
	const unsigned char *s = m;
	c = (unsigned char)c;
	while (n--) if (s[n]==c) return (void *)(s+n);
	return 0;
}

char *my_strrchr(const char *s, int c)
{
	return __my_memrchr(s, c, my_strlen(s) + 1);
}
