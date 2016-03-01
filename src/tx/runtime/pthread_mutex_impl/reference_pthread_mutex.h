/*
 *  Current definition of pthread_mutex_t.h in our Ubuntu 14.04 (15.05):
 *    ldd (Ubuntu EGLIBC 2.19-0ubuntu6.6) 2.19
 *
 * The following definition of pthread_mutex_t is taken from
 *   /usr/include/x86_64-linux-gnu/bits/pthreadtypes.h
 */

/* Data structures for mutex handling.  The structure of the attribute
   type is not exposed on purpose.  */
typedef union
{
  struct __reference_pthread_mutex_s
  {
    int __lock;
    unsigned int __count;
    int __owner;
#ifdef __x86_64__
    unsigned int __nusers;
#endif
    /* KIND must stay at this position in the structure to maintain
       binary compatibility.  */
    int __kind;
#ifdef __x86_64__
    short __spins;
    short __elision;
    __pthread_list_t __list;
# define __PTHREAD_MUTEX_HAVE_PREV      1
# define __PTHREAD_MUTEX_HAVE_ELISION   1
#else
    unsigned int __nusers;
    __extension__ union
    {
      struct
      {
        short __espins;
        short __elision;
# define __spins d.__espins
# define __elision d.__elision
# define __PTHREAD_MUTEX_HAVE_ELISION   2
      } d;
      __pthread_slist_t __list;
    };
#endif
  } __data;
  char __size[__SIZEOF_PTHREAD_MUTEX_T];
  long int __align;
} reference_pthread_mutex_t;
