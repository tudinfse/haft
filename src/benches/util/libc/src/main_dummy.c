#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <ctype.h>

char a[100];
char b[100];
char c[100];

int main() {
  bzero(a, 100);
  a[10] = 0;
  printf("bzero: %s\n", a);

  memset(a, 50, 100);
  a[10] = 0;  
  printf("memset: %s\n", a);

  memcpy(b, a, 100);
  printf("memcpy: %s\n", b);

  memmove(c, a, 100);
  printf("memmove: %s\n", c);

  // set some chars
  a[0] = 'a';
  b[0] = 'b';
  c[0] = 'c';
  a[0] = toupper(a[0]);
  printf("toupper: %s\n", a);

  // null-terminate again for sanity
  a[10] = 0;
  b[10] = 0;
  c[10] = 0;
  int r = strcmp(a, b);
  printf("strcmp: %d\n", r);

  size_t sz = strlen(a);
  printf("strlen: %d\n", sz);

  return 0;
}
