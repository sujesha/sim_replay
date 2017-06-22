#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "defs.h"
#include "mbuffer.h"
#include "rabin.h"
#include "debug.h"

uint32_t * rabintab;
uint32_t * rabinwintab;

//static uint32_t irrpoly = 0x759ddb9f;   //SSS: has 22 ones
//static uint32_t irrpoly = 0x45c2b6a1;   //SSS: has 13 ones

/* 0x4d96487b has 16 ones - from http://www.xmailserver.org/rabin_impl.pdf */
//#define DEFAULT_IRRPOLY 0x4d96487b

//from http://code.google.com/p/google-concurrency-library/source/browse/samples/dedup/rabin.c
//static u32int irrpoly = 0x45c2b6a1;
//#define DEFAULT_IRRPOLY 0x45c2b6a1	//5465355937 is NOT prime

#define DEFAULT_IRRPOLY 0x4D964885  //5596661893 is prime!

uint32_t irrpoly; 

int chunkingInitDone = 0;

// = x * t^k mod P(t), k=32, x ranges [0-255]
static uint32_t fpreduce(uint32_t x, uint32_t irr) 
{
	int i;

	//SSS: for each number from 32 to 1, but why from 32....?
	//Is it like t^32 + t^31 + t^30 and so on... 
	//rabin_impl.pdf states that a*t^32 mod irr == drop MSB from irr
	for(i=32; i!=0; i--)
    {
		//rabin_impl.pdf says: drop leading bit of irr conditioned on r1
    	if(x >> 31)
        {   //SSS: if MSB is 1
	 		x <<= 1;  //SSS: dropping the leading bit from x
			x ^= irr; //SSS: x XOR irr (for every non-zero term in polynomial)
    	}
        else    //SSS: if MSB is 0
        {
			x <<= 1;  //SSS: dropping the leading bit from x
		}
	}
  	return x;
}

static void fpmkredtab(uint32_t irr, int s) 
{
	uint32_t i;

	for(i=0; i<256; i++)
    {
    	rabintab[i] = fpreduce(i<<s, irr);
    }
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	return;
}

static uint32_t fpwinreduce(uint32_t irr, 
						/* int winlen,  */
						uint32_t x) 
{
	int i;
	uint32_t winval;

	// Assumption: The window only shifts by one byte.
	// window size = NWINDOW * 8 bits
	// Need to calculate: x * t^(size-8) mod P(t)

	int power = 8*(NWINDOW-1);

	int mul32 = power & ~0x1F;    // = (power/32) * 32
	int shift = power - mul32;

#ifdef DEBUG_SS
   	/* assert(winlen>0); */
	assert(NWINDOW>4);
#endif

	winval = x & 0xFF;
	winval <<= shift;

	for (i=mul32; i>0; i-=32) {
    	winval = fpreduce(winval, irr);   // = winval * t^k mod P(t), k=32
  	}

	return winval;
}

static void fpmkwinredtab(uint32_t irr /* , int winlen, */)
{
	uint32_t i;

	for(i=0; i<256; i++)
    {
          rabinwintab[i] = fpwinreduce(irr, /* winlen, */ i);
    }
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	return;
}

void initRabin(void/*int winlen*/)
{
	if (chunkingInitDone == 1)
	{
		printf("Init already done, no need to redo\n");
		return;
	}
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
	assert(!mbuffer_system_init());
#endif

	irrpoly = DEFAULT_IRRPOLY; 

	rabintab = (uint32_t *)malloc(256*sizeof(uint32_t));
	rabinwintab = (uint32_t *)malloc(256*sizeof(uint32_t));
	if(rabintab == NULL || rabinwintab == NULL) 
	{
    	VOID_ERR("Memory allocation failed.\n");
  	}

 	fpmkredtab(irrpoly, 0);
	fpmkwinredtab(irrpoly /*,winlen, */);

	chunkingInitDone = 1;
	return;
}

void exitRabin(void)
{
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(!mbuffer_system_destroy());
#endif
	if (rabintab != NULL)
       	free(rabintab);
	if (rabinwintab != NULL)
       	free(rabinwintab);
}

static uint32_t buildInitialPrint(mbuffer_t *m, __u16 *iter, __u16 n)
{
	uint32_t h=0, x=0;
	__u16 i;
	__u16 minlimit = 0;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#ifdef DEBUG_SS
	assert(m != NULL && iter != NULL);
#endif

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif

	if (n < NWINDOW)
		minlimit = n;
	else
		minlimit = NWINDOW;

	for(i=0; i<minlimit; i++)
	{
		x = h >> 24;
		h = (h<<8)|m->ptr[i];
		h ^= rabintab[x];	//rabintab drops leading bit of irr conditioned on r1
	}

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	*iter = i;

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	return h;
}

/* resumePrintBuilding: Resume print building from where it was left off
 * 			last time. The new muffer for building print is present in
 * 			m with size n, and the last time computed h is in lasth
 * 			whereas the i count from last time is in lasti
 * @m[in]: Input buffer to be Rabin finger printed
 * @n[in]: Size of the input buffer
 * @lasth[in]: h that was computed in last invocation
 * @lasti[in]: i that was iterated to in last invocation
 * @finalh[out]: h that has been computed at end of current invocation
 */
static __u16 resumePrintBuilding(mbuffer_t *m, __u16 n, /*, int winlen*/
						uint32_t lasth, __u16 lasti, uint32_t *finalh)
{
	__u16 i = lasti;
	uint32_t h = lasth;
	uint32_t x=0;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	while(i<n)
	{
		if (i >= NWINDOW)
		{
			x = m->ptr[i-NWINDOW];	/* the byte to be discarded from window */
			h ^= rabinwintab[x];	/* value to be removed from fingerprint */
		}
		x = h >> 24;	/* note the left-most byte of h into x for later lookup */
		h <<= 8;    /* discard left-most byte of h */
	    h |= m->ptr[i]; /* and add new databyte as right-most byte of h */
    	h ^= rabintab[x];	/* rabintab should have values indexed 0 to 255 */
		i++;

		/* found chunk boundary, i has been incremented in loop */
		if ((i >= MaxSegment) ||
			(i >= MinSegment && ((h & RabinMask) == RabinPrint)))
        {
			*finalh = h;
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
			return i;
		}
	}
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	*finalh = h;
	return (n+1);	//no chunk boundary found yet	
}

/* getFreshRabinOffset: Perform Rabin on given buffer & return offset.
 *		Invoked only when (bytes_left == 0 && len > 0)
 * 
 * @m[in]: Input buffer to be Rabin finger printed
 * @n[in]: Size of the input buffer
 * finalh[out]: output h
 * @return: offset at which chunk boundary encountered
 */
static __u16 getFreshRabinOffset(mbuffer_t *m, __u16 *n, uint32_t *finalh
									/*, uint32_t *ptime, int winlen*/)
{
	__u16 i=0, offset = 0;
	uint32_t h=0;
	uint32_t newh=0;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#ifdef DEBUG_SS
	assert(m != NULL && n != NULL && finalh != NULL);
#endif

	h = buildInitialPrint(m, &i, *n);
	if (*n < NWINDOW)
	{
		/* No more bytes to resume print building and no boundary either */
		*finalh = h;
		return (*n+1);
	}

	offset = resumePrintBuilding(m, *n, h, i, &newh/*, ptime*/);
#ifdef DEBUG_SS
	assert(offset != 0);
#endif
	
	*finalh = newh;

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	return offset;
}

/* Memory alloc for chunk_t struct ptr */
struct chunk_t* alloc_chunk_t(__u16 size)
{
	struct chunk_t *chunk;
    chunk = (struct chunk_t *)malloc(sizeof(struct chunk_t));
    if(chunk==NULL)
    {
        VOID_ERR("Memory allocation failed for chunk.\n");
		return NULL;
    }
    mbuffer_create(&(mbuf(chunk)), size);
	return chunk;
}

/* Returns 0 on success */
struct chunk_t* free_chunk_t(struct chunk_t **chunk)
{
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	assert(*chunk != NULL);
	WHERE;
#endif

	mbuffer_free(&mbuf(*chunk));
   	free(*chunk);
   	*chunk = NULL;

	return NULL;
}

/* mergeLeftovers: appends "buf" data to "leftover" data and 
 * 			outputs a single "chunk".
 *
 * @chunk[out]: output chunk data
 * @leftover[in|out]: input and output leftover data pointer
 * @buf[in]: data to be appended to leftover
 * @len[in]: len of data buf
 * @bytes_left: len of leftover chunk
 * @return: status
 */
void mergeLeftovers(struct chunk_t **chunk, struct chunk_t **leftover,
					unsigned char *buf, __u16 len, __u16 bytes_left)
{
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(*chunk != NULL);
#endif

    if(bytes_left > 0)
    {
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
      	memcpy(cdata(*chunk), cdata(*leftover), bytes_left);
		*leftover = free_chunk_t(leftover);
		*leftover = NULL;
    }
    if (len > 0)
    {
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
        memcpy(cdata(*chunk)+bytes_left, buf, len);
    }  
    *(cdata(*chunk) + bytes_left + len) = '\0';
    csize(*chunk) = bytes_left + len;

#ifdef DEBUG_SS
	assert(*leftover == NULL);
#endif

	/* bytes_left value is to be untouched here */

	return;
}

/* invokeRabin: Implementation of the Rabin chunking algorithm.
 * 		For a brand new data buffer, it uses getFreshRabinOffset() otherwise
 * 		uses resumePrintBuilding() to avoid repetitive effort of finger
 * 		printing. For example, if a block boundary is encountered but
 * 		chunk boundary is not found, then we need to fetch a new block
 * 		and chunk only the new data. Re-chunking of the "old" data
 * 		is not necessary, since it will be repetitive and is guaranteed
 * 		to not yield any chunk boundary. This is a minor optimization,
 * 		and is enabled by remembering the lasth and lasti.
 *
 * @chunk[in]: input data to be chunked
 * @len[in]: length of input data
 * @bytes_left[in|out]: number of bytes left over from last invocation
 * @offset[out]: offset at which a boundary found
 * @initflag[in]: To indicate whether invocation is in scanning phase or I/O
 * @return: split flag  
 */
int invokeRabin(struct chunk_t *chunk, __u16 *len, __u16 *bytes_left, 
						__u16 *foundclen, 
						//,uint32_t *ptime) TODO:ptime needed?	
						int initflag)
{
	/* Since this function could be called by scanning phase 
	 * (initflag==1) and by online phase (initflag==0), so we have
	 * 2 copies of each static variable.
	 */
	static uint32_t h;			/* static, to remember in next call */
	static uint32_t prevh = 0; /* static, to remember in next call */
	static int previ = -1;	/* static, to remember in next call */
	static uint32_t ioh;			/* static, to remember in next call */
	static uint32_t ioprevh = 0; /* static, to remember in next call */
	static int ioprevi = -1;	/* static, to remember in next call */

	/* These are pointers to the static variables above */
	uint32_t *hp;			/* h pointer */
	uint32_t *prevhp;   		/* prevh pointer */
	int *previp; 			/* previ pointer */
#if defined(PROCHUNKING_DEBUG_SSS) || defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (initflag == INIT_STAGE)
	{
		hp = &h;
		prevhp = &prevh;
		previp = &previ;
	}
	else
	{
		hp = &ioh;
		prevhp = &ioprevh;
		previp = &ioprevi;
	}	

	int split = 0;

#ifdef DEBUG_SS
	assert(chunk != NULL);
	if (*bytes_left + *len != csize(chunk))
		fprintf(stderr, "bytes_left=%u, len=%u\n", *bytes_left, *len);
	assert(*bytes_left + *len == csize(chunk));
#endif

    if (*bytes_left == 0 && *len > 0)
    {
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS) || defined(SIMREPLAY_DEBUG_SS_DONE)
	WHERE;
#ifdef NONSPANNING_PROVIDE
		printf("Should reach here at beginning of every block!\n");
#endif
#endif
        /* do chunking afresh */
        *foundclen = getFreshRabinOffset(&(mbuf(chunk)), &(csize(chunk)), hp);
    }
	else if (*bytes_left > 0 && *bytes_left < NWINDOW && *len > 0)
	{
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
    WHERE;
#endif
        /* do chunking afresh 
        *foundclen = getFreshRabinOffset(&(mbuf(chunk)), &(csize(chunk)), hp);
		*/
        /* do chunking of appended data with lasth and lasti */
        *foundclen = resumePrintBuilding(&mbuf(chunk), csize(chunk), 
                                    *prevhp, *previp, hp/*, ptime*/);
    }
    else if (*bytes_left > 0 && *len > 0) /* && *bytes_left >= MinSegment */
    {
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
        /* do chunking of appended data with lasth and lasti */
        *foundclen = resumePrintBuilding(&mbuf(chunk), csize(chunk), 
                                    *prevhp, *previp, hp/*, ptime*/);
    }
	else //(*bytes_left > 0 && *len == 0)
    {
	/* Need not be handled here again, since resumeChunking is already handling? */
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
        /* Zero sector was read, so leftover becomes chunk by itself*/
		/* TODO: What if *bytes_left<MinSegment and zero sector encountered? */
        *foundclen = *bytes_left;
    }

    /* Note prev & previ if and only if only block boundary encountered */
    if (*foundclen > csize(chunk))
    {
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
        *prevhp = *hp;
        *previp = csize(chunk);
    }
    else
    {
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
		*prevhp = 0;
        *previp = -1;
    }

	if (*foundclen < csize(chunk))
	{
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
		split = 1;
		/* Adjusting for next call of invokeRabin() upon split = 1 */
		*bytes_left = 0;
		*len = csize(chunk) - *foundclen;
	}
	else
	{
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
		split = 0;
	}

	return split;
}

/* Retain offset # of bytes in chunk, copy rest to leftover */
void splitChunkBuffer(struct chunk_t **chunk, 
								struct chunk_t** leftover, __u16 offset)
{
	int r;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	assert(offset < csize(*chunk));
	WHERE;
#endif
	*leftover = alloc_chunk_t((csize(*chunk) - offset));
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif

	/* Retain offset # of bytes in chunk, copy rest to leftover */
	r = mbuffer_split(&(mbuf(*chunk)), &(mbuf(*leftover)), offset); 
	if(r!=0) 
	{
		VOID_ERR("Unable to split memory buffer.\n");
    }
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	
	r = mbuffer_realloc(&(mbuf(*chunk)), offset);
	if(r!=0) 
	{
		VOID_ERR("Unable to realloc memory buffer.\n");
    }
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif

#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	assert(*chunk != NULL && *leftover != NULL);
	WHERE;
#endif

	return;
}

struct chunk_t* chunk_realloc(struct chunk_t *c, __u16 offset)
{
	int r;
	r = mbuffer_realloc(&(mbuf(c)), offset);
	if (r)
	{
		VOID_ERR("Unable to realloc memory buffer.\n");
	}
#if defined(DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	WHERE;
#endif
	return c;
}

void freeRabin(void)
{
	free(rabintab);
	free(rabinwintab);
#ifdef DEBUG_SS
	assert(!mbuffer_system_destroy());
#endif
}

