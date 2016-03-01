#include <math.h>

double my_scalbn(double x, int n);

double my_ldexp(double x, int n)
{
	return my_scalbn(x, n);
}
