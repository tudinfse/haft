#include <strings.h>
#include <ctype.h>

int my_tolower(int c);

int my_strncasecmp(const char *_l, const char *_r, size_t n)
{
	const unsigned char *l=(void *)_l, *r=(void *)_r;
	if (!n--) return 0;
	for (; *l && *r && n && (*l == *r || my_tolower(*l) == my_tolower(*r)); l++, r++, n--);
	return my_tolower(*l) - my_tolower(*r);
}


