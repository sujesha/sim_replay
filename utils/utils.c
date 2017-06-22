/* Handles all chunk-to-virt and virt-to-chunk mappings for PROVIDED
 * Was pcollect_mapping.c previously
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <asm/types.h>
#include <assert.h>
#include "md5.h"
#include "sha.h"
#include "utils.h"			
#include "debug.h"			/* EXIT_TRACE */
#include "pdd_config.h"
#include "unused.h"

const char zeroarray[65537] = { 0 };
const char zerohash[HASHLEN_STR-1] = { 0 };

void copycontent(__u8 **d, __u8 *s, unsigned int nbytes)
{
    *d = malloc(nbytes * sizeof(__u8));    //free in bunch_output_pkts()
    if (*d == NULL)
        fprintf(stderr, "malloc for d failed\n");
    assert(*d != NULL);
    memcpy(*d, s, nbytes);
}

unsigned char* alloc_mem(__u16 len)
{
    unsigned char *mem;
    mem = calloc(len, sizeof(unsigned char));
    if(mem == NULL)
    {
        VOID_ERR("Couldn't allocate memory!\n");
    }
    return mem;
}

void free_mem(unsigned char* mem)
{
    if (mem != NULL)
        free(mem);
    mem = NULL; 
}        


__u16 inc_blkoffset(__u16 val)
{
	if (val == BLKSIZE - 1)
		val = 0;
	else
		val++;
	return val;
}

__u16 dec_blkoffset(__u16 val)
{
	if (val == 0)
		return(BLKSIZE - 1);
	else
		return (val-1);
}

void markMagicSample(unsigned char *buf, int len, unsigned char *mag)
{
	UNUSED(buf);
	UNUSED(len);
	UNUSED(mag);
    return;
}

#ifndef RABIN_PERF_PROFILING
#ifndef FIX_INCONSISTENT
/** Computes hashkey for hashtab */
/* If ENABLE_HASHMAGIC, magicSample is appended to form hashkey for hashtab
 * @param[in] buf
 * @param[out] hashkey that is generated
 * @return status
 */
int getHashKey(unsigned char *buf, __u32 len, unsigned char key[])
{
	unsigned char *dig = NULL;			/* digest */
#if ENABLE_HASHMAGIC
	unsigned char *mag = NULL;
#endif

#ifdef DEBUG_SS
    WHERE;
#endif

    dig = alloc_mem(HASHLEN);
	if (dig == NULL)
	{
		RET_ERR("alloc_mem for dig failed\n");
	}
    getHash(buf, len, dig);
#ifdef DEBUG_SS
    WHERE;
	printk(KERN_DEBUG "getHash() done, hash = %s\n", dig);
#endif
    memcpy(key, dig, HASHLEN);
    free_mem(dig);

#ifdef DEBUG_SS
    WHERE;
	printk(KERN_DEBUG "free_mem(dig) done.\n");
#endif
#if ENABLE_HASHMAGIC
#if defined(PROCHUNKING_DEDUP_DEBUG_SS)
	printf("why are we here?????\n");	
    mag = alloc_mem(MAGIC_SIZE);
	if (mag == NULL)
	{
		RET_ERR("alloc_mem for mag failed\n");
	}
    markMagicSample(buf, len, mag);
    memcpy(key + HASHLEN, mag, MAGIC_SIZE);
	//key[HASHLEN+MAGIC_SIZE] = '\0';
    free_mem(mag);
#endif
#endif

#ifdef DEBUG_SS
    WHERE;
#endif
    return 0;	
}
#endif
#endif

int isPrime(unsigned int num)
{
    unsigned int i = 0;

    if(num%2==0)
        return 0;
    for(i=3; i<=num/2; i+=2)
    {
        if(num%i==0)
            return 0;
    }
    return 1;
}

