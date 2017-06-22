//Copyright 2009 Princeton University
//Written by Christian Bienia

/* This file contains methods and data structures to:
 *  - Allocate and manage memory buffers
 *  - Keep track of the usage of memory buffers and free them if necessary
 *  - Methods to break memory buffers into smaller buffers
 * The purpose of all this is to eliminate memory copies by reusing allocated memory as much as possible
 *
 * Note on use in multithreaded programs:
 * The routines expect that the caller takes care of guaranteeing exclusive access to the arguments
 * passed to the routines. Access to meta data that might be shared between multiple arguments (and
 * hence threads) will be synchronized. Obviously the user is responsible for synchronizing access
 * to the contents of the buffers.
 *
 * Note on allocating and freeing:
 * Two memory areas need to be distinguished: The mbuffer_t structure and the buffer that is encapsulated.
 * The subsystem can be used with both statically and dynamically allocated mbuffer_t structures. It will
 * always automatically free mbuffer_t structures it has allocated itself. Manually allocated mbuffer_t
 * structuers also need to be freed manually. The memory for the encapsulated buffer is always freed
 * automatically and needs to be dynamically allocated if manual allocation is used.
 */

#ifndef _MBUFFER_H_
#define _MBUFFER_H_

#include <stdlib.h>
#include "defs.h"
#include "pdd_config.h"

/* Defines to ease access of chunk_t struct members */
#ifndef cdata
#define cdata(chunkptr) ((chunkptr)->uncompressed_data.ptr)
#endif  
        
#ifndef csize
#define csize(chunkptr) (chunkptr)->uncompressed_data.n
#endif
            
#ifndef mbuf
#define mbuf(chunkptr) (chunkptr)->uncompressed_data
#endif      

#define SSIZE_MAX_128MB (128*1024*1024)     /* 128 MB for buffers */

//Add additional code to catch unallocated mbuffers and multiple frees
//#define ENABLE_MBUFFER_CHECK

#ifdef ENABLE_MBUFFER_CHECK
//random number to detect properly allocated & initialized mbuffers
#define MBUFFER_CHECK_MAGIC 4363097
#endif

//Definition of a memory buffer
typedef struct {
  __u8 *ptr; //pointer to the buffer
  __u16 n; //size of the buffer in bytes
#ifdef ENABLE_MBUFFER_CHECK
  int check_flag;
#endif
} mbuffer_t;



//Initialize memory buffer subsystem
int mbuffer_system_init();

//Shutdown memory buffer subsystem
int mbuffer_system_destroy();

//Initialize a memory buffer that has been manually or statically allocated
//The mbuffer system will not attempt to free argument *m
MY_EXTERN_C int mbuffer_create(mbuffer_t *m, __u16 size);

//Make a shallow copy of a memory buffer
mbuffer_t *mbuffer_clone(mbuffer_t *m);

//Make a deep copy of a memory buffer
mbuffer_t *mbuffer_copy(mbuffer_t *m);

//Free a memory buffer
MY_EXTERN_C void mbuffer_free(mbuffer_t *m);

//Resize a memory buffer
//Returns 0 if the operation was successful
MY_EXTERN_C int mbuffer_realloc(mbuffer_t *m, __u16 size);

//Split a memory buffer m1 into two buffers m1 and m2 at the designated location
//Returns 0 if the operation was successful
MY_EXTERN_C int mbuffer_split(mbuffer_t *m1, mbuffer_t *m2, __u16 split);

#endif //_MBUFFER_H_



