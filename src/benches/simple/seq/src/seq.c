#include <stdio.h>

__attribute__ ((noinline))
int foo(int x) {
  return x + 2;
}

__attribute__ ((noinline))
int bar(int x) {
  return foo(x) + 1;
}

int main() {
  int r = 0;
  r += foo(r);
  r += 1;
  r += bar(r);
  r += 2;
  return r;
}
