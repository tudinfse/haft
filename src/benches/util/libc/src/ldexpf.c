#include <math.h>

float my_scalbnf(float x, int n);

float my_ldexpf(float x, int n)
{
	return my_scalbnf(x, n);
}
