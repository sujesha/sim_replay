#ifndef _RABINSSS_H_
#define _RABINSSS_H_

#include "defs.h"
#include "mbuffer.h"
#include "pdd_config.h"

/* Define USED macro */
#define USED(x) { ulong y __attribute__ ((unused)); y = (ulong)x; }

#define FINGERPRINT_PT 0xbfe6b8a5bf378d83LL

enum {
	NWINDOW   = 32,
	MinSegment  = MINCHUNKSIZE,
	MaxSegment  = MAXCHUNKSIZE,
	RabinMask = 0x7fff,  // must be less than <= 0x7fff 
    //RabinPrint = 0x008,	//used as default in the very beginning
	RabinPrint = HEXPRINT12bits,	//default is 0x008 in Makefile - 25 Aug,2014
};

//The data type of a chunk, the basic work unit
struct chunk_t
{
  mbuffer_t uncompressed_data;
};

void initRabin(void/*int winlen*/);
void exitRabin(void);
MY_EXTERN_C struct chunk_t* alloc_chunk_t(__u16 size);
MY_EXTERN_C struct chunk_t* free_chunk_t(struct chunk_t **chunk);
MY_EXTERN_C struct chunk_t* chunk_realloc(struct chunk_t *c, __u16 offset);
MY_EXTERN_C void mergeLeftovers(struct chunk_t **chunk, struct chunk_t **leftover,
					unsigned char *buf, __u16 len, __u16 bytes_left);
MY_EXTERN_C int invokeRabin(struct chunk_t *chunk, __u16 *len, __u16 *bytes_left, 
						__u16 *offset, //,uint32_t *ptime) TODO:ptime needed?	
						int initflag);
MY_EXTERN_C void splitChunkBuffer(struct chunk_t **chunk, 
								struct chunk_t** leftover, __u16 offset);

#endif //_RABINSSS_H_


