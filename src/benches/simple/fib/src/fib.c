#include <stdio.h>

int main(int argc, char **argv) {

	long j;
	long f1 = 1, f2 = 1;

	for (j = 3 ; j <= 1000000000 ; ++ j) { 
		long t;

		t = f1;
		f1 = f2;
		f2 = t + f2;
	}
	printf("%ld %ld\n", j, f2);
	return 0;
}
