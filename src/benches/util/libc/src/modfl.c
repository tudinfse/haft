#include <stdint.h>

double my_modf(double x, double *iptr);

long double my_modfl(long double x, long double *iptr)
{
	double d;
	long double r;

	r = my_modf(x, &d);
	*iptr = d;
	return r;
}
