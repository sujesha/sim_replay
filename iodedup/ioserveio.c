/* This file has the interface that receives I/O requests
 * one-by-one and serves them by IODEDUP
 */
#include <asm/types.h>
#include <assert.h> 
#include <pthread.h>
#include "fixing.h" 
#include "slist.h"
#include "d2pv-map.h"
#include "debug.h"
#include "defs.h"
#include "per-input-file.h"
#include "serveio-utils.h"
#include "serveio.h"
//#include "sync-disk-interface.h"
#include "ulist.h"
#include "utils.h"
#include "vmbunching_structs.h"
#include "voltab.h"
#include "p2d-map.h"
#include "pdd_config.h"
#include "request-generic.h"
#include "contentcache.h"
#include "replay-plugins.h"
#include "debugg.h"
#include "ioruntime.h"
#include "unused.h"
#include "deduptab.h"
#include "replay-defines.h"
#include <time.h>

unsigned char cmaphit_flag = 0;
__u32 cmaphit_iodedupID = 0;

extern struct deduptab deduptab;
extern int warmupflag;
extern int read_enabled;       // Boolean: Enable reading
extern int write_enabled;       // Boolean: Enable writing
extern int collectformat;	/* MD5 hash present instead of BLKSIZE content */
extern int runtimemap;
extern int disksimflag;
extern __u64 ccache_hits;
extern __u64 ccache_hits_r;
extern __u64 ccache_misses;
extern __u64 ccache_misses_r;
extern __u64 ccache_dedup_misses;
extern __u64 ccache_nondedup_misses;
extern __u64 cmap_self_is_leader;
extern __u64 cmap_self_is_not_leader;
extern __u64 cmap_hits;
extern __u64 cmap_misses;
extern __u64 cmap_dirties;
extern __u64 ccache_dedup_hits;
extern __u64 ccache_nondedup_hits;
extern __u64 compulsory_misses;
extern __u64 capacity_misses;
extern FILE * ftimeptr;
extern vector32 * p2dmaps;

inline __u64 gettime(void);

/* Stats for ioreplay */
#ifdef PRO_STATS
    unsigned long iototalreq;    /* Including read/write reqs */
    unsigned long iototalblk;    /* Including read/write blks */
    
    unsigned long iorigblkread;  /* Original blks-to-be-read */
    unsigned long iorigblkwrite; /* Original blks-to-be-written */
    
    unsigned long iototalreadreq;    /* Read req received */
    unsigned long iototalwritereq;   /* Write req received */
    
    unsigned long iototalblkread;    /* Count of blks to-be-read */
    unsigned long iototalblkwrite;   /* Count of blks to-be-written */
    unsigned long io_zeroblksread;   /* Count of zeroblks so-not-to-be-read*/

    unsigned long io_blkread;    /* Blk read on IODEDUP success */
    unsigned long io_fallback_blkread;   /* Blk read on IODEDUP fail */
#endif

Node * newReusableDedupIDUList = NULL;
//static pthread_mutex_t newReusableIO_mutex;

extern Node * currReusableDedupIDUList;     /* from d2pv-map.c */
extern pthread_mutex_t currReusableIO_mutex;  /* from d2pv-map.c */
extern const char zeroarray[65537];
extern const char zerohash[HASHLEN_STR-1];

/* Fetch any d2p for given ioblkID,
 * Note that in IODEDUP, there is no particular definition of "dedup_d2p"
 * since this is not an I/O redirection per say.
 */
D2P_tuple_t* get_deduped_d2p(dedupmap_t *d2pv)
{
    D2P_tuple_t *d2pt;
    struct slist_head *p;

    __slist_for_each(p, &d2pv->d2pmaps)
    {
        d2pt = slist_entry(p, D2P_tuple_t, head);
        return d2pt;    /* Found */
    }

    return NULL;
}
/* Fetch d2p containing the (volID, ioblkID) because this mapping is used to 
 * analyze chunk and block boundaries (mostly during block writes)
 * Invoked only from del_d2p_from_d2pmaps()
 */
D2P_tuple_t* get_nondeduped_d2p(dedupmap_t *d2pv, __u32 ioblkID)
{
    D2P_tuple_t *d2pt;
    struct slist_head *p;

    __slist_for_each(p, &d2pv->d2pmaps)
    {
        d2pt = slist_entry(p, D2P_tuple_t, head);
        if (d2pt->ioblkID == ioblkID)
            return d2pt;    /* Found */
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "didnt find d2p(%u) but saw %u\n", ioblkID, d2pt->ioblkID);
#endif
    }

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "didnt find d2p(%u), return NULL\n", ioblkID);
#endif
    return NULL;
}

/* Find d2p tuple entry corresponding to (volID, ioblkID)
 * Return 0 for success
 */
int del_d2p_from_d2pmaps(dedupmap_t *d2pv, __u32 ioblkID)
{
    D2P_tuple_t* d2p = NULL;

    d2p = get_nondeduped_d2p(d2pv, ioblkID);
    if (d2p == NULL)
    {
        RET_ERR("ioblkID %u not found\n", ioblkID);
    }

    remove_d2p_tuple_from_map(d2p, d2pv);

    return 0;
}

/* add_chunkID_to_recyclelist: When chunks are over-written due to block-writes,
 * those chunk IDs can be added to recycle list so that they may be used at
 * the next possible instance.
 */
void add_iodedupID_to_recyclelist(dedup_id_t iodedupID)
{
    //pthread_mutex_lock(&newReusableIO_mutex);

    newReusableDedupIDUList = addtoUList(newReusableDedupIDUList, 
			&iodedupID, sizeof(dedup_id_t));

    //pthread_mutex_unlock(&newReusableIO_mutex);
}

/* recycleiodedupID(x)
 *
 *      Check if x is dedup or unique.
 *      If unique (i.e. has only 1 V2F tuple), add x to newReusableDedupIDUList
 *      If dedup, then unburden the mapping and free() any elements 
 *      that had been previously malloc'ed.
 *      return dedupstatus. (is this needed?)
 *
 * @param[in] iodedupID
 * @param[in] zeroflag
 * @param[in] volID
 * @param[in] ioblkID
 * @return status
 */
int recycleiodedupID(dedup_id_t iodedupID, int nozero_flag,
                        __u32 ioblkID)
{
    dedupmap_t *d2pv;

    d2pv = getDedupMap(iodedupID);
#ifdef DEBUG_SS
    assert(d2pv != NULL); /* Within loop, getChunkMap() should be success */
#endif

    if (slist_len(&d2pv->d2pmaps) > 1)
    {
        /* This dedup-blk is a deduplicated one, so iodedupID not recycled 
* Just remove the corresponding d2p element from d2pv->d2pmaps.
         */
        del_d2p_from_d2pmaps(d2pv, ioblkID);
    }
    else
    {
        /* No dedup, so dedupID to be recycled */
        del_d2p_from_d2pmaps(d2pv, ioblkID);
		hashtab_remove(deduptab.table, d2pv->dhashkey);	//d2pv unlinked
		setDedupMap(iodedupID, NULL);	//d2pv freed
		if (nozero_flag && iodedupID != DUMMY_ID)
    	    add_iodedupID_to_recyclelist(iodedupID);
    }

    return 0;
}

static int add_new_to_old_recyclelistIO()
{
    //pthread_mutex_lock(&currReusableIO_mutex);
    //pthread_mutex_lock(&newReusableIO_mutex);

	if (newReusableDedupIDUList != NULL)
	{
	    currReusableDedupIDUList = appendUList(currReusableDedupIDUList,
                                            &newReusableDedupIDUList);
#if defined(DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
    	assert(newReusableDedupIDUList == NULL);
#endif
	}

    //pthread_mutex_unlock(&newReusableIO_mutex);
    //pthread_mutex_unlock(&currReusableIO_mutex);

    return 0;
}

/* To reset the p2d mapping, the iodedupIDUList and free up malloc'ed memory */
int resetMappingsIO(p2d_datum *p2d, __u32 ioblkID)
{
    int ret;
    dedup_id_t iodedupID;

    /* As per our implementation of note_p2d_map(), a zero vblk
     * has a p2d mapping having a iodedupID == 0, indicating 
     * zero dedupblock. Thus, whether zero block or no, every vblk
     * will have a p2d map and a non-empty iodedupID field.
     */
    iodedupID = p2d->iodedupID;

    if (notzeroIO_vblk(p2d))
    {
        ret = recycleiodedupID(iodedupID, 1, ioblkID);
    }
    else
    {
        ret = recycleiodedupID(iodedupID, 0, ioblkID);
    }

    add_new_to_old_recyclelistIO();
    /* Free memory, was malloc'ed in note_p2d_map() */
    //free(p2d);    should this be done here? //FIXME

    if (ret)
    {
        RET_ERR("Error in initAndRecoverDedupIDs(%u)\n", ioblkID);
    }

    return ret;
}

/* mappingTrimScanIO(): to scan through the mappings of all pblks
 *      being written, and for every vblk initAndRecoverDedupID()
 *      to initialize all its dedup-block-related elements and for every
 *      iodedupID x in its iodedupIDUList, recycleiodedupID(x)
 *
 * Gets a list of blocks as input, lookup mapping.
 * If mapping found, invalidate the mapping and if it was a single block (i.e.
 * no duplicates mentioned in the mapping yet), then flush the corresponding 
 * content from cache as well. 
 * On the other hand, if mapping not found, nothing to invalidate here. 
 */
int mappingTrimScanIO(struct preq_spec **preql, int vop_iter)
{
    struct slist_head p2dlist;
    p2d_datum *p2d;
    int i = vop_iter;
    struct slist_head *p;
	int rc;
	__u32 ioblkID = (*preql+(i))->ioblk;
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    /* Get P2D map of the vblks being written */
    INIT_LIST_HEAD(&p2dlist);
    rc = getVirttoDedupMap(ioblkID, 1, &p2dlist);
	if (rc && runtimemap)
	{
		/* Since we are building map at run-time, a mapping lookup miss
		 * indicates that we are seeing this block for the first time.
		 * Create dummy map and set pdirty = 1. */
		p2d_datum *p2d = (p2d_datum*)calloc(1, sizeof(p2d_datum));
		p2d->pdirty = 1;
		p2d->iodedupID = DUMMY_ID;	//dummy: should be non-zero
		p2dmaps_set(p2dmaps, ioblkID, (void*)p2d);
		return 0;
	}
	else if (rc)
        RET_ERR("Either ZEROBLK or some other error\n");
    if (slist_len(&p2dlist) != 1)
    {
        RET_ERR("Number of mappings fetched %d is not equal to 1\n", 
				slist_len(&p2dlist));
    }

	/* List has only 1 element, so no need for i for iteration */
	p = slist_first(&p2dlist);
    p2d = slist_entry(p, p2d_datum, head);
	if (dedup_dirty(p2d))
	{
		return 0; /* Metadata dirty, no reset to be done here. Return done. */
	}
	p2d->pdirty = 1;	/* Proceeding to make metadata dirty, mark it here */
    if (resetMappingsIO(p2d, ioblkID))
        RET_ERR("resetMappingsIO error\n");

    return 0;
}

#if 0
__u16 get_fullvblkIO(struct preq_spec **preql, int *nreq, p2d_datum *p2d)
{
    dedupmap_t *d2pv = NULL;
    dedup_id_t nextID;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s: vblkID = %u\n", __FUNCTION__, p2d->ioblkID);
#endif

    nextID = p2d->iodedupID;
    d2pv = getDedupMap(nextID);
#ifdef DEBUG_SS
    assert(d2pv != NULL);
#endif
    if (dedup_dirty(p2d)) /* NOINIT_MAPDIRTY */
    {
        /* Since dedup is dirty, will have to fallback to original read */
        return 0;
    }

    if (elongate_preql(preql, nreq))
    {
        RET_ERR("realloc error for preql\n");
    }
    directcreate_preq_spec(p2d->iodedupID, BLKSIZE, 1, NULL,
            0, BLKSIZE - 1, *preql+(*nreq - 1));

    return BLKSIZE;
}
#endif


/* This is IODEDUP functionality for every read request.
 * Assumes that 1 block of BLKSIZE is received here at a time, with done==0.
 * Lookup mapping, if found, perform ARC cache lookup and mark "done" flag = 1.
 * If mapping not found, ARC cache can not be looked up => content-cache miss,
 * so "done" flag will stay reset, and will eventually result in disk-read,
 * simulation or otherwise.
 *
 * Function prototype is different than for CONFIDED/PROVIDED due to the 
 * different points of interception in the 2 cases, i.e. CONFIDED/PROVIDED
 * provide redirection mapping before hitting base-cache whereas IODEDUP 
 * provides content from content-cache, after having missed base-cache.
 */
int iodedupReadRequest(struct preq_spec *preq)
{
	struct slist_head p2dlist;
	struct dedupmap_t *d2pv = NULL;
	p2d_datum *p2d = NULL;
	int rc;
	__u8* content = NULL;
	unsigned long long stime=0, etime=0;

#if defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(preq != NULL);
#endif

	INIT_LIST_HEAD(&p2dlist);
	stime = gettime();	/* START IODEDUP mapping-lookup time */
	rc = getVirttoDedupMap(preq->ioblk, 1, &p2dlist);
	if (rc && runtimemap && rc != ZEROBLK_FLAG)
	{
		/* Mapping not found, so we need to build runtimemap, do nothing here */
		if (!warmupflag) {	//dont count stats in warmup (begin)
			cmap_misses++;
			compulsory_misses++;	//if missed here, goes to disk, count.
			etime = gettime();	/* END IODEDUP mapping-lookup time */
			ACCESSTIME_PRINT("ioded-mapping-lookup-fail time: %llu %d rw(%d)\n",
					 etime - stime,
    	             preq->bytes/BLKSIZE,
					 preq->rw);
		}
		return 0;
	}

	/* D2P found but check its dirty-flag */
#ifdef SIMREPLAY_DEBUG_SS_DONE
    assert(slist_len(&p2dlist) == 1);
#endif	
	p2d = slist_entry(slist_first(&p2dlist), p2d_datum, head);
    if (dedup_dirty(p2d))
    {
        /* Since dedup is dirty, will have to fallback to original read */
		if (!warmupflag) {	//dont count stats in warmup (begin)
			cmap_misses++;	//not a compulsory miss//
			capacity_misses++;
			cmap_dirties++;
		}
        return 0;
    }
	if (rc == ZEROBLK_FLAG)	//from above getVirttoDedupMap() call
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);

		/* This case will not occur in trace-replay with simulated disk. */
		// So, no need to worry about noting the cmaphit_dhashkey here. //

		/* For read requests, zero blocks may be being read, in which case,
		 * an actual disk fetch is not done, and the done=1 set anyway but
		 * bcachefound=0 because not found in sectorcache
		 * Create zero-filled content for later insertion in sectorcache.
		 */
		preq->done = 1;			//no disk fetch coz zeroblk, i.e. accomplished
		
		if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
				!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)	
		{
			preq->content = malloc(BLKSIZE);
			memcpy(preq->content, zeroarray, BLKSIZE);
		}
		else /* Disk not simulated, but content is hex hash */
		{
			/* Null char not to be copied as content */
			assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY);
			preq->content = malloc(MD5HASHLEN_STR-1);
			memcpy(preq->content, zeroarray, MD5HASHLEN_STR-1);
		}
		if (!warmupflag) {	//dont count stats in warmup (begin)
			cmap_hits++;	//Found D2P for non-dirty but zero blk
		}
		cmaphit_flag = 1;	//set flag for metadata hit
		return 0;
	}
	if (rc && !runtimemap)
	{
		RET_ERR("Couldnt find P2D map for specified read req (%u)\n",
                    preq->ioblk);
    }
	
	/* If we reach here, we found P2D map, not dirty and not zero blk */
	if (!warmupflag) {	//dont count stats in warmup (begin)
		cmap_hits++;	
	}
	cmaphit_flag = 1; 	//flag for metadata hit, note cmaphit_iodedupID below
	cmaphit_iodedupID = p2d->iodedupID;	//see explanation in resumeDeduping

	d2pv = getDedupMap(p2d->iodedupID);
	if (d2pv == NULL)
		RET_ERR("d2pv unexpectedly NULL\n");

	etime = gettime();	/* END IODEDUP mapping-lookup time */
	ACCESSTIME_PRINT("ioded-mapping-lookup-success time: %llu %d rw(%d)\n",
				 etime - stime,
   	             preq->bytes/BLKSIZE,
				 preq->rw);

	/* If disk is being simulated, the hex hash is already present in the traces
	 * and dhashkey is hash of hex hash. 
	 * Similarly done in overwrite_in_contentcache()
	 */
	stime = gettime();	/* START IODEDUP mapping-lookup time */
	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)	
		content = contentcache_lookup((__u8*)d2pv->dhashkey, MD5HASHLEN_STR-1, preq->ioblk, d2pv->ioblkID);
	else
		content = contentcache_lookup((__u8*)d2pv->dhashkey, BLKSIZE, preq->ioblk, d2pv->ioblkID);
	etime = gettime();	/* END IODEDUP mapping-lookup time */

	if (!warmupflag) {	//dont count stats in warmup (begin)
		if (preq->ioblk != d2pv->ioblkID)	//count only for read requests
			cmap_self_is_not_leader++;
		else
			cmap_self_is_leader++;
	}
	if (content == NULL)
	{
        ACCESSTIME_PRINT("ioded-content-lookup-fail time: %llu %d rw(%d)\n",
                     etime - stime,
                     preq->bytes/BLKSIZE,
                     preq->rw);
		if (!warmupflag) {	//dont count stats in warmup (begin)
	        ccache_misses++;
			ccache_misses_r++;
			capacity_misses++;
			if (preq->ioblk != d2pv->ioblkID)
				ccache_dedup_misses++;		//dedup-miss
			else
				ccache_nondedup_misses++;		//self-miss
		}

		if (!disksimflag)
		{
			assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);//assert(0);
			preq->content = NULL;
		}
		else	/* Trace already contains the content, dont NULLify it */
	        assert(preq->content != NULL);
	}
	else /* Found content */
	{
#ifdef SIMREPLAY_DEBUG_SS
		/* To verify that the content retrieved from content cache has 
		 * same key as the dhashkey mentioned in d2pv datastructure.
		 */
		//savemem unsigned char debugkey[HASHLEN];
		unsigned char* debugkey = malloc(HASHLEN);
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)	
		{
			if (memcmp(content, zeroarray, MD5HASHLEN_STR-1) == 0)
				memcpy(debugkey, zerohash, MD5HASHLEN);
			else if (getHashKey(content, MD5HASHLEN_STR-1, debugkey))
				RET_ERR("getHashKey() returned error for HASHLEN_STR\n");
		}
		else
		{
			if (memcmp(content, zeroarray, BLKSIZE) == 0)
				memcpy(debugkey, zerohash, HASHLEN);
			else if (getHashKey(content, BLKSIZE, debugkey))
				RET_ERR("getHashKey() returned error for BLKSIZE\n");
		}
		assert(memcmp(debugkey, d2pv->dhashkey, HASHLEN)==0);

#ifndef INCONSISTENT_TRACES
		/* Also, verify that the content present in the trace has same key
		 * as the dhashkey mentioned in d2pv datastructure.
		 */
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)	
		{
			if (memcmp(preq->content, zeroarray, MD5HASHLEN_STR-1) == 0)
				memcpy(debugkey, zerohash, MD5HASHLEN);
			else if (getHashKey(preq->content, MD5HASHLEN_STR-1, debugkey))
				RET_ERR("getHashKey() returned error for MD5HASHLEN_STR\n");
		}
		else
		{
			if (memcmp(preq->content, zeroarray, BLKSIZE) == 0)
				memcpy(debugkey, zerohash, HASHLEN);
			else if (getHashKey(preq->content, BLKSIZE, debugkey))
				RET_ERR("getHashKey() returned error for BLKSIZE\n");
		}
		assert(memcmp(debugkey, d2pv->dhashkey, HASHLEN)==0);
#endif
		/* This is within SIMREPLAY_DEBUG_SS but outside INCONSISTENT_TRACES */
		free(debugkey);	//savemem
#endif
		ACCESSTIME_PRINT("ioded-content-lookup-success time: %llu %d rw(%d)\n",
					 etime - stime,
    	             preq->bytes/BLKSIZE,
					 preq->rw);
		if (!warmupflag) {	//dont count stats in warmup (begin)
			ccache_hits++;
			ccache_hits_r++;
		}
		preq->done = 1;	 //no disk fetch coz found in content cache

		if (!disksimflag)
		{
			assert(0);	/* not expected in case of testing by iodedup-online */
			if (!collectformat)	/* content is BLKSIZE long */
			{
				assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
        	    preq->content = malloc(BLKSIZE);
            	memcpy(preq->content, content, BLKSIZE);
			}
			else				/* content is hex hash */
			{
				assert(REALDISK_SCANNING_COLLECTFORMAT_VMBUNCHREPLAY);
	            preq->content = malloc(MD5HASHLEN_STR-1);
    	        memcpy(preq->content, content, MD5HASHLEN_STR-1);
        	    //preq->content[MD5HASHLEN_STR-1] = '\0';	no null char to copy
			}
		}
#ifndef INCONSISTENT_TRACES
		else	/* Trace already contains the content, dont overwrite it */
		{
			if (read_enabled && write_enabled)
			{
				if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
						!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
		    	    assert(memcmp(preq->content, content, BLKSIZE)==0);
				else
	    		    assert(memcmp(preq->content, content, MD5HASHLEN_STR-1)==0);
			}
			else
			{
				/* If either reads or writes are disabled, the content in
				 * the disk-simulated request may be different than what
				 * was found in cache. We handle this by copying what was 
				 * in cache, back into the request.
				 */
				if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
						!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
					memcpy(preq->content, content, BLKSIZE);
				else
				{
					memcpy(preq->content, content, MD5HASHLEN_STR-1);
	                //preq->content[HASHLEN_STR-1] = '\0';	no null char
				}
			}
		}
#else
		else
		{
			free(preq->content);
			if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
					!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
            {
                preq->content = malloc(BLKSIZE);
                memcpy(preq->content, content, BLKSIZE);
            }
            else                /* content is hex hash */
            {
                preq->content = malloc(MD5HASHLEN_STR-1);
                memcpy(preq->content, content, MD5HASHLEN_STR-1);
                //preq->content[HASHLEN_STR-1] = '\0';  no null char to copy
            }
		}
#endif
	}

	if (content)
	{
	    /* If all reads and writes enabled, then these asserts should work,
	     * but if only reads are enabled, it is possible that a second read
	     * request actually reads (new) content that was written just before
	     * and hence is not present in cache
	     */
	    if ((DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY) && 
				read_enabled && write_enabled)
	        assert(memcmp(preq->content, content, MD5HASHLEN_STR-1)==0);
	    else if (read_enabled && write_enabled)
	        assert(memcmp(preq->content, content, BLKSIZE)==0);	
	}

	return 0;
}

void* io_mapupdate_sub(void *arg)
{
	int rc = 0;
	struct preq_spec **preql;
	struct vm_pkt *blkReq;
	int vop_iter;
	unsigned long long stime=0, etime=0;
	struct mapupdate_info *mip;

	mip = (struct mapupdate_info*)arg;
	preql = mip->preql;
	blkReq = mip->blkReq;
	vop_iter = mip->vop_iter;

	UNUSED(blkReq);

	stime = gettime(); /* START IODEDUP map-update for write time */
	rc = io_mapupdate(preql, vop_iter);
	if (rc < 0)
		fatal(NULL, ERR_USERCALL, "Error in io_mapupdate\n");

    etime = gettime();  /* END IODEDUP map-update for write time */
    ACCESSTIME_PRINT("iodedup-map-update-for-write time: %llu %d\n",
                     etime - stime, vop_iter);

	return NULL;
}


/* This is IODEDUP functionality for every write request block.
 * Mapping invalidation has already been done before this is called.
 *
 * Basic function:
 * For the content being written now, perform ARC cache update.
 *
 * Return 0 upon success */
int iodedupWriteRequest(struct preq_spec *preq)
{
#ifdef IOMETADATAUPDATE_UPON_WRITES
	if (preq->bcachefound)
		return overwrite_in_contentcache(preq, 0);  //version 3: Aug 27, 2014
	else 
		return overwrite_in_contentcache(preq, 1);	//version 1: MASCOTS & HiPC
#else
	UNUSED(preq);
	return 0;									//version 2: Apr 1, 2014
#endif
}
