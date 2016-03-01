#include <strings.h>
#include <ctype.h>

int my_tolower(int c);

int my_strcasecmp(const char *_l, const char *_r)
{
	const unsigned char *l=(void *)_l, *r=(void *)_r;
	for (; *l && *r && (*l == *r || my_tolower(*l) == my_tolower(*r)); l++, r++);
	return my_tolower(*l) - my_tolower(*r);
}

