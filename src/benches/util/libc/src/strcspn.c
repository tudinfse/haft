#include <string.h>

#define BITOP(a,b,op) \
 ((a)[(size_t)(b)/(8*sizeof *(a))] op (size_t)1<<((size_t)(b)%(8*sizeof *(a))))

char *my_strchrnul(const char *, int);

void *my_memset(void *dest, int c, size_t n);

size_t my_strcspn(const char *s, const char *c)
{
	const char *a = s;
	size_t byteset[32/sizeof(size_t)];

	if (!c[0] || !c[1]) return my_strchrnul(s, *c)-a;

	my_memset(byteset, 0, sizeof byteset);
	for (; *c && BITOP(byteset, *(unsigned char *)c, |=); c++);
	for (; *s && !BITOP(byteset, *(unsigned char *)s, &); s++);
	return s-a;
}
