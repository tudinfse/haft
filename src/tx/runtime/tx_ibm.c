#include <htmxlintrin.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MAX_RETRIES
#define MAX_RETRIES 2
#endif

#ifndef THRESHOLD
#define THRESHOLD 500
#endif

// thread-local dynamic counter (implemented as mov %fs:0xfc,%rax)
__thread long __txinstcounter = -1;

__attribute__((always_inline))
void tx_start(void) {
	int nretries = 0;
	while (1) {
		nretries++;
		unsigned status = __builtin_tbegin(1);
		if (status) {
            // successful start
			break; 
		}
		//  abort handler
		if (nretries == MAX_RETRIES) {
			break;
		}
	}
    // no matter how we exit, start counter anew
    __txinstcounter = THRESHOLD;
	return;
}

__attribute__((always_inline))
void tx_end(void) {
	unsigned char state = __builtin_ttest();
	if (_HTM_STATE(state) == _HTM_TRANSACTIONAL) { 
		 __builtin_tend(1);
	}
	return;
}

__attribute__((always_inline))
void tx_cond_start(void) {
  if (__txinstcounter > 0) 
    return;
  tx_end();
  tx_start();
}

__attribute__((always_inline))
void tx_abort(void) {
    // abort code can be any of 0x40 – 0xDF
	__builtin_tabort(0x40);
}

__attribute__((always_inline))
int tx_threshold_exceeded(void) {
  if (__txinstcounter > 0)
    return 0;
  return 1;
}

__attribute__((always_inline))
void tx_increment(unsigned long inc) {
  __txinstcounter -= (long)inc;
}

// pthread lock/unlock wrappers
int tx_pthread_mutex_lock(pthread_mutex_t *m) {
  if (_HTM_STATE(__builtin_ttest()) != _HTM_TRANSACTIONAL || ((int *)m)[0] != 0) {
    // not in Tx or lock is not free, fall back to usual lock
    return pthread_mutex_lock(m);
  }
  // in Tx and lock is free, use optimistic HTM locking
//   __txinstcounter -= 10;
  return 0;
}

int tx_pthread_mutex_unlock(pthread_mutex_t *m) {
  if (_HTM_STATE(__builtin_ttest()) != _HTM_TRANSACTIONAL) {
    // not in Tx, fall back to usual unlock
    return pthread_mutex_unlock(m);
  }
  // in Tx, we know that we used optimistic HTM locking, so nothing to do
  return 0;
}

// dummy vars, so that LLVM does not optimize function declarations away
void (*dummy_tx_start_var)(void) = tx_start;
void (*dummy_tx_cond_start_var)(void) = tx_cond_start;
void (*dummy_tx_end_var)(void)   = tx_end;
void (*dummy_tx_abort_var)(void) = tx_abort;
int  (*dummy_tx_threshold_exceeded_var)(void) = tx_threshold_exceeded;
void (*dummy_tx_increment_var)(unsigned long) = tx_increment;
int  (*dummy_tx_pthread_mutex_lock)(pthread_mutex_t *) = tx_pthread_mutex_lock;
int  (*dummy_tx_pthread_mutex_unlock)(pthread_mutex_t *) = tx_pthread_mutex_unlock;

#ifdef __cplusplus
}
#endif
