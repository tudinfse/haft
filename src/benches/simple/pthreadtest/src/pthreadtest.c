#include <stdio.h>
#include <pthread.h>

pthread_mutex_t globalmutex;
int globalint = 0;

void pthreadtest1() {
  pthread_mutex_lock(&globalmutex);
  globalint++;
  pthread_mutex_unlock(&globalmutex);
}

void pthreadtest2() {
  globalint++;  
  pthread_mutex_lock(&globalmutex);
  globalint++;
  pthread_mutex_unlock(&globalmutex);
  globalint++;  
}

void pthreadtest3(int flag) {
  pthread_mutex_lock(&globalmutex);
  if (flag) {
    globalint++;
    pthread_mutex_unlock(&globalmutex);
  } else {
    globalint--;
    pthread_mutex_unlock(&globalmutex);
  }
  globalint++;
}

void pthreadtest4(int flag) {
  pthread_mutex_lock(&globalmutex);
  if (flag) globalint++;
  else      return; // bad code, but we want it
  pthread_mutex_unlock(&globalmutex);
}

int main(int argc, char **argv) {
  pthread_mutex_init(&globalmutex, NULL);

  pthreadtest1();
  pthreadtest2();
  pthreadtest3(123);
  pthreadtest4(0);

  printf("%d\n", globalint);
  return 0;
}
