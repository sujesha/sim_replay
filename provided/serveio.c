/* This file has the interface that receives I/O requests
 * one-by-one and serves them.
 */

#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include "chunking.h"
#include "slist.h"
#include "c2pv-map.h"
#include "debug.h"
#include "defs.h"
#include "per-input-file.h"
#include "rabin.h"
#include "serveio-utils.h"
#include "serveio.h"
#include "sync-disk-interface.h"
#include "ulist.h"
#include "utils.h"
#include "vmbunching_structs.h"
#include "voltab.h"
#include "v2c-map.h"
#include "v2p-map.h"
#include "pdd_config.h"
#include "request-generic.h"
#include "replay-plugins.h"
#include "pruntime.h"
#include "chunktab.h"
#include "debugg.h"
#include "replay-defines.h"
#include "content-simfile.h"
#include "vector16.h"
#include "simdisk-API.h"
#include "replay-generic.h"

extern vector16 * v2cmaps;
extern int warmupflag;
extern struct chunktab chunktab;
extern int runtimemap;
extern int collectformat;
extern FILE * fhashptr;

__u64 vmap_hits = 0;
__u64 vmap_misses = 0;
__u64 vmap_dirties = 0;
__u64 vmapmiss_cachehits = 0;
__u64 vmapdirty_cachehits = 0;
__u64 vmapmiss_cachemisses = 0;

#ifdef PRO_STATS
	unsigned long ptotalreq = 0;	/* Including read/write reqs */
	unsigned long ptotalblk = 0;	/* Including read/write blks */

	unsigned long porigblkread = 0;	/* Original blks-to-be-read */
	unsigned long porigblkwrite = 0;	/* Original blks-to-be-written */

	unsigned long ptotalreadreq = 0;	/* Read req received */
	unsigned long ptotalwritereq = 0;	/* Write req received */

	unsigned long ptotalblkread = 0;	/* Count of blks to-be-read */
	unsigned long ptotalblkwrite = 0;	/* Count of blks to-be-written */
	unsigned long pro_zeroblksread = 0;	/* Count of zeroblks so-not-to-be-read*/

	unsigned long pro_blkread = 0;	/* Blk read on PROVIDED success */
	unsigned long pro_fallback_blkread = 0;	/* Blk read on PROVIDED fail */

	unsigned long chunking_blkread = 0;	/*Blk read for write chunking*/

	/* Following asserts should hold on above variables :-
	 * -- totalreadreq + totalwritereq == totalreq
	 * -- totalblkread + totalblkwrite + pro_zeroblksread == totalblk
	 * -- pro_blkread + fallback_blkread + chunking_blkread == totalblkread
	 * -- fallback_blkread <= origblkread
     * -- origblkwrite == totalblkwrite (since we do not optimize writes)
	 */

	/* Some derived information :-
	 * -- extra for PROVIDED read: totalblkread - (origblkread+chunking_blkread)
	 * -- extra for PROVIDED write: chunking_blkread
	 * -- total extra: totalblkread - origblkread
	 */
#endif 

Node * newReusableChunkIDUList = NULL;
//static pthread_mutex_t newReusable_mutex;

unsigned char vmaphit_flag = 0;
unsigned char vmapdirty_flag = 0;

extern Node * currReusableChunkIDUList;		/* from c2pv-map.c */
extern pthread_mutex_t currReusable_mutex;	/* from c2pv-map.c */

extern FILE * ftimeptr;
//inline __u64 gettime(void);

extern int disksimflag;	/* No actual disk read/write, i.e. read req have content */

/* Returns a positive number of vblk found in this C2V_tuple_t struct 
 * else returns 0
 */
int vBlkFound(struct slist_head *p, __u16 volID, __u32 vBlkID, chunk_size_t clen)
{
	C2V_tuple_t *c2v;
	int bytes_left = (int)clen;
	__u16 start_offset_into_vblk;
	int i=0;

	c2v = slist_entry(p, C2V_tuple_t, head);
	start_offset_into_vblk = c2v->start_offset_into_vblk;
	while (bytes_left > 0)
	{
		if (vblkIDMatch(c2v->volID, c2v->start_vblk_id + i, volID, vBlkID))
	    	return (i+1);	/* Found */
		bytes_left -= (BLKSIZE - start_offset_into_vblk);
		start_offset_into_vblk = 0;	//for iter 2 onwards
		i++;
	}

	/* Not found */
	return 0;
}

/* Fetch c2v containing the (volID, vBlkID) because this mapping is used to 
 * analyze chunk and block boundaries (mostly during block writes
 * during the re-chunking process)
 */
C2V_tuple_t* get_nondeduped_c2v(chunkmap_t *c2pv, __u16 volID, 
				__u32 vBlkID)
{
	C2V_tuple_t *c2vt;
	struct slist_head *p;

	__slist_for_each(p, &c2pv->c2vmaps)
	{
		if (vBlkFound(p, volID, vBlkID, c2pv->clen))
		{
			c2vt = slist_entry(p, C2V_tuple_t, head);
			return c2vt;
		}
	}

	return NULL;
}

void unmark_old_dedupfetch(chunkmap_t *c2pv)
{
    struct slist_head *p;

    /* Un-marking the other dedupfetch */
    __slist_for_each(p, &c2pv->c2vmaps)
    {
        C2V_tuple_t *c2vt = slist_entry(p, C2V_tuple_t, head);
        if (c2vt->dedupfetch)   /* Found old tuple */
        {
            /* Disabled it and return */
            c2vt->dedupfetch = 0;   /*  old  */
            return;
        }
    }

    /* Should never reach here */
    assert(0);
}


/* mark_another_dedupfetch: This is invoked if the c2v with the
 * 		dedupfetch flag is just about to be deleted from the chunkmap.
 * 		If so, we need to mark another c2v of this chunkmap as dedupfetch=1.
 * 		However, if there was only one c2v in the c2pv (then it is the
 * 		one about to be deleted), then just ignore.
 * 		FIXME: This function can/should be used for optimization, but
 * 		for now, we just mark the "next" c2v as dedupfetch=1.
 */
void mark_another_dedupfetch(chunkmap_t *c2pv)
{
	struct slist_head *p;

	if (slist_len(&c2pv->c2vmaps) == 1)
	{
		/* If there was only one c2v in the c2pv (then it is the
		 * 		one about to be deleted), then just ignore.
		 */
		return;
	}

	/* Marking a new dedupfetch */
    __slist_for_each(p, &c2pv->c2vmaps)
    {
      	C2V_tuple_t *c2v = slist_entry(p, C2V_tuple_t, head);
        if (!c2v->dedupfetch) /* Found i1st c2v with dedupfetch flag disabled */
        {
			/* Enable it and return */
            c2v->dedupfetch = 1;
			return;
        }
    }

	/* Should never reach here */
}

/* Find c2v tuple entry corresponding to (volID, vBlkID)
 * Return 0 for success
 */
int del_c2v_from_c2vmaps(chunkmap_t *c2pv, __u16 volID, __u32 vBlkID)
{
	C2V_tuple_t* c2v = NULL;

	c2v = get_nondeduped_c2v(c2pv, volID, vBlkID);
	if (c2v == NULL)
	{
		RET_ERR("volID %u, vBlkID %u not found\n", volID, vBlkID);
	}

	if (c2v->dedupfetch == 1)
		mark_another_dedupfetch(c2pv);
	remove_c2v_tuple_from_map(c2v, c2pv);

	return 0;
}

/* add_chunkID_to_recyclelist: When chunks are over-written due to block-writes,
 * those chunk IDs can be added to recycle list so that they may be used at
 * the next possible instance.
 */
void add_chunkID_to_recyclelist(chunk_id_t chunkID)
{
	//pthread_mutex_lock(&newReusable_mutex);		
	
	newReusableChunkIDUList = addtoUList(newReusableChunkIDUList, 
			&chunkID, sizeof(chunk_id_t));

	//pthread_mutex_unlock(&newReusable_mutex);
}

/* recyclechunkID(x)
 *
 * 		Check if x is dedup or unique.
 * 		If unique (i.e. has only 1 V2C tuple), add x to newReusableChunkIDUList
 * 		If dedup, then unburden the mapping and free() any elements 
 * 		that had been previously malloc'ed.
 * 		return dedupstatus. (is this needed?)
 *
 * @chunkID[in]: chunkID to be recycled
 * @nozero_flag[in]: indicates whether blk is zero blk or not
 * @volID[in}: represents VM to which it belongs
 * @blockID{in]: blk ID within VM
 * @return status
 */
int recyclechunkID(chunk_id_t chunkID, int nozero_flag,
						__u16 volID, __u32 blockID)
{
	chunkmap_t *c2pv;

	c2pv = getChunkMap(chunkID);
#ifdef DEBUG_SS
 	assert(c2pv != NULL); /* Within loop, getChunkMap() should be success */	
#endif
	if (slist_len(&c2pv->c2vmaps) > 1)
	{
		/* This chunk is a deduplicated one, so chunkID not recycled */
		/* Just remove the corresponding c2v element from c2pv->c2vmaps,
		 * (and corresponding c2p element from c2pv->c2pmaps?).
		 */
		del_c2v_from_c2vmaps(c2pv, volID, blockID);
		//v2p = getVirtualtoPhysMap(volID, blockID);
		//del_c2p_from_c2pmaps(c2pv, volID, blockID);
	}
	else
	{
		/* No dedup, so chunkID to be recycled */
		del_c2v_from_c2vmaps(c2pv, volID, blockID);	//c2v freed
		//v2p = getVirtualtoPhysMap(volID, blockID);
		//del_c2p_from_c2pmaps(c2pv, volID, blockID);
		hashtab_remove(chunktab.table, c2pv->chashkey);	//c2pv unlinked
		setChunkMap(chunkID, NULL);	//node freed from hashtab
		if (nozero_flag)
			add_chunkID_to_recyclelist(chunkID);	//chunkID reusable
	}

	return 0;
}

static int add_new_to_old_recyclelist()
{
	//pthread_mutex_lock(&currReusable_mutex);		
	//pthread_mutex_lock(&newReusable_mutex);		

	if (newReusableChunkIDUList != NULL)
	{
		currReusableChunkIDUList = appendUList(currReusableChunkIDUList, 
											&newReusableChunkIDUList);
#ifdef DEBUG_SS
		assert(newReusableChunkIDUList == NULL);
#endif
	}

	//pthread_mutex_unlock(&newReusable_mutex);		
	//pthread_mutex_unlock(&currReusable_mutex);		

	return 0;
}

/** Iterate through chunkIDs and invoke recyclechunkID() 
 * @param[in,out] IDUListp
 * @param[in] zeroflag
 * @param[in] volID
 * @param[in] blockID
 * @return status
 */ 
static int initAndRecoverChunkIDs(Node ** IDUListp, int nozero_flag,
		__u16 volID, __u32 blockID)
{
	int chunkidx = 0;
	chunk_id_t *chunkIDp = NULL, val;
	int ret = 0;	
	int numchunks = ulistLen(*IDUListp);

	/* Even if the vblk is a zero block, the length of
	 * IDUList will be 1. So, in all cases, this length >= 1.
	 */
	for (chunkidx=0; chunkidx < numchunks; chunkidx++)
    {
		chunkIDp = (chunk_id_t*)popUList(IDUListp);
#ifdef NONSPANNING_PROVIDE
		/* Forced zero chunks can result in non-spanning PROVIDED.
		 * Just free and continue, no need to recycle.
		 */
		if (*chunkIDp == 0)
		{
			free(chunkIDp);
			continue;
		}
#endif

#if defined(DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
		assert((*chunkIDp == 0 && nozero_flag == 0) ||
				(*chunkIDp != 0 && nozero_flag == 1));
#endif
		val = *chunkIDp;
		free(chunkIDp);
		ret = recyclechunkID(val, nozero_flag, volID, blockID);
	}

#if defined(DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
	/* Due to the popUList above, IDUList must be empty here*/
	assert(*IDUListp == NULL);
#endif

	return ret;
}

/* To reset the v2c mapping, the chunkIDUList and free up malloc'ed memory */
int resetMappings(v2c_datum *v2c, __u16 volID, __u32 vBlkID)
{
	int ret;
	ret = notzero_vblk(v2c);
	if (ret < 0)
	{
		RET_ERR("%s: notzero_vblk err for vBlkID %u\n", __FUNCTION__, vBlkID);
	}

    /* As per our implementation of note_v2c_map(), a zero vblk
     * has a v2c mapping having a chunkIDUList of len = 1, and 
     * the chunkID registered in that list is 0, indicating 
     * zero chunk. Thus, whether zero block or no, every vblk
     * will have a v2c map and a non-empty chunkIDUList[].
     */

    if (ret == 1)
    {
       ret = initAndRecoverChunkIDs(&v2c->chunkIDUList, 1, volID, vBlkID);
    }
    else	//zero block
    {
	    /* There should be single chunkID = 0 in IDUList */
        ret = initAndRecoverChunkIDs(&v2c->chunkIDUList, 0, volID, vBlkID);
    }

	add_new_to_old_recyclelist();

#ifdef SIMREPLAY_DEBUG_SS
    /* IDUList must have been freed up in initAndRecoverChunkIDs() */
	assert(v2c->chunkIDUList == NULL);
#endif

	if (ret)
	{
		RET_ERR("Error in initAndRecoverChunkIDs(%u, %u)\n",
				volID, vBlkID);
	}

	return ret;
}

/** mappingTrimScan -- to handle 3. of Possibility I and II. Pasted here for
 * convenience.
 *
 * 3. mappingTrimScan() to scan through the mappings of all vblks
 *      being written, and for every vblk initAndRecoverChunkIDs()
 *      to initialize all its chunks-related elements and for every
 *      chunkID x in its chunkIDUList, recyclechunkID(x)
 *      (Assert that first vblk in V2C tuple is same as vblk being handled.)
 *
 * @blkReq[in]: the write request for which mapping invalidation is being done.
 * @return: status      
 */
static int mappingTrimScan(struct vm_pkt *blkReq)
{
	struct slist_head v2clist;
	int i = 0, rc;
	struct slist_head *p;
	int ret;

#ifdef DEBUG_SS
	assert(blkReq != NULL);
#endif

	__u32 vBlkID = getVirtBlkID(blkReq);
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	__u16 volID = (__u16) ret;
	__u32 numBlocks = getNumBlocks(blkReq);

	/* Get V2C map of the vblks being written */
	INIT_LIST_HEAD(&v2clist);
    rc = getVirttoChunkMap(volID, vBlkID, numBlocks, &v2clist);
	if (rc && !runtimemap)
	{
		RET_ERR("%s: error in getVirttoChunkMap\n", __FUNCTION__);
	}
	else if (rc == -2)
	{
		RET_ERR("%s: unexpected chunkID listlen for block %u\n",
			__FUNCTION__, vBlkID);
	}
	else if (rc)
	{
		for (i=0; i < (int)numBlocks; i++)
		{
			v2c_datum *v2c = (v2c_datum*)calloc(1, sizeof(v2c_datum));
			v2c->cdirty = 1;
			v2c->chunkIDUList = NULL;
			v2cmaps_set(v2cmaps, volID, vBlkID+i, (void*)v2c);
		}

		return 0;
	}
	if (slist_len(&v2clist) != (int) numBlocks)
	{
		RET_ERR("Number of mappings fetched %d is not equal to number"
						" requested %u\n", slist_len(&v2clist), numBlocks);
	}

	/* Iterate through list of v2c maps, and through chunkIDUList per vblk */
	i = 0;
	__slist_for_each(p, &v2clist)
	{
    	v2c_datum *v2c;
		v2c = slist_entry(p, v2c_datum, head);
		/* If metadata already dirty, then nothign to reset here */
		if (cvblk_dirty(v2c))
		{
			continue;
		}
		v2c->cdirty = 1;	/* Proceeding to make metadata dirty, mark it */
		if (resetMappings(v2c, volID, vBlkID+i))
			RET_ERR("resetMappings error\n");
	//?? free(v2c); /* Free memory, was malloc'ed in note_v2c_map() */

		i++;
	}

	return 0;
}

#ifndef NONSPANNING_PROVIDE
/* coinciding_start_boundary: Checks whether the start boundary of
 * 		given vblk and chunk coincide. Assumes that chunkmap is not of
 * 		"zero" chunk. Another version is chunkstart_coincides()
 *		which uses only the chunkmap, no v2c as input.
 * @v2c[in]: v2c map of the vblk
 * @c2pv[in]: chunkmap of the chunk 
 * @return : TRUE (1) or FALSE (0)
 */
int coinciding_start_boundary(v2c_datum *v2c, chunkmap_t *c2pv)
{
	/* Just checking the offset values is not sufficient. We need
	 * to verify also that this v2c and this c2pv are indeed
	 * inter-related.
	 */
	chunk_id_t firstchunkID;

#ifdef DEBUG_SS
	assert(v2c != NULL && c2pv != NULL);
	assert(c2pv->chunkID != 0);
#endif

    /* Get chunkID of first chunk in above v2c map */
    firstchunkID = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, 0);

    if (firstchunkID != c2pv->chunkID)
    {
        VOID_ERR("these v2c and c2pv are not interrelated!\n");
        return 0;
    }

	if (v2c->start_offset_into_chunk == 0 && notzero_vblk(v2c) == 1)
		return 1;
	else
		return 0;
}

/* coinciding_end_boundary: Checks whether the end boundary of
 * 		given vblk and chunk coincide. Assumes that chunkmap is not of
 * 		"zero" chunk. Another version is chunkend_coincides()
 *		which takes as input only the chunkmap, no v2c.
 * @v2c[in]: v2c map of the vblk
 * @c2pv[in]: chunkmap of the chunk 
 * @return : TRUE (1) or FALSE (0)
 */
int coinciding_end_boundary(v2c_datum *v2c, chunkmap_t *c2pv)
{
	chunk_id_t lastchunkID;

#ifdef DEBUG_SS
	assert(v2c != NULL && c2pv != NULL);
	assert(c2pv->chunkID != 0);
#endif

	/* Just checking the offset values is not sufficient. We need
	 * to verify also that this v2c and this c2pv are indeed
	 * inter-related.
	 */

    /* Get chunkID of last chunk in above v2c map */
    lastchunkID = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, 
										ulistLen(v2c->chunkIDUList) - 1);

	if (lastchunkID != c2pv->chunkID)
	{
		VOID_ERR("these v2c and c2pv are not interrelated!\n");
		return 0;
	}

	if (v2c->end_offset_into_chunk == (c2pv->clen - 1))
		return 1;
	else
		return 0;
}
#endif

/* getChunkMapLastofprev:
 * @volID[in]: manipulation to be done for this volume
 * @blkID[in]: need to fetch for the blk before this one
 * @v2c[out]: double pointer to retrieved v2c for the blk before this one.
 */
chunkmap_t* getChunkMapLastofprev(__u16 volID, __u32 blkID, 
				v2c_datum **v2c)
{
#ifdef DEBUG_SS
	assert(blkID != 0);
#endif
	__u32 blockID = blkID - 1;
	v2c_datum *temp_v2c;
	struct slist_head v2clist;
	chunkmap_t *last_c2pv;
	chunk_id_t lastchunkID;
	int rc;

    /* Get V2C map of current blockID.
	 * Since fetch is only for 1 vblk => if it is zeroblk, then v2clistp == NULL
	 */
	INIT_LIST_HEAD(&v2clist);
    rc = getVirttoChunkMap(volID, blockID, 1, &v2clist);
	if (rc == ZEROBLK_FLAG)
	{
#ifdef PRO_DEBUG_SS
		fprintf(stdout, "%s: Zero blk (%u, %u) has no data\n", __FUNCTION__,
						volID, blkID);
#endif
		return NULL;
	}
	else if (rc)
	{
		fprintf(stderr, "Error in getVirttoChunkMap\n");
		return NULL;
	}

    temp_v2c = slist_entry(slist_first(&v2clist), v2c_datum, head);

#if 0
	/* Use of v2clistp over, so release it, allocated in getVirttoChunkMap() */
	free(v2clistp);
#endif

    /* Get chunkID of last chunk in above v2c map */
    lastchunkID = *(chunk_id_t*)getIndexedNode(temp_v2c->chunkIDUList, 
										ulistLen(temp_v2c->chunkIDUList) - 1);

    /* Get chunkmap for the above last chunk */
    last_c2pv = getChunkMap(lastchunkID);

	/* If the v2c needs to be remembered, send in a valid v2c pointer */
	if (v2c != NULL)
		*v2c = temp_v2c;

	return last_c2pv;
}

struct chunkmap_t* getChunkMapSecondofcurr(__u16 volID, __u32 blkID)
{
//    assert(blkID != (BLKTAB_SIZE- 1));
    __u32 blockID = blkID;
	chunk_id_t secondchunkID;
	chunkmap_t *second_c2pv;
	struct slist_head v2clist;
	v2c_datum *v2c;
	int rc;

    /* Get V2C map of current blockID.
     * Since fetch is only for 1 vblk => if it is zeroblk, then v2clistp = NULL
	 */
	INIT_LIST_HEAD(&v2clist);
    rc = getVirttoChunkMap(volID, blockID, 1, &v2clist);
	if (rc == ZEROBLK_FLAG)
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "%s: Zero blk (%u, %u) has no data\n", __FUNCTION__,
                    volID, blkID);
#endif
        return NULL;
    }
	else if (rc)
	{
		fprintf(stderr, "Error in getVirttoChunkMap\n");
		return NULL;
	}

#ifdef DEBUG_SS
	assert(slist_len(&v2clist) == 1);
#endif
    v2c = slist_entry(slist_first(&v2clist), v2c_datum, head);

#if 0
	/* Use of v2clistp over, so release it, allocated in getVirttoChunkMap() */
	free(v2clistp);
#endif

    /* Get chunkID of second chunk in above v2c map */
    secondchunkID = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, 1);

    /* Get chunkmap for the first chunk of v2c, with dedupflag == 0 */
    second_c2pv = getChunkMap(secondchunkID);

    return second_c2pv;
}

struct chunkmap_t* getChunkMapFirstofnext(__u16 volID, __u32 blkID, 
			v2c_datum **v2c)
{
#ifdef DEBUG_SS
	assert(blkID != (BLKTAB_SIZE- 1));
#endif
	__u32 blockID = blkID + 1;
	v2c_datum *temp_v2c;
	chunk_id_t firstchunkID;
	chunkmap_t *first_c2pv;
	struct slist_head v2clist;
	int rc;

    /* Get V2C map of current blockID.
	 * Since fetch is only for 1 vblk => if it is zeroblk, then v2clistp = NULL
	 */
	INIT_LIST_HEAD(&v2clist);
    rc = getVirttoChunkMap(volID, blockID, 1, &v2clist);
	if (rc == ZEROBLK_FLAG)
	{
#ifdef PRO_DEBUG_SS
		fprintf(stdout, "%s: Zero blk (%u, %u) has no data\n", __FUNCTION__,
					volID, blkID);
#endif
		return NULL;
	}
	else if (rc)
	{
		fprintf(stderr, "Error in getVirttoChunkMap\n");
		return NULL;
	}

#ifdef DEBUG_SS
	assert(slist_len(&v2clist) == 1);
#endif
    temp_v2c = slist_entry(slist_first(&v2clist), v2c_datum, head);

#if 0
	/* Use of v2clistp over, so release it, allocated in getVirttoChunkMap() */
	free(v2clistp);
#endif

    /* Get chunkID of first chunk in above v2c map */
    firstchunkID = *(chunk_id_t*)getIndexedNode(temp_v2c->chunkIDUList, 0);

    /* Get chunkmap for the first chunk of v2c, with dedupflag == 0 */
    first_c2pv = getChunkMap(firstchunkID);

	if (v2c != NULL)
		*v2c = temp_v2c;

	return first_c2pv;
}

#ifndef NONSPANNING_PROVIDE
/* This function covers points 1. and 2. in Possibility I and II of
 * block write handling provideWriteRequest(). Pasted here for convenience.
 * 1. The first vblk being written may already have a chunk mapping 
 *      starting at offset 0. If so, then no previous chunk needs to 
 *      be fetched (unless it is a "forced" boundary case mentioned
 *      in Possibility II below).
 *      However, if the start boundaries do not coincide, it implies
 *      that the prev blk's last chunk ends into this vblk that is
 *      being written. So, that chunk's data has to be fetched
 *      into the buffer to-be-chunked => prechunk
 * 2. Similarly, the last vblk being written may already have a 
 *      chunk mapping ending at offset BLKSIZE-1. If so, then no next
 *      chunk needs to be fetched (unless it is a "forced" boundary
 *      case due to the next vblk being a zero block).
 *      However, if the end boundaries do not coincide, it implies
 *      that this last vblk's last chunk ends into the next vblk.
 *      So, that chunk's data has to be fetched into the buffer
 *      to-be-chunked => postchunk
 *      Note the "sequentially next" chunk at this time.

 * For Possibility II:
 * 1. Since the first vblk is a new block being written (was zero block
 *      previously), so it is possible that the last chunk of previous
 *      vblk had a "forced" boundary and not a content-based boundary.
 *      Fetch that chunk => prechunk
 * 2. If last blk being written was previously a zero block but the 
 *      block right after it was not, then the first chunk of that 
 *      existing block was by circumstance. Its starting boundary can
 *      now change. Fetch that chunk => postchunk
 *      If any "forcing" results in pre- or post-chunks, then corresponding
 *      chunkID x also needs recyclechunkID(x).
 *
 * createPrePostchunkBufs -- accumulate the pre-chunk and post-chunk buffers
 * @prechunkp[out]: output buffer for prechunk data
 * @postchunkp[out]: output buffer for postchunk data
 * @volID[in]: volume whose block is being written
 * @blockID[in]: ID of block being written
 * @numB[in]: number of blocks being written in this one request
 * @seqnextp[out]: output pointer to the next chunk after postchunk
 */
void createPrePostchunkBufs(struct chunk_t **prechunkp, 
							struct chunk_t **postchunkp, 
			__u16 volID, __u32 blockID, u32int numB, chunkmap_t **seqnextp)
{

    v2c_datum *prev_v2c;
    chunkmap_t *last_c2pv = NULL;
    v2c_datum *v2c;
    chunkmap_t *first_c2pv = NULL;
    v2c_datum *first_v2c;
	__u32 postchunk_endblkID;

#if defined(DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
    assert(*prechunkp == NULL && *postchunkp == NULL);
#endif
   	prev_v2c = calloc(1, sizeof(v2c_datum));
   	v2c = calloc(1, sizeof(v2c_datum));
	first_v2c = calloc(1, sizeof(v2c_datum));
	if (!prev_v2c || !v2c || !first_v2c)
	{
		VOID_ERR("calloc failed\n");
		return;
	}

    /* If the block being written is the first block, no prechunk */
    if (blockID == 0)
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "vblk has blkID 0 => first block => no prechunk\n");
#endif
        goto noprechunk;
    }

    /* If we are here, this block has a prechunk. Fetching it into prechunkp.
	 * Get chunkmap of last chunk (and v2c) of prev-to-first-vblk 
     * because whether "their boundaries are coinciding or not", and
     * "their starting and ending offsets", determine the prechunk content
     */
    last_c2pv = getChunkMapLastofprev(volID, blockID, &prev_v2c);
    if (!last_c2pv)
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "prev-to-first-vblk is a zeroblk => no prechunk\n");
#endif
    }
    else
    {
        if (form_prechunk(prechunkp, prev_v2c, last_c2pv))
		{
            VOID_ERR("failure in form_prechunk\n");
			return;
		}
    }

noprechunk:

    /* Postchunk buffer can be due to following reasons:-
     * If next-vblk-after-last is a zero block, then nopostchunk.
     *    --And no seqnextp.
     * a. last-vblk-being-written was previously a zero block but the
     *      next-block_after-last is not zero block, then the first 
     *      chunk of next-block-after-last is the postchunk -> get_fullchunk()
     *    --And the second chunk of next-block-after-last is seqnextp.
     * b. Else in case of last chunk of last-vblk-being-written ending at 
     *      offset clen-1 (i.e. coinciding_end_boundary), then no postchunk.
     *    --And the first chunk of next-block-after-last is seqnextp.
     * c. Else if last chunk of last-vblk-being-written ends at some 
     *      offset within next-block-after-last, that much data within 
     *      next-block-after-last is "postchunk" => if multichunk_vblk(next)
     *    --And the second chunk of next-block-after-last is seqnextp.
     * d. Else if last chunk of last-vblk-being-written ends at some
     *      offset within some block later i.e. huge multi-vblk chunk,
     *      then all of that content until the last offset into final
     *      vblk is "postchunk".
     *    --And the second chunk of that "final" vblk is seqnextp.
     *    Note that c and d are both cases on non-coinciding boundaries.
     */

    /* Get chunkmap of first chunk of next-vblk-after-last */
    first_c2pv = getChunkMapFirstofnext(volID, blockID + numB - 1, &first_v2c);
    if (!first_c2pv)
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "next-block-after-last is a zeroblk =>no postchunk\n");
#endif
        /* The only possibility of having no postchunk is if 
         * next-vblk-after-last is a zero block
         */
        goto nopostchunk;
    }

    /* Get chunkmap of last chunk (& v2c map) of last-vblk-being-written */
    last_c2pv = getChunkMapLastofprev(volID, blockID + numB, &v2c);
    if (!last_c2pv)
    {
#ifdef PRO_DEBUG_SS
        VOID_ERR("last-vblk-being-written was zero while"
                        " next-block-after-last is not => yes postchunk\n");
#endif
		return;
    }

    if (form_postchunk(postchunkp, v2c, first_v2c, last_c2pv, first_c2pv,
                            &postchunk_endblkID))
	{
        VOID_ERR("form_postchunk failed\n");
		return;
	}



    /* Noting the seqnext/seqnextp here for a non-null postchunk.
     * If postchunk is present, need to check if its end boundary coincides
     *      with some block boundary or not. 
     *      If yes coincides, then seqnextp should be pointer to FIRST
     *          chunkmap of the block postchunk_endblkID+1. 
     *      If no, then seqnextp should be pointer to SECOND
     *          chunkmap of postchunk_endblkID (case #)
     */

    /* Checking that the first chunk of next-vblk-after-last has
     * a coinciding end boundary 
     */
    if (coinciding_end_boundary(first_v2c, first_c2pv))
    {
        /* Fetch first chunkmap of postchunk_endblkID+1 into seqnextp */
        *seqnextp = getChunkMapFirstofnext(volID, postchunk_endblkID, NULL);
    }
    else
    {
        /* Fetch second chunkmap of postchunk_endblkID into seqnextp */
        *seqnextp = getChunkMapSecondofcurr(volID, postchunk_endblkID);
    }

nopostchunk:

    if (*postchunkp == NULL)
        *seqnextp = NULL;

   	free(prev_v2c);
    free(v2c);
    free(first_v2c);

    return;
}
#endif


/* get_deduped_c2v: Aim is to locate the deduped vblk (which is to
 *      be fetched instead of the originally requested vblk). Input
 *      here is the c2pv and we locate the deduped vblk based on
 *      dedupfetch flag. This is invoked via get_idx_into_vblklist()
 *      and get_fullchunk().
 * 		This is the PROVIDED redirection logic in play.
 * @c2pv[in]: chunkmap of above concerned chunk
 * @return: the c2v tuple marked with dedupfetch:1 for this c2pv
 */
C2V_tuple_t * get_deduped_c2v(chunkmap_t *c2pv)
{
    struct slist_head *p;
    struct C2V_tuple_t *c2v;

	/*
     * C2V_tuple_t has dedupfetch:1 flag, so identify the dedup 
     *      tuple, calculate vblkid corresponding to requested block and 
     *      hence get index of "needed" block.
     */

	/* Iterating through each c2v within given chunkmap */
	assert(c2pv != NULL);
    __slist_for_each(p, &c2pv->c2vmaps)
    {
        c2v = slist_entry(p, struct C2V_tuple_t, head);
        if (c2v->dedupfetch)	/* Found c2v with dedupfetch flag enabled */
        {
			return c2v;
		}
	}
    /* At least one of the c2v should have dedupfetch == 1, so
     * we should never reach here!
     */
	return NULL;
}

/* get_vblk_idx_offset: For specified position "pos" within 
 * 		chunk whose dedup_c2v is "dc2v", find the corresponding
 * 		position "offset" within a vblk and the index "idx" of that 
 *		vblk within the "dc2v".
 *		Used by get_idx_into_vblklist().
 *
 * @dc2v[in]: represents deduped_c2v of chunk
 * @pos[in]:	the desired position within the chunk
 * @idx[out]:	index of the final vblk within dc2v
 * @offset[out]: final position within the final vblk
 */
void get_vblk_idx_offset(C2V_tuple_t *dc2v, chunk_size_t pos, 
					int *idx, __u16 *offset)
{
    //FIXME: are these calculations correct?
    *idx = (pos + dc2v->start_offset_into_vblk) / BLKSIZE;
    *offset = (pos + dc2v->start_offset_into_vblk) % BLKSIZE;
	
	return;
}

/* get_idx_into_vblklist: Aim is to locate the deduped vblk (which is to
 * 		be fetched instead of the originally requested vblk). For this,
 * 		c2pv is the chunk found from original v2c map and hence v2c had
 * 		indicated (in the caller function) that the originally requested
 * 		vblk starts at "start" offset inside the chunk c2pv. So, here we
 * 		wish to map from c2pv:start to fetchvblk:start_offset_into_fetchvblk
 * 		s.t. instead of fetching the originally requested vblk, we can
 * 		fetch "fetchvblk" and hopefully it will be in cache if popular.
 * 		This is the PROVIDED redirection logic in play (get_deduped_c2v).
 * 		In this function, we represent the fetchvblk by <deduped_c2v,idx> 
 * 		pair, where deduped_c2v indicates the deduplicated vblks list
 * 		and idx is the index of the to-be-fetched vblk within that list.
 * 
 * @pos_into_chunk[in]: position into chunk c2pv
 * @c2pv[in]: chunkmap of above concerned chunk
 * @deduped_c2v[out]: the c2v tuple marked with dedupfetch:1 for this c2pv
 * @start_offset_into_fetchvblk[out]: offset which "start" maps to
 * 			in the deduped vblk/tuple
 * @idx[out]: idx of vblk into which "start" maps in the deduped vblk/tuple
 * @return: status
 */
int get_idx_into_vblklist(chunk_size_t pos_into_chunk, chunkmap_t *c2pv,
                            struct C2V_tuple_t **deduped_c2v, int pflag,
                            __u16 *pos_offset_into_fetchvblk, int *idx)
{
#ifdef DEBUG_SS
	assert(deduped_c2v != NULL);
#endif
	if (pflag == PRESENT)
	{
		/* For a repeated invocation of this function, need not perform
		 * get_deduped_c2v() again, so simply set the present flag to 1.
		 * Used in createPrePostchunkBufs().
		 */
		assert(*deduped_c2v != NULL);
	}
	else
	{
    	/* chosen tuple returned as deduped_c2v by get_deduped_c2v */
		*deduped_c2v = get_deduped_c2v(c2pv);
	}
	if (*deduped_c2v == NULL)
		RET_ERR("could not get_deduped_c2pv\n");

	get_vblk_idx_offset(*deduped_c2v, pos_into_chunk, idx, 
				pos_offset_into_fetchvblk);

#ifdef PRO_DEBUG_SS
    fprintf(stdout, "PROVIDE the dedup tuple with (idx, offset) = "
					"(%d, %lu)\n", *idx, *pos_offset_into_fetchvblk);
#endif
    return 0;
}

#if 0
int get_idx_into_pblklist(chunk_size_t start, chunkmap_t c2pv, 
							struct C2P_tuple_t **dedup_c2p, 
							__u16 *start_offset_into_fetchpblk)
{
    struct slist_head *p;
	struct C2P_tuple_t *c2p;
	/* 
	 * C2P_tuple_t has dedupfetch:1 flag, so identify the dedup 
	 * 		tuple, calculate num pblks corresponding to requested 
	 * 		block and hence get index of "needed" block.
	 * 
	 * The chosen tuple will be marked as dedup_tuple when function returns.
	 */

	__slist_for_each(p, &c2pv->c2pmaps)
    {
        prev_c2pv = c2pv;
        c2p = slist_entry(p, struct C2P_tuple_t, head);
		if (c2p->dedupfetch)
		{
#ifdef PRO_DEBUG_SS
			fprintf(stdout, "Found the dedup tuple\n");
#endif
			//TODO: are these calculations correct?
			idx = (start + c2p->start_offset_into_pblk) / BLKSIZE;
			*start_offset_into_fetchpblk = (start + c2p->start_offset_into_pblk) % BLKSIZE;
			*dedup_c2p = c2p;
			return 0;
		}			
	}
	/* At least one of the c2p should have dedupfetch == 1, so
	 * we should never reach here!
	 */
	return -1;
}

struct pread_spec * malloc_outputpread(int num)
{
	struct pread_spec *out = malloc(num * sizeof(struct pread_spec));
	if (out == NULL)
	{
		fprintf(stderr, "malloc failed in malloc_outputpio\n");
	}
	return out;
}

void free_outputpread(struct pread_spec *out)
{
	free(out);
}
#endif

/* fetchdata_pblk: For fetching the data of a single pblk as specified
 * 				in preql, and also limited to (start, end) offsets also
 * 				specified in preql
 * @preql: single pblk fetch or read request
 * @return: status
 */
int fetchdata_pblk(struct preq_spec *preql)
{
    //FIXME: should this data fetch change to async?
	if (preql->rw)
	{
		//gen_malloc(preql->content, __u8, preql->end - preql->start + 1);
		if (_do_read(preql))
			RET_ERR("_do_read had error\n");

#if 0
#ifdef PRO_STATS
	chunking_blkread++;
	ptotalblkread++;
#endif
#endif

	}
	else
		RET_ERR("fetchdata_pblks can not work for writes\n");

	return 0;
}

int loop_and_fetchdata_pblks(struct chunk_t **chunkp, __u16 volID,
				__u32 startblk, __u16 start_offset_into_fetchvblk,
				__u32 endblk, __u16 end_offset_into_fetchvblk, __u16 *pos)
{
	*pos = 0;
	__u32 iter;
	struct preq_spec preq_local;
	__u16 start_into_buf, end_into_buf;

#ifdef DEBUG_SS
	assert(*chunkp != NULL);
#endif

    /* 3 cases wrt the last_c2pv chunk's straddling of blocks :-
     * (A) prev vblk and last_c2pv have coinciding boundaries
     * (B) chunk lies within prev vblk
     * (C) chunk straddles vblks prior to prev-vblk as well => huge
	 *
	 * In case (A) and (B), startblk == endblk, and
	 * in case (C), endblk > startblk
	 */

	for (iter = startblk; iter <= endblk; iter++)
	{       
#if 0
		create_pread_spec(volID, iter, BLKSIZE, output);
		fetchdata_pblks(output, buf, 1);
#endif

		/* For a multi-vblk chunk, its start_offset_into_fetchvblk
		 * for first vblk and end_offset_into_fetchvblk for last vblk
		 * may or may not be zero. All intermediate vblks should be
         * copied in entirety into prechunkp.
         */
		if (iter == startblk)
			start_into_buf = start_offset_into_fetchvblk;
		else
			start_into_buf = 0;

		if (iter == endblk)
			end_into_buf = end_offset_into_fetchvblk;
		else 
			end_into_buf = BLKSIZE - 1;

//fprintf(stdout, "create_preq_spec in %s\n", __FUNCTION__);
        create_preq_spec(volID, iter,
                    end_into_buf - start_into_buf + 1, 1, NULL,
                    start_into_buf, end_into_buf, &preq_local);

        /* Fetch the pblk (with start & end offsets) data into buf */
        if (fetchdata_pblk(&preq_local))
			RET_ERR("error in fetchdata_pblk\n");

		/* Copy those many bytes from buf into outbuf and free buf */
		memcpy(cdata(*chunkp)+*pos, preq_local.content,
                                end_into_buf-start_into_buf+1);
		free(preq_local.content);

		pos += (end_into_buf-start_into_buf+1);

	}	
	return 0;
}

#if 0
/* get_outbuf: Copies specified number of bytes from fetchbuf into outbuf 
 * 		
 * @outbuf[out]: Destination buffer
 * @fetchbuf[in]: Source buffer
 * @fetchbuf_len[in]: Length of source buffer
 * @startoff_into_fetchbuf[in]: offset from which to start copying
 * @numbytes_to_copy[in]:
 * @return: status
 */
int get_outbuf(char **outbuf, char *fetchbuf, chunk_size_t fetchbuf_len,
				__u16 startoff_into_fetchbuf, 
				chunk_size_t numbytes_to_copy)
{
	assert(outbuf == NULL);
	*outbuf = malloc(numbytes_to_copy * sizeof(char));
	if (*outbuf == NULL)
		RET_ERR("malloc failed for outbuf\n");
	
	assert(fetchbuf != NULL);
	assert(startoff_into_fetchbuf + numbytes_to_copy <= fetchbuf_len);

	memcpy(*outbuf, fetchbuf + startoff_into_fetchbuf, numbytes_to_copy);

	return 0;
}

#endif

/* get_partchunk: Get part of a chunk that is guaranteed be less than BLKSIZE.
 * 		This is invoked when trying to fetch data of a single vblk via
 * 		provideReadRequest. Since a vblk being requested originally can
 * 		map to any offset within another (deduplicated) vblk and can end
 * 		within corresponding (BLKSIZE away) offset within next (deduplicated)
 * 		vblk, hence this partchunk can be present in 2 ways:
 * 		1) either witihin a single vblk or 
 * 		2) straddled across two vblks. 
 * 		get_partchunk assumes that the c2v tuple being input is already
 * 		deduped (i.e. deduped_c2v) in the caller.
 *
 * @start_offset_into_fetchvblk[in]: point within vblk to start from
 * @numbytes_to_copy[in]
 * @deduped_c2v[in]: c2v after applying PROVIDED logic
 * @fetch_vblk_idx[in]: vblk to which above start offset applies
 * @outbuf[out]: contains the retrieved partchunk data
 * @return: status
 */
int get_partchunk(struct preq_spec **preql, int *nreq,
					__u16 start_offset_into_fetchvblk, 
					chunk_size_t numbytes_to_copy, 
					C2V_tuple_t *deduped_c2v,
                    /* struct vm_pkt *blkReq,  */
					__u32 fetch_vblk_idx) //, 
					//char **outbuf)
{
	__u32 vBlkID; 

	/* Case when partchunk is within single deduplicated vblk */
    if ((BLKSIZE - start_offset_into_fetchvblk + 1) >= numbytes_to_copy)
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "Just this one pblk is enough\n");
#endif
        vBlkID = deduped_c2v->start_vblk_id + fetch_vblk_idx;
		if (*preql != NULL)
		{
			__u32 ioblk;
    		if (getVirttoPhysMap(deduped_c2v->volID, vBlkID, &ioblk))
		        VOID_ERR("getVirttoPhysMap error'ed\n");
			if (ioblk == (*preql+(*nreq - 1))->ioblk &&
				start_offset_into_fetchvblk == (*preql+(*nreq - 1))->end+1)
			{
				(*preql+(*nreq - 1))->end += numbytes_to_copy;
				return 0;
			}
		}

		if (elongate_preql(preql, nreq))
		{
			RET_ERR("realloc error for preql\n");
		}
//fprintf(stdout, "create_preq_spec in %s: partchunk-in-single-blk\n", __FUNCTION__);
//fprintf(stdout, "create_preq_spec(%u, %u) => fetch_vblk_idx=%d\n",
//				deduped_c2v->volID, deduped_c2v->start_vblk_id, fetch_vblk_idx);
		if (disksimflag)
		{
			__u8 *simcontent = malloc(BLKSIZE);
			if (simcontent == NULL)
				RET_ERR("malloc failed\n");
			if (runtimemap)
			{
				if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
					disk_read_trap(deduped_c2v->volID, vBlkID, simcontent,
							BLKSIZE);
				else
					disk_read_trap(deduped_c2v->volID, vBlkID, simcontent,
							MD5HASHLEN_STR-1);
			}
			else
			{
				__u32 ioblk;
	    		if (getVirttoPhysMap(deduped_c2v->volID, vBlkID, &ioblk))
			        VOID_ERR("getVirttoPhysMap error'ed\n");
				get_simcontent(ioblk, simcontent, 1);
			}
#if defined(SIMREPLAY_DEBUG_SS_DONE)			
			fprintf(stdout, "simcontent=%s\n", simcontent);
#endif
			create_preq_spec(deduped_c2v->volID, vBlkID, BLKSIZE,1, simcontent, 
					start_offset_into_fetchvblk, 
					start_offset_into_fetchvblk + numbytes_to_copy - 1, 
					*preql+(*nreq - 1));
			free(simcontent);
		}
		else
		{
			create_preq_spec(deduped_c2v->volID, vBlkID, BLKSIZE, 1, NULL, 
					start_offset_into_fetchvblk, 
					start_offset_into_fetchvblk + numbytes_to_copy - 1, 
					*preql+(*nreq - 1));
		}
    }
	/* Case when partchunk straddles 2 deduplicated vblks */
    else
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "Get this blk and the next one also\n");
#endif
        vBlkID = deduped_c2v->start_vblk_id + fetch_vblk_idx;
		if (*preql != NULL)
		{
			__u32 ioblk;
    		if (getVirttoPhysMap(deduped_c2v->volID, vBlkID, &ioblk))
		        VOID_ERR("getVirttoPhysMap error'ed\n");
			if (ioblk == (*preql+(*nreq - 1))->ioblk &&
				start_offset_into_fetchvblk == (*preql+(*nreq - 1))->end+1)
			{
				(*preql+(*nreq - 1))->end += (BLKSIZE - start_offset_into_fetchvblk);
				(*preql+(*nreq - 1))->bytes += (BLKSIZE - start_offset_into_fetchvblk);
				return 0;
			}
		}

		/* Part from 1 block */
		if (elongate_preql(preql, nreq))
		{
			RET_ERR("realloc error for preql\n");
		}
//fprintf(stdout, "create_preq_spec in %s: partchunk-straddling-blk1\n", __FUNCTION__);
		if (disksimflag)
		{
			__u8 *simcontent = malloc(BLKSIZE);
			if (simcontent == NULL)
				RET_ERR("malloc failed\n");
			if (runtimemap)
			{
				if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
					disk_read_trap(deduped_c2v->volID, vBlkID, simcontent,
						BLKSIZE);
				else
					disk_read_trap(deduped_c2v->volID, vBlkID, simcontent,
						MD5HASHLEN_STR-1);
			}
			else
			{
				__u32 ioblk;
	    		if (getVirttoPhysMap(deduped_c2v->volID, vBlkID, &ioblk))
			        VOID_ERR("getVirttoPhysMap error'ed\n");
				get_simcontent(ioblk, simcontent, 1);
			}
#if defined(SIMREPLAY_DEBUG_SS_DONE)			
			fprintf(stdout, "simcontent=%s\n", simcontent);
#endif
			create_preq_spec(deduped_c2v->volID, vBlkID, BLKSIZE,1, simcontent, 
					start_offset_into_fetchvblk, 
					BLKSIZE - 1, 
					*preql+(*nreq - 1));
			free(simcontent);
		}
		else
		{
			create_preq_spec(deduped_c2v->volID, vBlkID, BLKSIZE, 1, NULL, 
					start_offset_into_fetchvblk, 
					BLKSIZE - 1, 
					*preql+(*nreq - 1));
		}

		/* Part from next block */
		if (elongate_preql(preql, nreq))
		{
			RET_ERR("realloc error for preql\n");
		}
//fprintf(stdout, "create_preq_spec in %s: partchunk-straddling-blk2\n", __FUNCTION__);
		if (disksimflag)
		{
			__u8 *simcontent = malloc(BLKSIZE);
			if (simcontent == NULL)
				RET_ERR("malloc failed\n");
			if (runtimemap)
			{
				if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
					disk_read_trap(deduped_c2v->volID, vBlkID+1, simcontent,
									BLKSIZE);
				else
					disk_read_trap(deduped_c2v->volID, vBlkID+1, simcontent,
									MD5HASHLEN_STR-1);
			}
			else
			{
				__u32 ioblk;
	    		if (getVirttoPhysMap(deduped_c2v->volID, vBlkID+1, &ioblk))
			        VOID_ERR("getVirttoPhysMap error'ed\n");
				get_simcontent(ioblk, simcontent, 1);
			}
#if defined(SIMREPLAY_DEBUG_SS_DONE)			
			fprintf(stdout, "simcontent=%s\n", simcontent);
#endif
			create_preq_spec(deduped_c2v->volID, vBlkID+1,BLKSIZE,1,simcontent, 
					0, start_offset_into_fetchvblk - 1, 
					*preql+(*nreq - 1));
			free(simcontent);
		}
		else
		{
			create_preq_spec(deduped_c2v->volID, vBlkID+1, BLKSIZE, 1, NULL, 
					0, start_offset_into_fetchvblk - 1, 
					*preql+(*nreq - 1));
		}
#if 0
		create_pread_spec(deduped_c2v->volID, vBlkID, BLKSIZE, output);
		create_pread_spec(deduped_c2v->volID, vBlkID+1, BLKSIZE, (output+1));
#endif
    }
#if 0
   	fetchdata_pblks(output, fetchbuf, numblks);
	free_outputpread(output);

    if (get_outbuf(outbuf, fetchbuf, numblks*BLKSIZE, 
			start_offset_into_fetchvblk, numbytes_to_copy))
		RET_ERR("Error in get_outbuf\n");
#endif
	return 0;
}

chunk_size_t get_partvblk(struct preq_spec **preql, int *nreq,
				v2c_datum *v2c, int chunkidx, 
				chunk_size_t startoff_into_chunk, 
				chunk_size_t nbytes_to_copy)
{
    chunk_size_t numbytes_to_copy;
	chunkmap_t *c2pv = NULL;
	chunk_id_t nextID;
	int ret = 0;
	int fetch_vblk_idx;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
#if 0
	__u16 start_offset_into_fetchpblk;
	struct C2P_tuple_t *dedup_c2p;
#endif
	__u16 start_offset_into_fetchvblk;
	struct C2V_tuple_t *deduped_c2v;

    nextID = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, chunkidx);
#ifdef NONSPANNING_PROVIDE
	/* If we are performing non-spanning runtime chunking for PROVIDED,
	 * there can be zero chunks of random length at the end of blocks.
	 * Though we do not note the length of each zero chunk, we can be 
	 * sure that if complete zero chunks exist, they have to have been
	 * created forcefully, when the block boundary was reached. This is
	 * because if block boundary was not reached, a stretch of zeroes
	 * by itself would never result in a zero chunk. So, if a zero
	 * chunk is to be fetched here, we can safely construct it manually 
	 * as a stretch of nbytes_to_copy # of bytes.
	 */
	if (nextID == 0)	/* zero chunk to be retrieved */
	{
		printf("fetch zero chunk of %u bytes at chunkidx=%u\n", 
				nbytes_to_copy, chunkidx);
    	return nbytes_to_copy;	/* # of bytes in zero chunk */
	}
#endif
    c2pv = getChunkMap(nextID);

	if (c2pv == NULL)
		printf("%s: chunkID=%u has c2pv NULL@chunkidx=%u, nbytes_to_copy=%u\n", 
				__FUNCTION__, nextID, chunkidx, nbytes_to_copy);
    assert(c2pv != NULL); /* Within loop, getChunkMap() should be success */

    ret = get_idx_into_vblklist(startoff_into_chunk, c2pv, &deduped_c2v, 
			NOT_PRESENT, &start_offset_into_fetchvblk, &fetch_vblk_idx);
	if (ret)
	{
		/* fatal error */
		VOID_ERR("fatal error in get_idx_into_vblklist\n");
		/* Error is indicated by zero (0) bytes copied */
		return 0;
	}

	/* Definition of numbytes_to_copy is different for 
	 * last chunk versus others 
	 */
    if (chunkidx == ulistLen(v2c->chunkIDUList) - 1)
    {
			/* Description from caller function: provideReadRequest()
			 * i) If we reached here, we are trying to fetch the
			 * initial part (only rem_bytes) of the last chunk
			 * belonging to a multi- or at least two-chunked vblk.
			 * ii) We can also reach here if the vblk maps within a single 
			 * chunk i.e. rem_bytes = BLKSIZE above.
			 */
		/* For the last (or only) chunk, fetch part of it => nbytes_to_copy 
		 * starting from the specified startoff_into_chunk */
#ifdef DEBUG_SS
        assert(nbytes_to_copy != 0);
#endif
        numbytes_to_copy = nbytes_to_copy;
    }
    else
    {
			/* Description from caller function: provideReadRequest()
			 * If we reached here, we are trying to fetch the whole
			 * of a chunk that lies within the single vblk being
			 * requested. This would be true of all chunks between
			 * the first and last chunks of a multi-chunked vblk.
			 * We can also reach here for fetching the last part 
			 * (only numbytes_accounted, as decided in get_partvblk)
			 * of the first chunk belonging to a multi- or at least
			 * two-chunked vblk.
			 */
		/* For the first chunk, copy only the last part whereas
		 * for intermediate chunks, copy whole 
		 */
#ifdef DEBUG_SS
        assert(nbytes_to_copy == 0);
#endif
        numbytes_to_copy = c2pv->clen - startoff_into_chunk;
    }

#if defined(SIM_BENCHMARK_STATS) && defined(NONSPANNING_PROVIDE)
	/* If we are here, most likely the metadata is good to go. In rare
	 * case, we might learn later that some subsequent chunks were dirty and
	 * we ended up discarding this too. But hopefully thats rare, and for 
	 * accounting purposes, let us be optimistic here, and count this one in.
	 */
    char bufhuman[HASHLEN_STR];
    MD5Human(c2pv->chashkey, bufhuman);

    fprintf(fhashptr, "maphitdedupchunkID = %u rw=%d ", c2pv->chunkID,
                                1);
    fprintf(fhashptr,
            "%s %u %x%x%x%x volID %u startblk %u startoff %u\n",
            bufhuman, c2pv->clen,
	        'd', 'u', 'm', 'y', 
			deduped_c2v->volID, fetch_vblk_idx, start_offset_into_fetchvblk);
#endif
    get_partchunk(preql, nreq, start_offset_into_fetchvblk, 
			numbytes_to_copy, deduped_c2v, fetch_vblk_idx);
    return numbytes_to_copy;
}

#ifndef NONSPANNING_PROVIDE
int form_postchunk(struct chunk_t **postchunkp, v2c_datum *v2c, 
					v2c_datum *first_v2c, chunkmap_t *last_c2pv, 
					chunkmap_t *first_c2pv, __u32 *postchunk_endblkID)
{
	int ret;
	__u16 start_offset_into_fetchvblk, end_offset_into_fetchvblk, pos;
	int fetch_startvblk_idx, fetch_endvblk_idx;
	C2V_tuple_t *deduped_c2v;
	__u32 postchunk_startblk, postchunk_endblk;

    if ((last_c2pv == NULL)) /* Case a of postchunk */
    {
        if (get_fullchunk(first_c2pv, postchunkp, postchunk_endblkID))
            RET_ERR("error in get_fullchunk\n");
#if 0
        *postchunkp = alloc_chunk_t(first_c2pv->clen);
        if (*postchunkp == NULL)
            RET_ERR("malloc failed for postchunk\n");

        memcpy(cdata(*postchunkp), outbuf, first_c2pv->clen);
#endif
    }
    else if (coinciding_end_boundary(v2c, last_c2pv))   /* Case b */
    {
        *postchunkp = NULL;
		*postchunk_endblkID = 0; /* dont care value */
    }
    else /* case c and d -->- non-coinciding_end_boundary */
    {
        /* Chunk last_c2pv could be straddled across 2 blocks or
         * straddled across multiple vblks. The below call of
         * get_idx_into_vblklist fetches the deduped_c2v for first_c2pv
         * and gives (fetch_startblk_idx, start_offset_into_fetchvblk)
         * for first_c2v->start_offset_into_chunk.
         */
        ret = get_idx_into_vblklist(first_v2c->start_offset_into_chunk,
                first_c2pv, &deduped_c2v, NOT_PRESENT,
                &start_offset_into_fetchvblk, &fetch_startvblk_idx);
        if (ret)
        {
            /* fatal error */
            RET_ERR("fatal error in get_idx_into_vblklist\n");
        }

        /* The next call of get_idx_into_vblklist uses same deduped_c2v as 
         * above and gives (fetch_endblk_idx, end_offset_into_fetchvblk)
         * for first_c2v->end_offset_into_chunk
         */
        ret = get_idx_into_vblklist(first_v2c->end_offset_into_chunk,
                first_c2pv, &deduped_c2v, PRESENT,
                &end_offset_into_fetchvblk, &fetch_endvblk_idx);
        if (ret)
        {
            /* fatal error */
            RET_ERR("get_idx_into_vblklist error\n");
        }

        postchunk_startblk = deduped_c2v->start_vblk_id + fetch_startvblk_idx;
        postchunk_endblk = deduped_c2v->start_vblk_id + fetch_endvblk_idx;

        /* Approximate allocation only, cropped later */
        *postchunkp = alloc_chunk_t(postchunk_endblk - postchunk_startblk + 1);

        if (loop_and_fetchdata_pblks(postchunkp, deduped_c2v->volID,
                        postchunk_startblk, start_offset_into_fetchvblk,
                        postchunk_endblk, end_offset_into_fetchvblk, &pos))
			RET_ERR("error in loop_and_fetchdata_pblks\n");

        /* Cropping the postchunk to exact size */
        *postchunkp = chunk_realloc(*postchunkp, pos + 1);
		
		/* Noting postchunk_endblkID for passing up to caller */
		*postchunk_endblkID = postchunk_endblk;
    }

	return 0;
}

/**  form_prechunk -- Retrieves the prechunk data into prechunkp
 *
 * Prechunk buffer can be due to following reasons:-
 * 1. In case of prev-block-before-first having coinciding boundary with
 *      its last chunk and the last chunk of prev-block-before-first is a i
 *      "forced" chunk and clen!=64K (this is the case that first vblk was 
 *      zero), then that chunk is "prechunk". 
 *      Last chunk of prev blk "forced", happens if first vblk was zero blk
 * 2. Else if first chunk of this vblk starts at some offset within
 *      previous vblk, then that much data within previous vblk
 *      forms the "prechunk".
 */
int form_prechunk(struct chunk_t **prechunkp,
			   		v2c_datum *prev_v2c, chunkmap_t *last_c2pv)
{
	__u32 prechunk_endblkID;
	__u16 start_offset_into_fetchvblk;
	int fetch_startvblk_idx;

    /* If prev-to-first-vblk had coinciding boundary but was a "forced" chunk,
     * then that whole chunk is prechunk.
     */
	if (coinciding_end_boundary(prev_v2c, last_c2pv) && 
			last_c2pv->cforced == 1) 						/* Case 1 */
    {
        if (get_fullchunk(last_c2pv, prechunkp, &prechunk_endblkID))
			RET_ERR("error from get_fullchunk\n");
    }
    /* OR
     * If prev-to-first-vblk did not have coinciding boundary,
     * then that part of chunk within previous vblk is prechunk.
     */
    else if (!coinciding_end_boundary(prev_v2c, last_c2pv)) /* Case 2 */
	{
		C2V_tuple_t *last_deduped_c2v;
		__u16 end_offset_into_fetchvblk;
		int fetch_endvblk_idx;
		__u32 prechunk_startblk;
		__u32 prechunk_endblk;
		int ret; 
		__u16 pos;

        /* Chunk last_c2pv could be within single vblk, straddled across 2 
         * or even straddled across multiple vblks. The below call of 
         * get_idx_into_vblklist simply fetches the deduped_c2v for last_c2pv 
		 * and gives (fetch_startblk_idx, start_offset_into_fetchvblk)
         * for the start (0) of the chunk last_c2pv.
         */
        ret = get_idx_into_vblklist(0, last_c2pv, &last_deduped_c2v,
            NOT_PRESENT, &start_offset_into_fetchvblk, &fetch_startvblk_idx);
        if (ret)
        {
            /* fatal error */
            RET_ERR("fatal error in get_idx_into_vblklist\n");
        }

        /* 3 cases wrt the last_c2pv chunk's straddling of blocks :-
         * (A) prev vblk and last_c2pv have coinciding boundaries
         * (B) chunk lies within prev vblk
         * (C) chunk straddles vblks prior to prev-vblk as well => huge
		  Below code is generic enough to include all 3 cases.
         */

		/* We want to get_idx_into_vblklist for the "pos" such that pos maps
		 * to the last byte of prev-vblk-before-first. Thus, in case of 
		 * a chunk straddling multiple vblks, we fetch deduplicated 
		 * "endvblk" corresponding to above "pos", and then do 
		 * datafetch for each vblk from startvblk to endvblk.
		 */
		ret = get_idx_into_vblklist(prev_v2c->end_offset_into_chunk,
                            last_c2pv, &last_deduped_c2v, PRESENT,
                            &end_offset_into_fetchvblk, &fetch_endvblk_idx);
        if (ret)
        {
        	/* fatal error */
            RET_ERR("fatal error in get_idx_into_vblklist\n");
        }

        /* Note in which vblk chunk starts and ends, in last_deduped_c2v */
        prechunk_startblk=last_deduped_c2v->start_vblk_id + fetch_startvblk_idx;
        prechunk_endblk = last_deduped_c2v->start_vblk_id + fetch_endvblk_idx;

        /* Approximate allocation only, cropped later */
        *prechunkp = alloc_chunk_t(prechunk_endblk - prechunk_startblk + 1);
        if (loop_and_fetchdata_pblks(prechunkp, last_deduped_c2v->volID,
                            prechunk_startblk, start_offset_into_fetchvblk,
                            prechunk_endblk, end_offset_into_fetchvblk, &pos))
			RET_ERR("error in loop_and_fetchdata_pblks\n");

        /* Cropping the prechunk to exact size */
        *prechunkp = chunk_realloc(*prechunkp, pos + 1);
	}

	return 0;
}
#endif

/* provideReadRequest -- PROVIDED functionality for every read request.
 * Assumes that num bytes requested is integral multiple of BLKSIZE.
 * This function intercepts a read request, perform chunk deduplication logic
 * and substitutes original read request with alternates (if dedup deems it)
 *
 * @blkReq[in]: Read request to be serviced
 * @totpreql[out]: Complete list of block requests resulting for this read
 * @totnreq[out]: Total number of block requests resulting for this read
 */
int provideReadRequest(struct vm_pkt *blkReq, 
				struct preq_spec **totpreql, int *totnreq)
{
	int ret;
	__u32 vBlkID;						/* blkid of block(s) being written */
	__u16 volID;						/* volid of blocks(s) being written */
	v2c_datum *v2c;						/* V2C of blocks(s) being written */
	int chunkidx = 0;					/* to iterate through chunks */
	chunk_size_t rem_bytes=0;			/* to track remaining bytes */
	chunk_size_t startoff_into_chunk;	/* starting offset into chunk */
	int i = 0, j = 0, rc;				/* iterators */
	struct slist_head v2clist;			/* list of V2C for block(s) to write */
	chunk_size_t numbytes_accounted = 0;/* tracking how much of single block
										 * accounted for, while redirecting
										 */
	int numreq = 0;						/* to track number of blocks resulting
										 * for this vblk
										 */
	int *nreq = &numreq;
	struct preq_spec *pl = NULL;		/* list of block requests resulting
										 * for this vblk
										 */
	struct preq_spec **preql = &pl;

#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

#ifdef DEBUG_SS
	assert(blkReq != NULL);
	assert(totpreql != NULL && *totpreql == NULL);

 	/* Assumes that num bytes requested is integral multiple of BLKSIZE */
	assert(blkReq->nbytes % BLKSIZE == 0);
#endif

	if (disksimflag)
		assert(blkReq->nbytes == BLKSIZE);

	vBlkID = getVirtBlkID(blkReq) - 1;	//incremented in loop below
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	volID = (__u16) ret;
	ret = 0;

	/* For each block in the multi-block request */
	for (i=0; i<(int)blkReq->nbytes/BLKSIZE; i++)
	{
        assert(i==0 ||
                (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
				 !DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY && 
                 !DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY));
                /* if disk simulation or collect format, do not expect
                 * multi-block requests, only single block at a time for now.
                 */
		vBlkID++;
		*nreq = 0;
		rem_bytes = BLKSIZE;

	    /* Get V2C map of current blockID.
		 * Since fetch is only for 1 vblk => if it is zeroblk, then v2c==NULL 
		 */
		INIT_LIST_HEAD(&v2clist);
	    rc = getVirttoChunkMap(volID, vBlkID, 1, &v2clist);
		if (!rc)
		{
			/* V2C found but check its dirty-flag */
#ifdef DEBUG_SS
			assert(slist_len(&v2c_list) == 1);
#endif
		    v2c = slist_entry(slist_first(&v2clist), v2c_datum, head);
//			free(v2clistp);	dont do this!!!
			if (cvblk_dirty(v2c))
			{
				/* Since dirty, can not be used
				 */
				numbytes_accounted = 0;
				vmapdirty_flag = 1;
				if (!warmupflag)
					vmap_dirties++;
			}
			else
			{
				int numchunks = ulistLen(v2c->chunkIDUList);
				if (*nreq < 0)
					*nreq = 0;

				/* The below code snippet has all cases of vblk-chunk mappings:
				 * 1. vblk lies within 1 chunk
				 * 2. vblk straddles 2 chunks
				 * 3. vblk straddles multiple chunks
				 *
				 * And, for each of above chunks, reqd data to be read from 
				 * chunk would be present in 1 of 2 ways:-
				 * 1. to-be-read data is within 1 vblk => within 1 pblk
				 * 2. to-be-read data straddles 2 vblks => straddles 2 pblks
				 * Every chunk maps to consecutive vblks, though corresponding 
				 * pblks may or may not be sequential.
				 */
				for(chunkidx=0; chunkidx < numchunks; chunkidx++)
				{
					/* Definition of startoff_into_chunk is different for 
					 * first chunk versus others 
					 */
					if (chunkidx == 0)
					{
						/* While starting off with first chunk of this vblk,
						 * mark BLKSIZE byte remain to be fetched for this vblk.
						 * Also, vblk can start off at any offset into 1st chnk,
						 * mark that offset as startoff_into_chunk
						 */
						rem_bytes = BLKSIZE;
						startoff_into_chunk = v2c->start_offset_into_chunk;
					}
					else
					{
						/* For all chunks other than first one, 
						 * the new startoff_into_chunk is guaranteed to be 0
						 * because these are intermediate chunks or last chunk
						 * in a multi- or at least two-chunked vblk.
						 */
						startoff_into_chunk = 0;
					}
			
#ifdef DEBUG_SS
					assert(rem_bytes > 0);	
#endif	
					if (chunkidx == ulistLen(v2c->chunkIDUList) - 1)
					{
						/* i) If we reached here, we are trying to fetch the
						 * initial part (only rem_bytes) of the last chunk
						 * belonging to a multi- or at least two-chunked vblk.
						 * ii) We can also reach here if the vblk maps within a 
						 * single chunk i.e. rem_bytes = BLKSIZE above.
						 */
//fprintf(stdout, "get_partvblk 1\n");
						numbytes_accounted = get_partvblk(preql, nreq, v2c, 
							chunkidx, startoff_into_chunk, rem_bytes);
						rem_bytes -= numbytes_accounted;
					}
					else
					{
						/* If we reached here, we are trying to fetch the whole
						 * of a chunk that lies within the single vblk being
						 * requested. This would be true of all chunks between
						 * the first and last chunks of a multi-chunked vblk.
						 * We can also reach here for fetching the last part 
						 * (only numbytes_accounted, as decided in get_partvblk)
						 * of the first chunk belonging to a multi- or at least
						 * two-chunked vblk.
						 */
//fprintf(stdout, "get_partvblk 2\n");
						numbytes_accounted = get_partvblk(preql, nreq, v2c, 
							chunkidx, startoff_into_chunk, 0);
						rem_bytes -= numbytes_accounted;
					}
	
				}/* loop over the chunks comprising a single vblk */
			}/* case of good (non-zero) vblk where v2c exists */
		}
		else
			numbytes_accounted = 0;	//since no mapping found

		if (rc == ZEROBLK_FLAG)	//from above getVirttoChunkMap call
		{
			numbytes_accounted = BLKSIZE;	//no disk fetch since zeroblk
			rem_bytes = 0;
			*nreq = 0;
#ifdef PRO_STATS	
	if (!warmupflag) {
			pro_zeroblksread++;
			ptotalblk++;
	}
#endif
			//dont return here, (like CONFIDE) this is only 1 blk in multiblk!
		}
		else if (rc && !runtimemap)
		{
			RET_ERR("Couldnt find V2C map for specified read req (%u, %u)\n",	
					volID, vBlkID);
		}
		else
		{
		    /* If any of the chunks are dirty, we need to fetch 
			 * pblk instead of going through with chunk mappings. And, 
			 * break out of this loop. 
			 */
		    if (numbytes_accounted== 0)	/* shows no alternatives found */
			{
				/* Free any preql if already allocated, else ignore 
				 * This might happen if initial chunks were non-dirty, but
				 * later ones are dirty. Relatively unlikely, rare 
				 * occurrence. 
				 */
				if (*preql != NULL)
				{
					for (j=0;j<*nreq;j++)
						if((*preql+j)->content)
							free((*preql+j)->content);
					free(*preql);
				}
				*nreq = 0;
		
				if (elongate_preql(preql, nreq))
				{
					RET_ERR("realloc error for preql\n");
				}
//fprintf(stdout, "create_preq_spec(%u, %u)\n", volID, vBlkID);
				create_preq_spec(volID, vBlkID, 
							BLKSIZE, blkReq->rw, blkReq->content, 
							0, BLKSIZE-1, *preql+(*nreq-1));
//fprintf(stdout, "create_preq_spec(%u, %u) done\n", volID, vBlkID);
				rem_bytes -= BLKSIZE;
				if (!warmupflag)
					vmap_misses++;
		    }	
			else
			{
				if (!warmupflag)
					vmap_hits++;
				vmaphit_flag = 1;
			}
		}	/* case of good (non-zero) vblk where v2c exists */

		/* Append above list to the total list of blocks */
		if (*preql != NULL)
		{
			for (j=0;j<*nreq;j++)
			{
				if (*totnreq < 0)
					*totnreq = 0;
				if (elongate_preql(totpreql, totnreq))
				{
					RET_ERR("realloc error for totpreql\n");
				}
				copy_preq_spec(*preql+j, *totpreql+(*totnreq-1));
				free((*preql+j)->content);
			}
			free(*preql);
			*preql = NULL;
		}

#ifdef PRO_STATS	
		if (!warmupflag) {
		ptotalblk += (*nreq);
		ptotalblkread +=(*nreq);
		}

        if (numbytes_accounted==0)
        {
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
            fprintf(stdout, "PROVIDED could not get alternates\n");
#endif
            pro_fallback_blkread++;
        }
        else if (*nreq==0)
        {
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
            fprintf(stdout, "PROVIDED just serves zeroblk!\n");
#endif
		}
		else
		{
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
			fprintf(stdout, "PROVIDED got alternates!\n");
#endif
            pro_blkread += (*nreq);
        }
#endif
		/* Whether alternative or not, rem_bytes should be 0, else error */
        if (rem_bytes > 0)
        {
            RET_ERR("fatal error in provideReadRequest\n");
        }

	}	/* loop for multiple vblks requested within single vm_pkt */

#ifdef PRO_STATS	
	if (!warmupflag) {
	ptotalreq++;
	ptotalreadreq++;
	porigblkread += (blkReq->nbytes/BLKSIZE);
	}
#endif

	/* Problem with above chunk-by-chunk fetching of buffer data for vblk 
	 * is that, a single vblk that straddles multiple chunks, can have 
	 * each chunk mapping to different pblks or same. Let us consider 
	 * both cases, one-by-one.
	 * In the worst case, number of chunks for a single vblk can be 9.
	 * Case I: If all 9 chunks have different pblk marked as its 
	 * dedup mapping => 9 pblk fetches to satisfy a single vblk read 
	 * request! This can lead to unpredictable performance in the long run.
	 * Case II: If all 9 chunks have same pblk marked as its 
	 * dedup mapping => block will be fetched from cache 9 times. Compile
	 * all requests such that only 1 block will end up being fetched. DONE.
	 */
	return 0;
}

void* p_mapupdate_sub(void *arg)
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

	stime = gettime(); /* START PROVIDED map-update for write time */
	rc = p_mapupdate(preql, blkReq, vop_iter);
	if (rc < 0)
		fatal(NULL, ERR_USERCALL, "Error in f_mapupdate\n");

    etime = gettime();  /* END PROVIDED map-update for write time */
    ACCESSTIME_PRINT("provided-map-update-for-write time: %llu %d\n",
                     etime - stime, vop_iter);

	return NULL;
}


/* This is PROVIDED functionality for every write request.
 * This function intercepts a write request, updates mapping tables and 
 * returns control. It is assumed that a single write request received 
 * here could potentially have write content for multiple blocks, 
 * indicated by "bytes" within struct vm_pkt.
 *
 * ################Begin side-note:
 * For optimization sakes, can add h (ending Rabin fingerprint of chunk)
 * If h satisfies the valid chunk boundary criteria, it implies that
 * the chunk is a "valid" one. Else, it could be one of the following
 * cases:
 * a. Chunk formed because next vblk was zero block or EOF
 * b. Chunk formed because no boundary found and clen == 64K
 * The h value can be used to enable "resumePrintBuilding" so that we
 * do not end up chunking and re-chunking the same data repeatedly
 * just to find the boundary.
 * However, finally to perform dedup check need to have the MD5 hash
 * of entire chunk and for this, we need to have data of entire chunk.
 * A future optimization could be dropping the use of MD5 hash and 
 * using just the Rabin fingerprint over entire block to identify
 * unique blocks, and in this case, can use the stored "h" to compute
 * chunk fingerprint without fetching whole content! IF this optimization
 * materializes, we can go ahead and implement p_mapupdate() for runtime.
 * ################End side-note
 * 
 * Basic function:
 * 
 * Split into 2 possibilities:-
 * ----------------------------
 * Possibility I: Existing block being over-written
 * Possibility II: New block to write (earlier pointed to zero chunk/block)
 *
 * In description below, though implementation is
 * generic to apply to multiple (non-zero) vblks being written, but
 * it doesnt handle special case of previous zero and non-zero vblks
 * being over-written together. It can handle only all zero vblks or
 * all non-zero vblks being written.
 *
 * For Possibility I:
 * 1. The first vblk being written may already have a chunk mapping 
 * 		starting at offset 0. If so, then no previous chunk needs to 
 * 		be fetched (unless it is a "forced" boundary case mentioned
 * 		in Possibility II below).
 * 		However, if the start boundaries do not coincide, it implies
 * 		that the prev blk's last chunk ends into this vblk that is
 * 		being written. So, that chunk's data has to be fetched
 * 		into the buffer to-be-chunked => prechunk
 * 2. Similarly, the last vblk being written may already have a 
 * 		chunk mapping ending at offset BLKSIZE-1. If so, then no next
 * 		chunk needs to be fetched (unless it is a "forced" boundary
 * 		case due to the next vblk being a zero block).
 * 		However, if the end boundaries do not coincide, it implies
 * 		that this last vblk's last chunk ends into the next vblk.
 * 		So, that chunk's data has to be fetched into the buffer
 * 		to-be-chunked => postchunk
 * 		If any "forcing" results in pre- or post-chunks, then corresponding
 * 		chunkID x also needs recyclechunkID(x).TODO: add this in createPrePostchunkBufs().
 * 		Note the "sequentially next" chunk at this time.
 * Note: 1 and 2 are done in createPrePostchunkBufs()
 * 3. mappingTrimScan() to scan through the mappings of all vblks
 * 		being written, and for every vblk initAndRecoverChunkIDs()
 * 		to initialize all its chunks-related elements and for every
 * 		chunkID x in its chunkIDUList, recyclechunkID(x)
 * Note: newReusableChunkIDUList list should be accessed via some lock only.
 * Note: Assert that first vblk in V2C tuple is same as vblk being handled.
 * 4. Sandwich the write buffer between prechunk and postchunk to form
 * 		the chunking buffer
 * Note: 4 is done via createInitialChunkBuf() <-- perfWriteChunking()
 * 5. perfWriteChunking() on the chunking buffer
 * 6. After chunking, the beginning boundary of sequentially next
 * 		chunk might change depending upon the chunking process
 * 		if no chunk boundary is found when the chunking buffer finishes.
 * 		If so, implies that existing chunk (say chunkID x) will 
 * 		be replaced with new chunk (with new chunkID y). 
 *		Note v2c mapping for the next after last block for later use.
 * 		recyclechunkID(x)
 *		Create new chunk y, using above-noted v2c mapping.
 *
 *
 * For Possibility II:
 * 1. Since the first vblk is a new block being written (was zero block
 * 		previously), so it is possible that the last chunk of previous
 * 		vblk had a "forced" boundary and not a content-based boundary.
 * 		Fetch that chunk => prechunk
 * 2. If last blk being written was previously a zero block but the 
 * 		block right after it was not, then the first chunk of that 
 * 		existing block was by circumstance. Its starting boundary can
 * 		now change. Fetch that chunk => postchunk
 * 		If any "forcing" results in pre- or post-chunks, then corresponding
 * 		chunkID x also needs recyclechunkID(x).	TODO
 * Note: 1 and 2 are done in createPrePostchunkBufs()
 * 3. As above, except, in mappingTrimScan(), no chunkIDs to be recycled 
 * 		because all previous zero blocks would just map to zero chunk. 
 * 		Just, free() any elements that had been previously malloc'ed.
 * 4. In current (first-cut?) implementation of Possibiliity II, it 
 * 		is assumed here that ALL blocks being written here were previously 
 * 		zero blocks. So, there are no other "old chunks" to deal with. Simply 
 * 		sandwich the write buffer between prechunk and postchunk to form 
 * 		the chunking buffer.
 * Note: 4 is done via createInitialChunkBuf() <-- perfWriteChunking()
 * 5. As above
 * 6. As above.
 *
 * Output for write request are corresponding redirected vblk requests.
 * Any reads that have to be done for re-chunking are the responsibility 
 * of PROVIDED itself, and are not part of its output.
 *
 * @blkReq[in}: Input request --- single blk or multi-blk
 * @preql[out]: Output preq_spec packet list
 * @nreq[out]: Number of output packets
 * @return: status
 */
int provideWriteRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq)
{
	int ret = 0;
#ifndef NONSPANNING_PROVIDE
	struct chunk_t *prechunk = NULL;
	struct chunk_t *postchunk = NULL;
	struct chunkmap_t *seqnext = NULL;
	__u32 numBlocks;
#endif
	__u32 blkID;
	unsigned char *buf = NULL;
	unsigned long long stime=0, etime=0;

#ifdef DEBUG_SS
	assert(blkReq != NULL);
	assert(preql != NULL && *preql == NULL);
#endif

	if (disksimflag)
		assert(blkReq->nbytes == BLKSIZE);

	*nreq = 0;
	__u32 firstvBlkID = getVirtBlkID(blkReq);
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	__u16 volID = (__u16) ret;
	ret = 0;

#ifndef NONSPANNING_PROVIDE
    numBlocks = getNumBlocks(blkReq);
#endif
	//FIXME: For "efficient" runtime design, do not use pre- and post- buffers.
	//		resumeDynChunking() should just use the ending Rabin 
	//		fingerprint, just before the written block(s). Need to store 
	//		above ending fingerprint in metadata. But would also need to discard
	//		MD5 usage and use Rabin fingerprints themselves for dedup
	//		identification. This is feasible only if we use different 
	//		irreducible polynomials for different blocks of data (preferably
	//		randomly chosen but we cant afford randomness since we need to
	//		identify duplicates.) So we need to use multiple repeatable 
	//		irreducible polynomials and then mathematically compute Rabin
	//		fingerprint of entire chunk when we have only the Fingerprints of
	//		the chunk's sub-parts.

#ifndef NONSPANNING_PROVIDE
	/* Since the block being written may be part of some chunk in front of
	 * it and some chunk behind it, so we need to fetch those data snippets
	 * so that we can get the MD5 of whole chunks after Rabin chunking this
	 * new block. The snippet from start-of-chunk-before-this to the byte
	 * before this new block is called pre-chunk and the snippet from
	 * byte after this new block to byte at end-of-chunk-after-this is called
	 * the post-chunk. More detailed description in the comments section
	 * of createPrePostchunkBufs().
	 */	
	stime = gettime();  /* START PROVIDED get-pre-post-chunks time */
    createPrePostchunkBufs(&prechunk, &postchunk, volID, 
					firstvBlkID, numBlocks, &seqnext);
	etime = gettime();  /* END PROVIDED get-pre-post-chunks time */
    ACCESSTIME_PRINT("provided-get-pre-post-chunks time: %llu %d rw(%d)\n",
         etime - stime,
         blkReq->nbytes/BLKSIZE,
         blkReq->rw);
#endif

	/* Get data buf from blkReq since it is a write request */
	if (retrieveBuf(&buf, blkReq))
		RET_ERR("retrieveBuf() error'ed\n");

	stime = gettime();  /* START PROVIDED map-invalidate time */
	/* Scan through and reset mappings of all vblks being written */
	if (mappingTrimScan(blkReq))
		RET_ERR("mappingTrimScan() error'ed\n");
	etime = gettime();  /* END PROVIDED map-invalidate time */
    ACCESSTIME_PRINT("provided-invalidate time: %llu %d rw(%d)\n",
         etime - stime,
         blkReq->nbytes/BLKSIZE,
         blkReq->rw);

#ifndef NONSPANNING_PROVIDE
	//FIXME: Should actually be done later via thread near f_mapupdate_sub()
	//		But temporarily done here for simulation, since coding was ready.
	//		Once above runtime design is done, may be easier to move this code.
	//FIXME: Would also need to consider locking mechanisms to avoid multiple
	//		writes overriding each other.....
	/* Chunk writes and update mappings */
	stime = gettime();  /* START PROVIDED provided-map-update-for-write time */
	ret = perfWriteChunking(buf, blkReq->nbytes, volID, firstvBlkID, 
			&prechunk, &postchunk, &seqnext);
	etime = gettime();  /* END PROVIDED provided-map-update-for-write time */
    ACCESSTIME_PRINT("provided-map-update-for-write time: %llu %d rw(%d)\n",
         etime - stime,
         blkReq->nbytes/BLKSIZE,
         blkReq->rw);
#endif

	/* Consider multi-block writes */
	*nreq = 0;
	blkID=firstvBlkID;
#ifdef NONSPANNING_PROVIDE
    assert(DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
            DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
    if (disksimflag)
        assert(blkReq->nbytes == BLKSIZE);
#else	
	assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY);
#endif

	/* creating output packets that will actually write to disk */
	while (*nreq < (int)blkReq->nbytes/BLKSIZE)
	{
		/* Create to-be-executed requests */
		if (elongate_preql(preql, nreq))
		{
			RET_ERR("realloc error for preql in provideWriteRequest\n");
		}
		create_preq_spec(volID, blkID, 
					BLKSIZE, blkReq->rw, blkReq->content + (*nreq-1)*BLKSIZE, 
					0, 0, *preql+(*nreq-1));
	}

	free(buf);

#ifdef PRO_STATS
	if (!warmupflag) {
	ptotalreq++;
	ptotalwritereq++;
	ptotalblk += (*nreq);
	ptotalblkwrite += (*nreq);
	porigblkwrite += (*nreq);
	}
#endif

#ifdef SIMREPLAY_DEBUG_SS
    assert(*nreq == (int)blkReq->nbytes/BLKSIZE);
#endif	
	return ret;
}

