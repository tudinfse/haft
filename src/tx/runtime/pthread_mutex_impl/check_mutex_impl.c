/* 
 * This test is to check that the pthread_mutex_t definition on your system
 * is as assumed by the Transactifier run-time.
 * The run-time assumes that the very first integer is a lock variable, so
 *   if ((int*) &m) != 0 -- then some thread acquired a lock
 *   if ((int*) &m) == 0 -- then lock is free
 *
 * If the program outputs no difference after lock() and unlock(),
 * your implementation of pthread mutexes has different structure,
 * and pthread lock/unlock wrappers in ../tx_intel.c must be changed.
 */

#include <stdio.h>
#include <stdlib.h>

#include <pthread.h>
#include "reference_pthread_mutex.h"

pthread_t threads[2];
pthread_mutex_t m;
int x = 0;

void *func(void *arg) {
  reference_pthread_mutex_t *rm = (reference_pthread_mutex_t *) &m;
  int *mint = (int *) &m;

  flockfile(stdout);

  pthread_mutex_lock(&m);
  printf("[after lock] reference_pthread_mutex_t.__data.__lock = %d\n", rm->__data.__lock);
  printf("[after lock] first integer in int array              = %d\n", mint[0]);

  x++;

  pthread_mutex_unlock(&m);
  printf("[after unlock] reference_pthread_mutex_t.__data.__lock = %d\n", rm->__data.__lock);
  printf("[after unlock] first integer in int array              = %d\n", mint[0]);

  funlockfile(stdout);
  return NULL;
}

int main (int argc, char *argv[]) {
  reference_pthread_mutex_t *rm = (reference_pthread_mutex_t *) &m;
  int *mint = (int *) &m;

  printf("sizeof(pthread_mutex_t) = %lu\n", sizeof(pthread_mutex_t));

  pthread_mutex_init(&m, NULL);
  printf("[after init] reference_pthread_mutex_t.__data.__lock = %d\n", rm->__data.__lock);
  printf("[after init] first integer in int array              = %d\n", mint[0]);

  int i;
  for(i = 0; i < 2; i++) {
	pthread_create(&threads[i], NULL, func, NULL);
	}

  for(i = 0; i < 2; i++) {
    pthread_join(threads[i], NULL);
  }

  printf("x = %d\n", x);

  return 0;
}
