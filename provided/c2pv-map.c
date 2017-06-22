#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include "chunking.h"
//#include "chunking-prototypes.h"
#include "chunktab.h"
#include "c2pv-map.h"
#include "v2p-map.h"
#include "debug.h"
//#include "rabin-prototypes.h"
#include "rabin.h"
#include "serveio.h"
#include "serveio-utils.h"
#include "utils.h"
#include "vector32.h"
#include "pdd_config.h"
#include "content-simfile.h"
#include "unused.h"
#include "simdisk-API.h"
#include "replay-defines.h"
#include "content-gen.h"

#if defined(PROMAPPING_TEST) || defined(PROCHUNKING_TEST) || defined(PDD_REPLAY_DEBUG_SS)
	extern FILE * fhashptr;
#endif

__u32 dedupfactor[10] = {0};
__u32 max_dedupfactor = 0;
__u32 pcollisions = 0;
__u32 pzerocollisions = 0;
__u32 pcollisionstp = 0;
__u32 pcollisionsdiffclen = 0;
int chunkmap_alive = 0;
/* ID 0 indicates zero block/chunk and 
 * ID 1 is DUMMY_ID
 */
chunk_id_t chunkNum = 2;

/* Globals to be used during write chunking, in processChunk() */
__u16 iolastBlkOffsetminus1 = 0;        /* can not be -1 */
__u32 ioprevBoundaryBlkNum = 0;

Node * currReusableChunkIDUList = NULL;
//pthread_mutex_t currReusable_mutex;
extern struct chunktab chunktab;
extern const char zeroarray[65537];
extern int disksimflag;	/* read req have content */
extern int runtimemap;
extern int preplayflag;

/* This is vector indexed by chunkID, pointing to chunks 
 * in the hashtable structure */
vector32 * chunkmap_by_cid = NULL;

/* create_chunkmap_mapping_space: Use this to initialize chunkmap 2D vector
 * before starting off pro_scan_and_process or pdd_preplay threads
 */
void create_chunkmap_mapping_space(void)
{
#ifdef PDDREPLAY_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	if (chunkmap_alive == 1)
	{
		fprintf(stdout, "chunkmap already non-NULL\n");
		return;
	}
	chunkmap_alive = 1;
	
	/* Initialize c2pv mapping space */
	chunkmap_by_cid = calloc(1, sizeof(vector32));
	if (chunkmap_by_cid == NULL)
		VOID_ERR("couldnt malloc chunkmap_by_cid\n");
	vector32_init(chunkmap_by_cid);
	vector32_resize(chunkmap_by_cid, 40000000);

	/* Initialize chunktab */
	if (chunktab_init(&chunktab, CHUNKTAB_SIZE))
	{
		VOID_ERR("chunktab_init failed\n");
		chunktab_exit(&chunktab);
	}
	return;
}

void free_chunkmap(void)
{
    __u32 i;        /* For iterating over chunkID */
    chunkmap_t *ptri;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	if (chunkmap_alive == 0)
	{
		fprintf(stdout, "chunkmap is not alive, exit\n");
		return;
	}
	chunkmap_alive = 0;

    for (i = 0; i < vector32_size(chunkmap_by_cid); i++)
    {
        if ((ptri = (chunkmap_t*) vector32_get(chunkmap_by_cid,i)) != NULL)
		{
            //free(ptri); /* Freeing chunkmap node done in chunktab_exit */
			int j;
			struct slist_head *p;
			struct C2V_tuple_t *c2v;
			int len = slist_len(&ptri->c2vmaps);
			for (j=0; j<len;j++)
			{
				p = slist_first(&ptri->c2vmaps);
				c2v = slist_entry(p, struct C2V_tuple_t, head);
				slist_del(&c2v->head, &ptri->c2vmaps);
				free(c2v);
			}
#ifdef DEBUG_SS
			assert(slist_empty(&ptri->c2vmaps));
#endif
		}
    }
	free(chunkmap_by_cid->data);
	free(chunkmap_by_cid);

	/* Free chunktab */
	chunktab_exit(&chunktab);
	fprintf(stdout, "done now\n");
}	

/* notzero_chunk: Check whether the chunk specified by c2pv chunkmap
 *      is a zero chunk i.e. mapped by a zero vblk.
 * @c2pv[in]: specified chunkmap
 * @return : TRUE(1) or FALSE (0)
 */
int notzero_chunk(chunkmap_t *c2pv)
{
    /* If the chunk is a zero chunk, it will have chunkID = 0 */
    if (0 == c2pv->chunkID)
        return 1;
    else
        return 0;
}

#if 0
/* chunkstart_coincides: Checks whether the start boundary of
 *      chunk coincides with its corresponding vblk. Assumes that
 *      chunkmap is not of "zero" chunk. Difference from the call
 *      coinciding_start_boundary is that this one uses only 
 *      the chunkmap (no v2c) to check this.
 * @c2pv[in]: chunkmap of the chunk 
 * @return : TRUE (1) or FALSE (0)
 */
int chunkstart_coincides(chunkmap_t *c2pv)
{
    assert(c2pv->chunkID != 0);
    if (c2pv->start_offset_into_vblk == 0)		//FIXME
        return 1;
    else
        return 0;
}
#endif

/* chunkend_coincides: Checks whether the end boundary of chunk 
 *      coincides with its corresponding vblk. Assumes that 
 *      chunkmap is not o "zero" chunk. Difference from the call
 *      coinciding_end_boundary is that this one uses the
 *      chunkmap and v2c to check this.
 * @c2pv[in]: chunkmap of the chunk 
 * @return : TRUE (1) or FALSE (0)
 */
int chunkend_coincides(chunkmap_t *c2pv, C2V_tuple_t *c2v)
{
    assert(c2pv->chunkID != 0);
    if ((c2v->start_offset_into_vblk + c2pv->clen) == BLKSIZE)
        return 1;
    else
        return 0;
}

/* In scanning phase, chunkIDs only keep increasing. 
 * In the online (IO) phase, need to use currReusableChunkIDList[] as well. 
 * However, the global chunkNum variable still needs atomic
 * increment and fetch, since it is use in the online (IO) phase as well.
 */
//FIXME: Would the chunkNum counter rollover?
chunk_id_t getNextChunkNum(int initflag)
{
    chunk_id_t c;
	//UNUSED(initflag);

#if 1
    chunk_id_t *cNum, val;
	/* We want to re-use IDs in real system, but not here in simulation */
    if (initflag == NOINIT_STAGE)   /* online phase */
    {
        //pthread_mutex_lock(&currReusable_mutex);
        if (ulistLen(currReusableChunkIDUList))
        {
            cNum = (chunk_id_t*)popUList(&currReusableChunkIDUList);
			val = *cNum;
			free(cNum);
            return val;
        }
        //pthread_mutex_unlock(&currReusable_mutex);
    }
#endif

    /* If you are here, one of the following is true:-
     * 1. empty currReusableChunkIDUList in online phase, or
     * 2. scanning phase
     *
     * Use this to stay atomic
     * uint64_t atomic_inc_64_nv(volatile uint64_t *target);
     * http://www.cognitus.net/html/tutorial/usingVolatile.html
     */
    c = __sync_add_and_fetch(&chunkNum, 1);
    return c;
}

void note_chunk_attrs(c2pv_datum *c2pv, struct chunk_t *chunk,
                unsigned char *key, chunk_id_t chunkID)
{
    /* If HASHMAGIC is disabled, MAGIC_SIZE = 0, so wont get copied anyway */
    memcpy(c2pv->chashkey, key, HASHLEN + MAGIC_SIZE);
    c2pv->clen = csize(chunk);

    c2pv->chunkID = chunkID;
}

void note_c2v_tuple(C2V_tuple_t *c2vp, __u16 volID,
                __u32 prevBoundaryBlkNum,
                    __u16 lastBlkOffsetminus1)
{
    c2vp->volID = volID;
    c2vp->start_vblk_id = prevBoundaryBlkNum;
    c2vp->start_offset_into_vblk = inc_blkoffset(lastBlkOffsetminus1);
}

void add_c2v_tuple_to_map(C2V_tuple_t *c2vt, c2pv_datum *c2pv)
{
    slist_add(&c2vt->head, &c2pv->c2vmaps);
}

void remove_c2v_tuple_from_map(C2V_tuple_t *c2v, c2pv_datum *c2pv)
{
	C2V_tuple_t *c = c2v;
    slist_del(&c2v->head, &c2pv->c2vmaps);
	free(c);
}

void setChunkMap(chunk_id_t chunkID, c2pv_datum *c2pv)
{
	vector32_set(chunkmap_by_cid, chunkID, (void*)c2pv);
}

/* Retrieve chunk mapping for specified chunkID */
//struct chunkmap_t* getChunkMap(chunk_id_t chunkID, __u16 volID, __u32 vBlkID)
struct chunkmap_t* getChunkMap(chunk_id_t chunkID)
{
    /* Using the chunkID, need to retrieve the chunkmap. For this,
     * we use the chunkmap_by_cid vector.
     */
    return (chunkmap_t*) vector32_get(chunkmap_by_cid, chunkID);
}


/** Processing a chunk boundary.
 *
 * Chunk dedup detection is to be done in provided module 
 * The chunkID is present in the mapping, and in the virt-to-chunk mapping, we 
 * should point to chunks by chunkID.
 *
 * @param c 
 * @param leftover_len
 * @param newBoundaryBlkNum
 * @param initflag
 * @return Return chunk num (chunkID) so it can be added to vblk chunk list
 */
chunk_id_t processChunk(
                struct chunk_t *c, /* chunk to be processed != NULL */
                __u16 leftover_len, /* length of leftover data <= BLKSIZE */
                __u32 newBoundaryBlkNum, /* blk in which chunk found */
                __u16 volID,        /* blk belongs to which volume */
                int initflag,  /* online phase or scanning phase */
                __u16 blklen,    /* len of block (special significance in I/O) */
                int lastblk_flag, /* for resetting static vars */
				int rw_flag)	/* for collecting stats */
{
	int ret = 0;
    c2pv_datum *c2pv = NULL;
    struct C2V_tuple_t *c2v;
	chunkmap_t *dedupc2pv;
#ifdef STRICT_NO_HASH_COLLISION
	__u32 oldendblkID;
	//savemem unsigned char debugkey[HASHLEN + MAGIC_SIZE];	/* only for debug */
	unsigned char *debugkey = malloc(HASHLEN + MAGIC_SIZE);
	struct chunk_t **oldchunk = malloc(sizeof(struct chunk_t));
#endif
    //savemem unsigned char key[HASHLEN + MAGIC_SIZE];
	unsigned char* key = malloc(HASHLEN + MAGIC_SIZE);

    static __u16 lastBlkOffsetminus1 = BLKSIZE - 1;    /* can not be -1 */
    static __u16 iolastBlkOffsetminus1 = BLKSIZE - 1;  /* can not be -1 */
    static __u32 prevBoundaryBlkNum = 0;

    /* These are pointers to the static variable or global variables above */
    __u16 *lastBlkOffsetminus1p;
    __u32 *prevBoundaryBlkNump;
    if (initflag == INIT_STAGE)
    {
        lastBlkOffsetminus1p = &lastBlkOffsetminus1;
        prevBoundaryBlkNump = &prevBoundaryBlkNum;
    }
    else
    {
        lastBlkOffsetminus1p = &iolastBlkOffsetminus1;
        prevBoundaryBlkNump = &ioprevBoundaryBlkNum;
        if (lastblk_flag == PRECHUNK_PARTIAL_FIRSTBLK)
        {
            *lastBlkOffsetminus1p = BLKSIZE - blklen;
            *prevBoundaryBlkNump = newBoundaryBlkNum;
        }
		if (lastblk_flag == ONLYBLK)
			*prevBoundaryBlkNump = newBoundaryBlkNum;
    }

    /* If first block encountered for first time, offset = 0
     * else this may be second/later chunk in first block, then offset >= 0
     */
    if (lastblk_flag==SCAN_FIRSTBLK && *prevBoundaryBlkNump != 0)/*first chunk*/
    {
        *prevBoundaryBlkNump = 0;
        *lastBlkOffsetminus1p = BLKSIZE - 1;
    }

	/* This is added to just update chunk offsets for the next time, when
	 * a zero block is encountered, which generates no chunk and hence offsets
	 * become stale by the time next chunk is encountered. Do nothing else.
	 * Return value is also immaterial.
	 * However, if this zero block is also the lastblk of volume (ULTIMATE_LASTBLK)
	 * then *prevBoundaryBlkNump needs to be set to 0. Since both flags 
	 * ULTIMATE_LASTBLK and JUST_UPDATE_NEXT_CHUNKOFFSETS can not be sent via
	 * same variable lastblk_flag, hence we rely on getLastvBlk() to find 
	 * whether this is the last block.
	 */
	if (lastblk_flag == JUST_UPDATE_NEXT_CHUNKOFFSETS)
	{
		__u32 lastblk;
        if (getLastvBlk(volID, &lastblk))
            RET_ERR("volume ID %u has no info\n", volID);
		if (newBoundaryBlkNum == lastblk)
		{
#ifdef SIMREPLAY_DEBUG_SS
			fprintf(stdout, "JUST_UPDATE_NEXT_CHUNKOFFSETS at ULTIMATE_LASTBLK\n");
#endif
			*prevBoundaryBlkNump = 0;
		}
		else
	    	*prevBoundaryBlkNump = newBoundaryBlkNum+1;
    	*lastBlkOffsetminus1p = BLKSIZE - 1;
		return 0;
	}


	/* This can be true for every first scan block of every VM, and 
	 * also for every first FULL block of a write request 
	 */
	if (lastblk_flag != PRECHUNK_PARTIAL_FIRSTBLK &&
		(lastblk_flag & FIRSTBLK) != 0)
	{
		*lastBlkOffsetminus1p = BLKSIZE - 1;
		*prevBoundaryBlkNump = 0;
	}

    assert(c != NULL);

    /* Hash(+magic) the chunk */
    getHashKey(cdata(c), csize(c), key);
#if defined(PROMAPPING_TEST) || defined(PROCHUNKING_TEST) || defined (PDD_REPLAY_DEBUG_SS)
    //WHERE;
    char bufhuman[HASHLEN_STR];
    MD5Human(key, bufhuman);
#endif

    /* Note the C2V tuple for this chunk */
    c2v = calloc (1, sizeof(C2V_tuple_t));
    note_c2v_tuple(c2v, volID, *prevBoundaryBlkNump, *lastBlkOffsetminus1p);

    /* trying to identify whether found chunk is dedup or not  */
    dedupc2pv = (c2pv_datum*) hashtab_search(chunktab.table, key);
    if (dedupc2pv) /* hash matched, check if hash-collision */
    {
#ifdef STRICT_NO_HASH_COLLISION
		/* Before assuming that a hash-value match indicates content match,
		 * we need to check the actual content as well. So, fetch content
		 * of dedupc2pv chunk and compare with content of input chunk c
		 */
#if defined(PDD_REPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
		/* For debugging only.... comment later */
		*oldchunk = NULL;
        if (get_fullchunk(dedupc2pv, oldchunk, &oldendblkID))
            RET_ERR("error in get_fullchunk\n");
		getHashKey(cdata(*oldchunk), csize(*oldchunk), debugkey);
		assert(memcmp(debugkey, dedupc2pv->chashkey, HASHLEN) == 0);
		assert(memcmp(debugkey, key, HASHLEN) == 0);
		*oldchunk = free_chunk_t(oldchunk);
#endif

		if (memcmp(zeroarray, cdata(c), csize(c)) == 0)
		{
#if defined(PROMAPPING_TEST) || defined(PROCHUNKING_TEST) || defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
			fprintf(fhashptr, "dedupchunkID = 0 ");
		    fprintf(fhashptr,
        	    "%s %u %x%x%x%x volID=%u startblk=%u startoff=%u\n",
            	bufhuman, csize(c),
		        cdata(c)[csize(c)-4], cdata(c)[csize(c)-3], cdata(c)[csize(c)-2],
        		cdata(c)[csize(c)-1], volID, 
		        *prevBoundaryBlkNump, inc_blkoffset(*lastBlkOffsetminus1p));
#endif
			pzerocollisions++;
#if defined(PDD_REPLAY_DEBUG_SS)
			//fprintf(stdout, "zero chunk with following size: %u\n", csize(c));
			fprintf(stdout, "zc%u ", csize(c));
#endif
    		*prevBoundaryBlkNump = newBoundaryBlkNum+1;
	    	*lastBlkOffsetminus1p = BLKSIZE - 1;
			return 0;	/* this is a zero chunk of size "csize(c)" bytes 
						 * caused when a block ends with some zeros and the next
						 * block was a zero block, causing this chunk to become
						 * a zero chunk by itself.
						 */
		}
		else if (dedupc2pv->clen != csize(c))
		{
			/* This is a case of hash collision with different chunk length */
			pcollisionsdiffclen++;
			dedupc2pv = NULL;
			goto newchunk;
		}
		*oldchunk = NULL;
        if (get_fullchunk(dedupc2pv, oldchunk, &oldendblkID))
            RET_ERR("error in get_fullchunk\n");

		if (memcmp(cdata(*oldchunk), cdata(c), csize(c)))
		{
			/* This is a case of hash collision with different content */
			*oldchunk = free_chunk_t(oldchunk);
			pcollisions++;
			dedupc2pv = NULL;
			goto newchunk;
		}
		*oldchunk = free_chunk_t(oldchunk);
#endif
		
		/* Found existing chunk, just add new c2v mapping */
		pcollisionstp++;
		unmark_old_dedupfetch(dedupc2pv);
		c2v->dedupfetch = 1;	//mark new
        add_c2v_tuple_to_map(c2v, dedupc2pv);

		/* Noting max_dedupfactor */
		if ((unsigned)slist_len(&dedupc2pv->c2vmaps) > max_dedupfactor)
		{
			max_dedupfactor = slist_len(&dedupc2pv->c2vmaps);
			//printf("Update max_dedupfactor = %u\n", max_dedupfactor);
		}
		if (slist_len(&dedupc2pv->c2vmaps) <= 9)
			dedupfactor[slist_len(&dedupc2pv->c2vmaps)]++;
#if defined(PROMAPPING_TEST) || defined(PROCHUNKING_TEST) || defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
		fprintf(fhashptr, "dedupchunkID = %u rw=%d ", dedupc2pv->chunkID,
								rw_flag);
#endif
    }
	else /* Chunk not found in chunktab => new chunk */
    {
#ifdef STRICT_NO_HASH_COLLISION
newchunk:		
#endif
        c2pv = (c2pv_datum*) calloc (1, sizeof(c2pv_datum));
		INIT_LIST_HEAD(&c2pv->c2vmaps);
        note_chunk_attrs(c2pv, c, key, getNextChunkNum(initflag));
		c2v->dedupfetch = 1;
        add_c2v_tuple_to_map(c2v, c2pv);
        ret = hashtab_insert(chunktab.table, c2pv->chashkey, c2pv); //add to hashtab
		if (ret == -ENOMEM)
		{
			RET_ERR("no memory for insert in chunktab.table\n");
		}
		else if (ret == -EEXIST)
		{
			RET_ERR("why re-inserting if already exists\n");
		}

#if defined(PDD_REPLAY_DEBUG_SS_DONE) || defined(STRICT_NO_HASH_COLLISION)
		/* For debugging only.... comment later */
		*oldchunk = NULL;
        if (get_fullchunk(c2pv, oldchunk, &oldendblkID))
            RET_ERR("error in get_fullchunk\n");
		//fprintf(stdout, "inserted chunksize=%u\n", csize(*oldchunk));
		getHashKey(cdata(*oldchunk), csize(*oldchunk), debugkey);
		assert(memcmp(debugkey, c2pv->chashkey, HASHLEN) == 0);
		assert(memcmp(debugkey, key, HASHLEN) == 0);
		*oldchunk = free_chunk_t(oldchunk);
#endif

		dedupfactor[slist_len(&c2pv->c2vmaps)]++;
#if defined(PROMAPPING_TEST) || defined(PROCHUNKING_TEST)  || defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
		fprintf(fhashptr, "chunkID = %u rw=%d ", c2pv->chunkID, rw_flag);
#endif
    }

#ifdef PDD_REPLAY_DEBUG_SS_DONE
    fprintf(stdout,
            "%s %u %x%x%x%x volID=%u startblk=%u startoff=%u chunkID=%u\n",
            bufhuman, csize(c),
        cdata(c)[csize(c)-4], cdata(c)[csize(c)-3], cdata(c)[csize(c)-2],
        cdata(c)[csize(c)-1], volID, 
        *prevBoundaryBlkNump, inc_blkoffset(*lastBlkOffsetminus1p),
		c2v->start_vblk_id, c2v->start_offset_into_vblk);
		//dedupc2pv?dedupc2pv->chunkID:c2pv->chunkID);
#endif

#if defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
    fprintf(fhashptr,
            "%s %u %x%x%x%x volID %u startblk %u startoff %u\n",
            bufhuman, csize(c),
        cdata(c)[csize(c)-4], cdata(c)[csize(c)-3], cdata(c)[csize(c)-2],
        cdata(c)[csize(c)-1], volID, 
		c2v->start_vblk_id, c2v->start_offset_into_vblk);
        //*prevBoundaryBlkNump, inc_blkoffset(*lastBlkOffsetminus1p));
#endif
#if defined(PROMAPPING_TEST) || defined(PDD_REPLAY_DEBUG_SS_DONE)
    fprintf(fhashptr,
            "%u %x%x%x%x volID=%u startblk=%u startoff=%u\n",
            csize(c),
        cdata(c)[csize(c)-4], cdata(c)[csize(c)-3], cdata(c)[csize(c)-2],
        cdata(c)[csize(c)-1], volID, 
		c2v->start_vblk_id, c2v->start_offset_into_vblk);
        //*prevBoundaryBlkNump, inc_blkoffset(*lastBlkOffsetminus1p));
#endif

#if defined(PROMAPPING_TEST) || defined(PDD_REPLAY_DEBUG_SS_DONE)
	int num_c2v;
	chunkmap_t *cp;
	C2V_tuple_t *ct;

	if (dedupc2pv)
		cp = dedupc2pv;
	else
		cp = c2pv;

	fprintf(stdout, "%s", bufhuman);  /* 1 */
	fprintf(stdout, " %u", cp->chunkID); /* 2 */
	fprintf(stdout, " %u", cp->clen); /* 3 */
	fprintf(stdout, " %d", cp->cforced); /* 5 */

	num_c2v = slist_len(&cp->c2vmaps);
	fprintf(stdout, " num_c2v=%d ", num_c2v);

	if (num_c2v > 0)
	{
		struct slist_head *p;
		fprintf(stdout, "[ ");
		__slist_for_each(p, &cp->c2vmaps)  /* 7 */
		{
    		ct = slist_entry(p, struct C2V_tuple_t, head);
		    fprintf(stdout, "(%d)", ct->dedupfetch); /* a*/
		    fprintf(stdout, " %u", ct->volID);/*b*/
		    fprintf(stdout, " %u:", ct->start_vblk_id);    /* c */
		    fprintf(stdout, "%u ", ct->start_offset_into_vblk);
		}
		fprintf(stdout, "]");
	}
	fprintf(stdout, "\n");
#endif

	 /* PRECHUNK_PARTIAL_FIRSTBLK, NOT_LASTBLK */
    /* note static elements for next time */
	if (lastblk_flag == CHUNK_BY_ZEROBLK)	/* if chunk by itself due to next blk being zeroblk, then set prevBoundaryBlkNump as blk+1 for next time */
	{
		__u32 lastblk;
        if (getLastvBlk(volID, &lastblk))
            RET_ERR("volume ID %u has no info\n", volID);
		if (newBoundaryBlkNum == lastblk)
		{
#ifdef SIMREPLAY_DEBUG_SS
			fprintf(stdout, "CHUNK_BY_ZEROBLK at ULTIMATE_LASTBLK\n");
#endif
			*prevBoundaryBlkNump = 0;
		}
		else
    		*prevBoundaryBlkNump = newBoundaryBlkNum+1;
	}
	else
	    *prevBoundaryBlkNump = newBoundaryBlkNum;
    *lastBlkOffsetminus1p = dec_blkoffset(blklen - leftover_len);

#ifndef NONSPANNING_PROVIDE
    /* If chunk & block boundaries coincided, then for a 
     * new chunk, its starting block is the next one (so increment)
     * and its starting offset would be byte 0 of that next block */
    if (*lastBlkOffsetminus1p == BLKSIZE - 1)
    {
		__u32 lastblk;
        if (getLastvBlk(volID, &lastblk))
            RET_ERR("volume ID %u has no info\n", volID);
		if (*prevBoundaryBlkNump == lastblk)
		{
#ifdef SIMREPLAY_DEBUG_SS
			fprintf(stdout, "last offset at ULTIMATE_LASTBLK\n");
#endif
			*prevBoundaryBlkNump = 0;
		}
		else
        	(*prevBoundaryBlkNump)++;
    }
#endif

	free(key);	//savemem
#ifdef STRICT_NO_HASH_COLLISION
	free(debugkey);	//savemem
#endif

    if (dedupc2pv)
        return dedupc2pv->chunkID;
    else
    {
        setChunkMap(c2pv->chunkID, c2pv);
        return c2pv->chunkID;
    }
}

#ifndef PROMAPPING_TEST
#ifndef PROCHUNKING_TEST
#ifndef PRODUMPING_TEST

/* get_fullchunk: get full data belonging to chunk specified by c2pv 
 * 			by using the PROVIDED logic of redirection.
 * @c2pv[in]:  a pointer to the existing chunkmap.
 * @outbuf[out]: contains data of the chunk specified by c2pv
 * @endblkID[out]: blockID in which seqnextp ends
 */
int get_fullchunk(chunkmap_t* c2pv, 
				struct chunk_t **chunkp, __u32 *endblkID)
{
	struct C2V_tuple_t *deduped_c2v;
	struct preq_spec preq_local;
	__u16 start_into_buf, end_into_buf;
	int numbytes_to_copy, numbytes_copied=0;
	__u16 pos = 0;
	__u32 fetch_vblk;
#if defined(PDD_REPLAY_DEBUG_SS)
	int debugi = 0;
#endif
#if 0
	struct pread_spec *output = malloc_outputpread(1);
#endif

	assert(*chunkp == NULL);
#ifdef DEBUG_SS
	assert(c2pv != NULL);
#endif

	/* Retrieve the c2v mapping corresponding to the deduplicated 
	 * mapping. This is because while fetching any chunk's content,
	 * we want to repeatedly fetch it from the same (physical) block
	 * s.t. its popularity will increase and probability of its 
	 * presence in cache will increase, hence improving fetch times.
	 */
	deduped_c2v = get_deduped_c2v(c2pv);
	if (!deduped_c2v)
		RET_ERR("No c2v tuple marked as dedup_fetch:1\n");

	fetch_vblk = deduped_c2v->start_vblk_id;

    *chunkp = alloc_chunk_t(c2pv->clen);
    if (*chunkp == NULL)
            RET_ERR("malloc failed for prechunk\n");

	/* We wish to fetch entire chunk here. Multiple cases :- 
	 * 1. Chunk lies within single vblk
	 * 2. Chunk straddles 2 vblks
	 * 3. Chunk straddles multiple blocks
	 */
#if defined(PDD_REPLAY_DEBUG_SS_DONE)
	debugi = 0;
#endif
	numbytes_to_copy = c2pv->clen;
	while (numbytes_to_copy > 0)
	{
#if defined(PDD_REPLAY_DEBUG_SS_DONE)
		printf("inserted chunk numbytes_to_copy=%u in %d iter\n", numbytes_to_copy,
			debugi);
#endif
		/* For the first (or only) vblk of this chunk, 
		 * start_into_buf is decided by where chunk starts 
		 * off within the vblk. For all others, it starts at 0 for sure.
		 */
		if (fetch_vblk == deduped_c2v->start_vblk_id)
		{
			start_into_buf = deduped_c2v->start_offset_into_vblk;
		}
		else
		{
			start_into_buf = 0;
		}

		/* For the last (or only) vblk of this chunk,
		 * end_into_buf is decided by where chunks ends within
		 * the vblk. For all others, it ends at last offset (BLKSIZE-1).
		 */
		if (numbytes_to_copy <= (BLKSIZE - start_into_buf))
		{
			*endblkID = fetch_vblk;
			end_into_buf = start_into_buf + numbytes_to_copy - 1;
		}
		else
		{
			end_into_buf = BLKSIZE - 1;
		}

#ifdef SIMREPLAY_DEBUG_SS_DONE
		fprintf(stdout, "start_into_buf=%u, end_into_buf=%u, fetch_vblk=%u\n",
				start_into_buf, end_into_buf, fetch_vblk);
#endif

		/* Determine the numbytes to be copied from this iteration/buf */
		numbytes_copied = (end_into_buf - start_into_buf + 1);

        if (disksimflag)
        {
            __u8 *simcontent = malloc(BLKSIZE);
			if (simcontent == NULL)
				RET_ERR("malloc failed\n");
			if (runtimemap)		//temporary!!!!!
			{
				if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
				{
					if (disk_read_trap(deduped_c2v->volID, fetch_vblk, 
								simcontent, BLKSIZE))
							VOID_ERR("disk_read_trap failed NOCOLLECT\n");
				}
				else
				{
					unsigned char tempbuf[MD5HASHLEN_STR-1];
					if (disk_read_trap(deduped_c2v->volID, fetch_vblk, 
								tempbuf, MD5HASHLEN_STR-1))
							VOID_ERR("disk_read_trap failed COLLECT\n");
					generate_BLK_content(simcontent, tempbuf, 
							MD5HASHLEN_STR-1,BLKSIZE);
				}
			}
#if 0
			else
			{
	            __u32 ioblk;
    	        if (getVirttoPhysMap(deduped_c2v->volID, fetch_vblk, &ioblk))
        	        VOID_ERR("getVirttoPhysMap error'ed\n");
	            get_simcontent(ioblk, simcontent, 1);
		        create_preq_spec(deduped_c2v->volID, fetch_vblk, 
					end_into_buf - start_into_buf + 1, 1, simcontent,
                    start_into_buf, end_into_buf, &preq_local);
			}
#endif

			/* Copy those many bytes and free buf */
			memcpy(cdata(*chunkp)+pos, simcontent + start_into_buf, 
					numbytes_copied);
			free(simcontent);
		}
		else
		{
	        create_preq_spec(deduped_c2v->volID, fetch_vblk, 
					end_into_buf - start_into_buf + 1, 1, NULL,
                    start_into_buf, end_into_buf, &preq_local);

			/* Fetch the pblk (with start & end offsets) data into buf */
		    if (fetchdata_pblk(&preq_local))
				RET_ERR("error in fetchdata_pblk\n");

			/* Copy those many bytes and free buf */
	        memcpy(cdata(*chunkp)+pos, preq_local.content, numbytes_copied);
			free(preq_local.content);
		}


		/* Update the remaining numbytes_to_copy counter */
		numbytes_to_copy -= numbytes_copied;

		/* Update the vblk number to be fetched in next iteration */
		fetch_vblk++;

		/* Update pos into local_buf for next iteration */
		pos += numbytes_copied;
#if defined(PDD_REPLAY_DEBUG_SS)
		debugi++;
#endif

	}
#ifdef DEBUG_SS
	assert(numbytes_to_copy == 0);
#endif
	return 0;
}

#ifndef NONSPANNING_PROVIDE
/* seqnextp is a double pointer to the existing chunkmap
 * cdp contains the new chunk piece to be prepended to above chunk
 * lastblk_flag notes the circumstance in which this is happening (useful here?)
 *
 * 1a. Fetch the chunk content for seqnextp into local buf.
 * 1b. Note the blockID in which seqnextp ends => endblkID. Its v2c mapping
 * 		to be updated later in 4.
 * 2. Create new buf with content of both cdp and seqnextp.
 * 3. Check if seqnextp is a dedup chunk. If yes, unburden mapping. If no, nothing.
 * 4. processChunk() for new content
 * 5. Return the new chunkID as chunkIDp, so that it can be used to update 
 *      v2c map of endblkID in caller function
 *
 * @seqnextp[in]: Pointer to sequentially next chunk during write chunking
 * @cdp[in]: Pointer to chunk piece that needs to be prepended to seqnextp
 * @chunkIDp[out]: Pointer to output chunk ID - maybe new or dedup
 * @endblkID[out]: Block where the seqnextp chunk ends, this is used in caller
 * @volID[in]: volume to which the block being written belongs
 * @blockID[in]: used to identify the corresponding c2v map in recyclechunkID
 */
int updateBegChunkMap(struct chunkmap_t **seqnextp, struct chunk_t **cdp,
        int lastblk_flag, __u16 blklen,
		chunk_id_t *chunkIDp, __u32 *endblkID, __u16 volID, __u32 blockID)
{
	struct chunk_t *localbuf = NULL;
	struct chunk_t *seqchunk = NULL;

	assert(*seqnextp != NULL);

	/* Fetch the chunk content for seqnextp into local buf and also
	 * the last vblk for chunk seqnextp into endblkID 
	 */
	get_fullchunk(*seqnextp, &seqchunk, endblkID);

#if 0
	/* Create seqchunk containing data of seqnextp */
	seqchunk = alloc_chunk_t((*seqnextp)->clen);
	if (seqchunk == NULL)
		RET_ERR("alloc_chunk_t failed for seqchunk\n");
	seqbytes = (*seqnextp)->clen;
	memcpy(cdata(seqchunk), outbuf, seqbytes);
#endif

	/* Appending "seqchunk" data to "cdp" chunk and output as "localbuf" */
	if (createInitialChunkBuf(&localbuf, cdata(seqchunk), csize(seqchunk),
					cdp, &csize(*cdp)))
	{
		RET_ERR("createInitialChunkBuf returned error in updateBegChunkMap\n");
	}
#ifdef DEBUG_SS
	assert(*cdp == NULL);
#endif

	/* Based on dedup status of the seqnextp chunk, recycle its chunkID
	 * if required. Will also unburden the relevant c2v mapping from chunkmap.
	 */
	if (recyclechunkID((*seqnextp)->chunkID, 1, volID, blockID))
		RET_ERR("recyclechunkID failed\n");

	*chunkIDp = processChunk(localbuf, 0, blockID, volID, NOINIT_STAGE,
						blklen, lastblk_flag);

	return 0;
}
#endif

#endif // ifndef PRODUMPING_TEST
#endif // ifndef PROCHUNKING_TEST
#endif // ifndef PROMAPPING_TEST

