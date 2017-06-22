
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "debugg.h"
#include "debug.h"
#include "serveio-utils.h"
#include "sectorcache.h"
#include "pdd_config.h"
#include "lru_cache.h"
#include "md5.h"
#include "unused.h"
#include "replay-defines.h"
#include <time.h>
#include "simcache-file-API.h"

extern __u64 compulsory_misses;
extern __u64 capacity_misses;
extern unsigned char fmaphit_flag;
extern unsigned char fmapdirty_flag;
extern unsigned char vmaphit_flag;
extern unsigned char vmapdirty_flag;

/* Extern'ed from replay-plugins.c */
extern int freplayflag;
extern int preplayflag;
extern int sreplayflag;    /* default */
extern int ioreplayflag;

/* Extern'ed from sim-replay-generic.c */
extern int cachesimflag;
extern int collectformat;

/* Extern'ed from sync-disk-interface.c */
extern int disksimflag;

/* Extern'ed from lru_cache.c */
extern __u32 MAX_CACHE_COUNT;

/* Extern'ed from contentcache.c */
extern int CCACHEsize_MB;

extern int warmupflag;
extern int write_enabled;       // Boolean: Enable writing
extern int read_enabled;       // Boolean: Enable reading

/* Globals */
int RAMsize_MB = 1024;		/* Default RAM size is 1GB */
//float RAMsize_MB = 0.05;
__u64 bcache_hits = 0;
__u64 bcache_misses = 0;
__u64 bcache_hits_r = 0;
__u64 bcache_misses_r = 0;
__u64 bcache_hits_w = 0;
__u64 bcache_misses_w = 0;

extern FILE * ftimeptr;
inline __u64 gettime(void);

void sectorcache_init()
{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	/* Number of entries that can be held in a given cache depends upon 
	 * the cache size and blksize.
	 */
	if (preplayflag || sreplayflag || freplayflag)
		MAX_CACHE_COUNT = RAMsize_MB * 1024 * 1024 / BLKSIZE;
	//MAX_CACHE_COUNT = 1;

	/* In our simulation, IODEDUP can use part of the RAM as a content-based 
	 * cache (CCACHE), so the MAX_CACHE_COUNT will be reduced accordingly.
	 */
	if (ioreplayflag)
		MAX_CACHE_COUNT = (RAMsize_MB - CCACHEsize_MB) * 1024 * 1024 / BLKSIZE;
		//MAX_CACHE_COUNT = (__u32)((RAMsize_MB * 1024 * 1024 / BLKSIZE) + 1);

#if defined(SIMREPLAY_DEBUG_SS)
	printf("MAX_CACHE_COUNT = %u\n", MAX_CACHE_COUNT);
#endif
}

void sectorcache_add(__u32 ioblkID, char *content, int len, int updatehits,
		__u32 newleader_ioblkID, int *bcachefoundout)
{
	char key[25], newleaderkey[25];

	if (updatehits && (freplayflag || preplayflag))	/* Write request */
		sprintf(newleaderkey, "%u", newleader_ioblkID);
	else
		strcpy(newleaderkey, "");

	/* Content in sector-cache should be BLKSIZE long, but for sake
	 * of simulation by traces, we may just use the human representation of
	 * MD5 hashes as content themselves.
	 */
	sprintf(key, "%u", ioblkID);
	add_to_cache(key, content, len, updatehits, newleaderkey, bcachefoundout); 
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	if (strstr(key, "1050479") || strstr(key, "1095667"))
		printf("In %s, added to cache for key = %s\n", 
				__FUNCTION__, key);
#endif

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	if (strcmp(key, "558350")==0)
    printf("Buffer add: key=%s, buf=%s\n", key, (char*)content);
#endif
}

int sectorcache_lookup(__u32 ioblkID, int len, struct preq_spec *preq)
{
	char key[25];
	char *content = NULL;
	char *dummystring = NULL;
	UNUSED(len);

	sprintf(key, "%u", ioblkID);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	print_lrucache_stat();
#endif
	if (!collectformat && disksimflag)
		dummystring = find_in_cache(key);
	else
	{
		content = find_in_cache(key);
		UNUSED(dummystring);
	}

	/* If preadwritedump traces & disk-simulated, now we use file
	 * for storing cache content physically to save memory usage.
	 */
	if (!collectformat && disksimflag)
	{
		char skey[20];
		if (dummystring)	/* found in sectorcache */
		{
			//printf("dummystring=%s\n", dummystring);
			assert(strcmp(dummystring, "cf")==0);
			strcpy(skey, preq->blkidkey);

			content = malloc(BLKSIZE);
			if (content == NULL)
				VOID_ERR("malloc failed\n");
			cachefile_fetch((unsigned char*)content, preq->blkidkey, BLKSIZE);
		}
		else
		{
			content = NULL;
		}
	}

	if (content)
	{
		if (!disksimflag)	/* Found content when disk not simulated */
		{
			assert(0);	/* not expected */
			if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
					DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
					DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
			{
				preq->content = malloc(MD5HASHLEN_STR-1);
				memcpy(preq->content, content, MD5HASHLEN_STR-1);
				//preq->content[HASHLEN_STR-1] = '\0';	no null char
			}
			else
			{
				preq->content = malloc(BLKSIZE);
				if (preq->content == NULL)
					VOID_ERR("malloc failed\n");
				memcpy(preq->content, content, BLKSIZE);
			}
		}
		else if (preq->rw)	/* Disk is simulated and this is read request */
		{
	        assert(preq->content != NULL);

#ifndef INCONSISTENT_TRACES
    	    /* If all reads & writes enabled, then these asserts should work,
        	 * but if only reads are enabled, it is possible that a 2nd read
	         * request reads (new) content that was written just before
    	     * and hence is not present in cache.
			 *
			 * Note that the trace itself may have some records missing, 
			 * because of which this assert may fail. This was tested
			 * and found to be true. So in the actual replay run, dont rely on
			 * these asserts. Instead if content is found in cache, then
			 * discard the content in the trace, & use the found one instead!
			 * Therefore, these are commented by #define INCONSISTENT_TRACES!
        	 */
			if ((DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
					DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY)
					&& read_enabled && write_enabled)
			{
				if (memcmp(preq->content, content, MD5HASHLEN_STR-1)!=0)
					printf("%s: assert fail for ioblkID=%u\n",__FUNCTION__,
						 ioblkID);
    	        assert(memcmp(preq->content, content, MD5HASHLEN_STR-1)==0);
			}
        	else if (read_enabled && write_enabled)
            	assert(memcmp(preq->content, content, BLKSIZE)==0);
#else
			if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
					DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
				memcpy(preq->content, content, MD5HASHLEN_STR-1);
			else
				memcpy(preq->content, content, BLKSIZE);
#endif
		}
		if (!collectformat && disksimflag)
		{
			/* content was newly allocated, and not just a pointer into
			 * the cache, if we are here. So free it.
			 */
			free(content);
		}
		return 1;
	}
	else
	{
		if (!disksimflag)
		{
			assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
			preq->content = NULL;
		}
		return 0;
	}
}

/* Look-up for each blk (for given request) into base-cache.
 * If the lookup succeeds, the "done" should be set for those blks
 * Used only for reads (dont use for writes, since cache hits are handled in 
 * overwrite_in_sectorcache -> add_to_cache for writes)
 */
int find_in_sectorcache(struct preq_spec **preql, int vop_iter)
{
    int i=vop_iter;
	int len;
	__u32 ioblkID;
	unsigned long long stime=0, etime=0;

#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(vop_iter >= 0);
    assert(preql != NULL);
    assert(*preql != NULL);
#endif 
	assert(cachesimflag);
	assert((*preql+(i))->rw);

	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
		len = MD5HASHLEN_STR-1; /* Len of hex hash */
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY 
			|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
		len = BLKSIZE;
	}

	/* Handling single block (@vop_iter) at a time */

		ioblkID = (*preql+(i))->ioblk;
		stime = gettime();	/* START sector-lookup time */
		if (sectorcache_lookup(ioblkID, len, *preql+(i)))
		{
			etime = gettime();	/* END sector-lookup successtime */
			ACCESSTIME_PRINT("sector-lookup-success time: %llu %d\n",
					 etime - stime, vop_iter);
			if (!warmupflag) {	//dont count stats in warmup
				bcache_hits++;
				bcache_hits_r++;
			}
        	(*preql+(i))->done = 1;
        	(*preql+(i))->bcachefound = 1;
		}
		else
		{
			etime = gettime();	/* END sector-lookup failtime */
			ACCESSTIME_PRINT("sector-lookup-fail time: %llu %d\n",
					 etime - stime, vop_iter);
			if (!warmupflag) {	//dont count stats in warmup
				bcache_misses++;
				bcache_misses_r++;
				if (freplayflag && !fmaphit_flag && !fmapdirty_flag)
					compulsory_misses++;
				else if (freplayflag && (fmapdirty_flag || fmaphit_flag))
					capacity_misses++;
				else if (preplayflag && !vmaphit_flag && !vmapdirty_flag)
					compulsory_misses++;
				else if (preplayflag && (vmapdirty_flag || vmaphit_flag))
					capacity_misses++;
			}
        	(*preql+(i))->done = 0;
        	(*preql+(i))->bcachefound = 0;
		}

	return 0;
}

/* update the base-cache with new value.
 * Note if a new block is being written, corresponding block buffer will 
 * not be present in base-cache, hence may result in some eviction,
 * else it will just be an update in cache.
 * Can be used to populate the base-cache in both read and write paths.
 * In read path, this is used to update the sector-cache after disk fetch.
 * In write path, this is used to update sector-cache before going to disk,
 * so internally this needs to update the cache-hits as well (updatehits flag
 * in sectorcache_add) and to update existing block in cache if present, by
 * deleting existing block and adding new as LRU.
 */
int overwrite_in_sectorcache(struct preq_spec **preql, int vop_iter)
{
    int i=vop_iter, len;
	__u32 ioblkID;
	__u32 newleader_ioblkID;
	int bcachefound = 3;
	unsigned long long stime=0, etime=0;
#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(vop_iter >= 0);
    assert(preql != NULL);
    assert(*preql != NULL);
#endif 
	assert(cachesimflag);
	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY) 
		len = MD5HASHLEN_STR-1;	/* Len of hex hash */
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
		len = BLKSIZE;
	}

	/* Insert into sectorcache only if warm-up phase is over */
	if (warmupflag)
		return 0;

	/* Handling single block (@vop_iter) at a time */

		/* If in read path, this block may already have been found in 
		 * sector-cache earlier, if so, no need to update now 
		 */
		if ((*preql+(i))->bcachefound == 1 && (*preql+(i))->rw)
			return 0;
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		print_lrucache_stat();
#endif

		ioblkID = (*preql+(i))->ioblk;
		if (freplayflag || preplayflag)
			newleader_ioblkID = (*preql+(i))->newleader_ioblk;
		else
			newleader_ioblkID = 0;
		stime = gettime();	/* START sector-write-upon-read/write time */
		if (!collectformat && disksimflag)
		{
			sectorcache_add(ioblkID, "cf",
				strlen("cf")+1, !(*preql+(i))->rw, 
				newleader_ioblkID, &bcachefound);
		}
		else
		{
			sectorcache_add(ioblkID, (char*)(*preql+(i))->content, 
				len, !(*preql+(i))->rw, newleader_ioblkID, &bcachefound);
		}
		(*preql+(i))->bcachefound = bcachefound==1 ? 1 : 0;
		etime = gettime();	/* END sector-write-upon-read/write time */

		if (!(*preql+(i))->rw)	//write request only
			ACCESSTIME_PRINT("sector-write-upon-write time: %llu %d\n",
					 etime - stime, vop_iter);

		/* The call to find_in_sectorcache() could have set the "done" flag
		 * for write requests as well. But write requests are not done at 
		 * this point, they still have to go to (content cache and) disk.
		 * So, reset the "done" flag here for write requests, just in case.
		 */
		if ((*preql+(i))->rw)
        	(*preql+(i))->done = 1; /* set done flag in read path only */
		else
        	(*preql+(i))->done = 0; /* reset done flag in write path */

	return 0;
}
