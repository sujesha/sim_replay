//Copyright 2009 Princeton University
//Written by Christian Bienia

#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef ENABLE_PTHREADS
#include <pthread.h>
#endif //ENABLE_PTHREADS

#ifdef ENABLE_DMALLOC
#include <dmalloc.h>
#endif //ENABLE_DMALLOC


#include "mbuffer.h"
#include "debug.h"
#include "pdd_config.h"



#ifdef ENABLE_PTHREADS

//Use spin locks instead of mutexes (this file only)
#define ENABLE_SPIN_LOCKS

#ifdef ENABLE_SPIN_LOCKS
typedef pthread_spinlock_t pthread_lock_t;
#define PTHREAD_LOCK_INIT(l) pthread_spin_init(l, PTHREAD_PROCESS_PRIVATE)
#define PTHREAD_LOCK_DESTROY(l) pthread_spin_destroy(l)
#define PTHREAD_LOCK(l) pthread_spin_lock(l)
#define PTHREAD_UNLOCK(l) pthread_spin_unlock(l)
#else
typedef pthread_mutex_t pthread_lock_t;
#define PTHREAD_LOCK_INIT(l) pthread_mutex_init(l, NULL)
#define PTHREAD_LOCK_DESTROY(l) pthread_mutex_destroy(l)
#define PTHREAD_LOCK(l) pthread_mutex_lock(l)
#define PTHREAD_UNLOCK(l) pthread_mutex_unlock(l)
#endif //ENABLE_SPIN_LOCKS

//Array with global locks to use. We use many mutexes to achieve some concurrency.
//FIXME: Merge into mbuffer mcb
//FIXME: Update documentation with latest changes
//Unfortunately mutexes cannot be stored inside the memory buffers because of
//concurrent free operations
pthread_lock_t *locks = NULL;

//Number of locks to use. More locks means higher potential concurrency, assuming lock usage is reasonably balanced.
//We use a prime number for that value to get a simple but effective hash function
#define NUMBER_OF_LOCKS 1021

//A very simple hash function to map memory addresses to a lock id for the locks array
static inline int lock_hash(void *p) {
  return (int)(((unsigned long long int)p) % NUMBER_OF_LOCKS);
}
#endif //ENABLE_PTHREADS



//Initialize memory buffer subsystem
int mbuffer_system_init() {
#ifdef ENABLE_PTHREADS
  int i;

  assert(locks==NULL);
  locks = malloc(NUMBER_OF_LOCKS * sizeof(pthread_lock_t));
  if(locks==NULL) return -1;
  for(i=0; i<NUMBER_OF_LOCKS; i++) {
    if(PTHREAD_LOCK_INIT(&locks[i]) != 0) {
      int j;
      for(j=0; j<i; j++) {
        PTHREAD_LOCK_DESTROY(&locks[i]);
      }
      free((void *)locks);
      locks=NULL;
      return -1;
    }
  }
#endif
  return 0;
}

//Shutdown memory buffer subsystem
int mbuffer_system_destroy() {
#ifdef ENABLE_PTHREADS
  int i, rv;
  rv=0;
  for(i=0; i<NUMBER_OF_LOCKS; i++) {
    rv+=PTHREAD_LOCK_DESTROY(&locks[i]);
  }
  free((void *)locks);
  locks=NULL;
  return rv ? -1 : 0;
#else
  return 0;
#endif
}

//Initialize a memory buffer
MY_EXTERN_C int mbuffer_create(mbuffer_t *m, __u16 size) {
  uint8_t *ptr;

  assert(m!=NULL);
  assert(size > 0);

  //FIXME: Merge both mallocs to one
  ptr = (uint8_t *)malloc(size+1);
  if(ptr==NULL) 
	{
		RET_ERR("malloc failed in mbuffer_create\n");
	}
//	*(ptr+size) = '\0';

  m->ptr = (uint8_t *)ptr;
  m->n = size;
#ifdef ENABLE_MBUFFER_CHECK
  m->check_flag=MBUFFER_CHECK_MAGIC;
#endif

  return 0;
}

//Make a shallow copy of a memory buffer
mbuffer_t *mbuffer_clone(mbuffer_t *m) {
  mbuffer_t *temp;

  assert(m!=NULL);
#ifdef ENABLE_MBUFFER_CHECK
  assert(m->check_flag==MBUFFER_CHECK_MAGIC);
#endif

  temp = (mbuffer_t *)malloc(sizeof(mbuffer_t));
  if(temp==NULL) return NULL;

  //copy state, use joint mcb
  temp->ptr = m->ptr;
  temp->n = m->n;
#ifdef ENABLE_MBUFFER_CHECK
  temp->check_flag=MBUFFER_CHECK_MAGIC;
#endif

  return temp;
}

//Make a deep copy of a memory buffer
mbuffer_t *mbuffer_copy(mbuffer_t *m) {
  mbuffer_t *temp;

  assert(m!=NULL);
  assert(m->n >= 1);
#ifdef ENABLE_MBUFFER_CHECK
  assert(m->check_flag==MBUFFER_CHECK_MAGIC);
#endif

  //NOTE: No need to update reference counter of master, resulting copy of buffer will be independent
  temp = (mbuffer_t *)malloc(sizeof(mbuffer_t));
  if(temp==NULL) return NULL;
  if(mbuffer_create(temp, m->n)!=0) {
    free(temp);
    temp=NULL;
    return NULL;
  }
  memcpy(temp->ptr, m->ptr, m->n);
#ifdef ENABLE_MBUFFER_CHECK
  temp->check_flag=MBUFFER_CHECK_MAGIC;
#endif

  return temp;
}

//Free a memory buffer
MY_EXTERN_C void mbuffer_free(mbuffer_t *m) {
  assert(m!=NULL);
#ifdef ENABLE_MBUFFER_CHECK
  assert(m->check_flag==MBUFFER_CHECK_MAGIC);
#endif

#ifdef ENABLE_MBUFFER_CHECK
    m->check_flag=0;
#endif
	free(m->ptr);
}

//Resize a memory buffer
//Returns 0 if the operation was successful
MY_EXTERN_C int mbuffer_realloc(mbuffer_t *m, __u16 size) 
{
	uint8_t *r;

	assert(m!=NULL);
	assert(size>0);
#ifdef ENABLE_MBUFFER_CHECK
	assert(m->check_flag==MBUFFER_CHECK_MAGIC);
#endif
#ifdef SIMREPLAY_DEBUG_SS_DONE
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	r = (uint8_t *)realloc(m->ptr, size);
//fprintf(stdout, "no eror here either\n");
  	if(r != NULL) 
	{
    	m->ptr = r;
//		m->ptr[size]='\0';	/*SSS: null character at end */
	    m->n = size;
  	}
	else
  		RET_ERR("Memory allocation failed during realloc.\n");

	return 0;
}

//Split a memory buffer m1 into two buffers m1 and m2 at the designated location
//Returns 0 if the operation was successful
MY_EXTERN_C int mbuffer_split(mbuffer_t *m1, mbuffer_t *m2, __u16 split) {
  assert(m1!=NULL);
  assert(m2!=NULL);
  assert(split>0);
  assert(split < m1->n);
#ifdef ENABLE_MBUFFER_CHECK
  assert(m1->check_flag==MBUFFER_CHECK_MAGIC);
  assert(m2->check_flag!=MBUFFER_CHECK_MAGIC);
#endif

  //split buffer
//  m2->ptr = m1->ptr+split;	/* SSS: should not there be a memcpy here? from chunk to temp, or m1 to m2? */
  //memcpy(m1->ptr+split,m2->ptr, m1->n-split);
  memcpy(m2->ptr, m1->ptr+split, m1->n-split);
  m2->n = m1->n-split;
  m1->n = split;		/* Setting n not enough. Need to truncate the ptr string as well... split is not just offset, it is number of bytes */
//	*(m1->ptr + m1->n) = '\0';
//	*(m2->ptr + m2->n) = '\0';

#ifdef ENABLE_MBUFFER_CHECK
  m2->check_flag=MBUFFER_CHECK_MAGIC;
#endif

  return 0;
}


