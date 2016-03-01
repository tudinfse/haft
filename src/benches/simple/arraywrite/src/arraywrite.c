#include <stdio.h>
#include <stdlib.h>

#define MAXSIZE 1000*1000

char arr[MAXSIZE];

__attribute__ ((noinline))
void arraywrite(int size) {
  int i = size-1;
  for (; i >= 0; i--) {
    arr[i] = i;
  }
}

int main(int argc, char** argv) {
  if (argc != 2) {
    printf("usage: %s char_array_size\n", argv[0]);
    return 1;
  }
  int size = atoi(argv[1]);

  arraywrite(size);

  printf("arr[0]: %hhd \n", arr[0]);
  return 0;
}
