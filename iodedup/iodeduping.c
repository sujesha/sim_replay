#include <asm/types.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "iodeduping.h"
#include "ioserveio.h"
#include "d2pv-map.h"
#include "debug.h"
#include "defs.h"
#include "utils.h"
#include "p2d-map.h"
#include "uhashtab.h"
#include "deduptab.h"
#include "unused.h"
#ifdef DEDUPING_TEST
	#include "prodeduping_test_stub.h"
#endif
#include "replay-defines.h"
#include <time.h>

__u32 iocollisions = 0;
__u32 iocollisionstp = 0;

#if defined(IOMAPPING_TEST) || defined(PDD_REPLAY_DEBUG_SS)
	extern FILE * fhashptr;
#endif

extern __u32 dedupfactor[10];
extern __u32 max_dedupfactor;
extern unsigned char cmaphit_flag;
extern __u32 cmaphit_iodedupID; 
extern unsigned char ccache_already_had_flag;
extern __u32 ccache_already_had_obj_ioblkID;
extern struct deduptab deduptab;
extern int disksimflag;
extern int collectformat;
extern FILE * ftimeptr;
extern int warmupflag;

inline __u64 gettime(void);
int del_d2p_from_d2pmaps(dedupmap_t *d2pv, __u32 ioblkID);

/* Resume the deduping process that happens during the initial scanning.
 * We also use it as a building block in the run-time map creation
 * process as well, just by initializing the variables accordingly. 
 * P2D tuple consists of iodedupID to indicate which dedup block does
 * this pblk map into.
 *
 * @param[in] buf
 * @param[in] len
 * @param[in] ioblkID
 * @param[in] initflag
 * @param[in] lastblk_flag
 */
int resumeDeduping(unsigned char *buf, __u16 len,
		__u32 ioblkID, int initflag, int lastblk_flag, int rw_flag)
{
    //uint32_t ptime;  /* To be noted when event occurs, is this needed? TODO
	int ret;
	__u32 iodedupID;
	unsigned long long stime=0, etime=0;
    //savemem unsigned char dig[HASHLEN + MAGIC_SIZE];
	unsigned char *dig = malloc(HASHLEN + MAGIC_SIZE);
	unsigned char *key = NULL;
	d2pv_datum *dedupd2pv = NULL, *d2pv = NULL;
	D2P_tuple_t *d2p = NULL;
#ifdef STRICT_NO_HASH_COLLISION
	unsigned char *oldbuf = NULL;
    //savemem unsigned char debugkey[HASHLEN + MAGIC_SIZE];
	unsigned char* debugkey = malloc(HASHLEN + MAGIC_SIZE);
#endif
#if defined(DEDUPING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	 /* Buf will always have "len" == BLKSIZE. No leftovers.  */
#ifdef DEBUG_SS
		assert(len == BLKSIZE || lastblk_flag == ZEROBLK_FLAG);
		assert(initflag == INIT_STAGE || initflag == NOINIT_STAGE);
#endif

	if (len != 0)	/* Not a zero blk */
	{
		assert(buf != NULL);
		memset(dig, 0, HASHLEN);
		stime = gettime();	/* START IODEDUP map-update-get-hash time */
		if (getHashKey(buf, len, dig))
    	    RET_ERR("getHashKey() returned error\n");	
		etime = gettime();	/* END IODEDUP map-update-get-hash */
		ACCESSTIME_PRINT("iodedmap-map-update-component-get-hash time: %llu\n",
					 etime - stime);

		key = (unsigned char*)dig;
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		printf("Content metadata update: buf=%s\n", (char*)buf);
		printf("Content metadata update: md5="); puts((char*)dig);
#endif

		if (cmaphit_flag && !disksimflag)
		{
			assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);//assert(0);
			//no need to update metadata if this is read request with
			//metadata hit, and disk is real, not simulated!
			return 0;
		}
		else if (cmaphit_flag)	/* read with metadata hit but disk simulated */
		{
			//ideally, i.e. if traces are perfect, then
			//no need to update metadata here, just return, but.... 
			d2pv_datum *curr_d2pv = NULL;
			//but if the traces dont have consistent read/write requests,
			//in some cases of metadata-hit followed by cccahe-miss, the
			//following assert fails! work-around below...
			curr_d2pv = (d2pv_datum*) hashtab_search(deduptab.table, key);
#ifndef INCONSISTENT_TRACES
			assert(curr_d2pv != NULL);
#endif
			//work-around:
			//since we have already updated the content-cache with
			//the "offending content", we have to update the existing
			//metadata of this ioblk with new dhashkey, and also add
			//that new d2pv entry into deduptab (or update existing
			//dedupd2pv for the new dhashkey, if that is the case)

			d2pv_datum *cmaphit_d2pv = NULL; 	//found during metadata hit
			d2pv_datum *trace_d2pv = NULL;		//due to inconsistent trace,
												//if disk is being simulated
#ifdef INCONSISTENT_TRACES
			D2P_tuple_t *trace_d2p = NULL;
#endif
			cmaphit_d2pv = getDedupMap(cmaphit_iodedupID);
			assert(cmaphit_d2pv != NULL);
			trace_d2pv = (d2pv_datum*) hashtab_search(deduptab.table, key);
			if (cmaphit_d2pv == trace_d2pv)
				curr_d2pv = cmaphit_d2pv;	//true if trace is consistent
#ifndef INCONSISTENT_TRACES
			else
				RET_ERR("inconsistence why?\n");
#else
			else	//begin: fix for inconsistent trace
			{
				//trace is inconsistent, so we have to do some
				//impromptu metadata updates:-
				
				if (trace_d2pv == NULL)
				{
					__u32 iodedupID;
					trace_d2pv = (d2pv_datum*) calloc(1, sizeof(d2pv_datum));
		            INIT_LIST_HEAD(&trace_d2pv->d2pmaps);
		            iodedupID = getNextDedupNum(initflag);
					trace_d2p = calloc (1, sizeof(D2P_tuple_t));

/***************************************************************				
					if (ccache_already_had_flag)
					{
						note_dedup_attrs(trace_d2pv, key, iodedupID,
							ccache_already_had_obj_ioblkID);
						note_d2p_tuple(trace_d2p, 
							ccache_already_had_obj_ioblkID);
					}
					else
					{
						note_dedup_attrs(trace_d2pv, key, iodedupID, ioblkID);
			        	note_d2p_tuple(trace_d2p, ioblkID);
					}
***************************************************************/					
					/* We are here only upon a ccache miss after metadata hit,
					 * so ccache_already_had_flag == 0 mandatory!
					 */
					//assert(ccache_already_had_flag == 0);//not true for wif
					note_dedup_attrs(trace_d2pv, key, iodedupID, ioblkID);
					note_d2p_tuple(trace_d2p, ioblkID);
					add_d2p_tuple_to_map(trace_d2p, trace_d2pv);
					ret = hashtab_insert(deduptab.table, trace_d2pv->dhashkey, 
							trace_d2pv);
					setDedupMap(trace_d2pv->iodedupID, trace_d2pv);
					ret = updateBlockio(ioblkID, lastblk_flag, iodedupID);
					if (ret)
			            RET_ERR("updateBlockio() error'ed\n");
				}
				else
				{
					__u32 iodedupID;
					iodedupID = trace_d2pv->iodedupID;
					if (NULL == get_nondeduped_d2p(trace_d2pv, ioblkID))
					{
						trace_d2p = calloc (1, sizeof(D2P_tuple_t));
						//assert(ccache_already_had_flag == 0); //not true
						note_d2p_tuple(trace_d2p, ioblkID);
						add_d2p_tuple_to_map(trace_d2p, trace_d2pv);
					}
					ret = updateBlockio(ioblkID, lastblk_flag, iodedupID);
                    if (ret)
                        RET_ERR("updateBlockio() error'ed\n");
				}

				//if current ioblk is the only one in old sector-list,
				//then delete cmaphit_d2pv from hash-table, else let
				//it stay there.
				if (slist_len(&cmaphit_d2pv->d2pmaps) > 1)
				{
					del_d2p_from_d2pmaps(cmaphit_d2pv, ioblkID);
				}
				else
				{
					del_d2p_from_d2pmaps(cmaphit_d2pv, ioblkID);
					hashtab_remove(deduptab.table, cmaphit_d2pv->dhashkey); 
					setDedupMap(cmaphit_iodedupID, NULL);   //d2pv freed
				}

				//mark trace_d2pv as curr_d2pv for self-hits/misses below!
				curr_d2pv = trace_d2pv;

			}//end: fix for inconsistent trace
#endif

			//update the ioblkID for counts of self-hits/misses
			//copied from iodeduping.c, if change in 1 place, change both
            if (!ccache_already_had_flag)   //set in __arc_add 
                curr_d2pv->ioblkID = ioblkID;   //ensure counts line up -- self
            else
                curr_d2pv->ioblkID = ccache_already_had_obj_ioblkID;
			return 0;	
		}

	    /* Note the D2P tuple for this dedup */
	    d2p = calloc (1, sizeof(D2P_tuple_t));
    	note_d2p_tuple(d2p, ioblkID);

		stime = gettime();	/* START IODEDUP map-update-hashtab-search time */
		dedupd2pv = (d2pv_datum*) hashtab_search(deduptab.table, key);
		if (dedupd2pv)
		{
			etime = gettime();	/* END IODEDUP map-update-hashtab-search time */
			ACCESSTIME_PRINT("iodedmap-map-update-component-hashtab-search-success time: %llu\n", etime - stime);
#ifdef STRICT_NO_HASH_COLLISION
			if (!disksimflag){
			if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
				!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
       		{
				assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
					|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
				oldbuf = NULL;
				if (get_fulldedup(dedupd2pv, &oldbuf))
					RET_ERR("error in get_fulldedup\n");
				if (memcmp(buf, oldbuf, BLKSIZE))
				{
					char bufhuman2[HASHLEN_STR];
		    		MD5Human(key, bufhuman2);
					/* This is a case of hash-collision but different content */
					fprintf(stdout, "case of hash-collision %s but " \
							"different content\n", bufhuman2);
					iocollisions++;
					free(oldbuf);
					dedupd2pv = NULL;
					goto newdedup;
				}
				free(oldbuf);
			}}
#endif
			iocollisionstp++;
			if (NULL == get_nondeduped_d2p(dedupd2pv, ioblkID))
	        	add_d2p_tuple_to_map(d2p, dedupd2pv);
			else
				free(d2p);	//new
			iodedupID = dedupd2pv->iodedupID;

			//same as above, if change in 1 place, change both
			if (!ccache_already_had_flag)	//set in __arc_add 
				dedupd2pv->ioblkID = ioblkID;	//ensure counts line up -- self
			else
				dedupd2pv->ioblkID = ccache_already_had_obj_ioblkID;
	
			/* Noting max_dedupfactor */
			if ((unsigned)slist_len(&dedupd2pv->d2pmaps) > max_dedupfactor)
			{
				max_dedupfactor = slist_len(&dedupd2pv->d2pmaps);
				//printf("Update max_dedupfactor = %u\n", max_dedupfactor);
			}
			if (slist_len(&dedupd2pv->d2pmaps) <= 9)
				dedupfactor[slist_len(&dedupd2pv->d2pmaps)]++;
#if defined(IOMAPPING_TEST) || defined(DEDUPING_TEST) || defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
    	    fprintf(fhashptr, "dedupiodedupID = %u rw=%d ", 
					dedupd2pv->iodedupID, rw_flag);
#else
			UNUSED(rw_flag);
#endif
		}
		else
		{
#ifdef STRICT_NO_HASH_COLLISION
newdedup:
#endif
			etime = gettime();	/* END IODEDUP map-update-hashtab-search time */
			ACCESSTIME_PRINT("iodedmap-map-update-component-hashtab-search-fail time: %llu\n", etime - stime);

			stime = gettime();	/* START IODEDUP map-update-new-dedup time */
			d2pv = (d2pv_datum*) calloc(1, sizeof(d2pv_datum));
			INIT_LIST_HEAD(&d2pv->d2pmaps);
			iodedupID = getNextDedupNum(initflag);
			if (ccache_already_had_flag)
				note_dedup_attrs(d2pv, key, iodedupID, 
						ccache_already_had_obj_ioblkID);
			else
				note_dedup_attrs(d2pv, key, iodedupID, ioblkID);
			add_d2p_tuple_to_map(d2p, d2pv);
			etime = gettime();	/* END IODEDUP map-update-new-dedup time */
			ACCESSTIME_PRINT("iodedmap-map-update-component-new-dedup time: %llu\n", etime - stime);

			stime = gettime();	/* START IODEDUP map-update-hashtab-insert time */
			ret = hashtab_insert(deduptab.table, d2pv->dhashkey, d2pv); //add to hashtab
			etime = gettime();	/* END IODEDUP map-update-hashtab-insert time */
			ACCESSTIME_PRINT("iodedmap-map-update-component-hashtab-insert time: %llu\n", etime - stime);
			if (ret == -EEXIST)
			{
				printf("-EEXIST found for ioblkID=%u, buf=%s\n", ioblkID, buf);
				if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
					!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
				{
					char bufhuman2[HASHLEN_STR], bufhuman3[HASHLEN_STR];
	    			MD5Human(d2pv->dhashkey, bufhuman2);
	    			MD5Human(key, bufhuman3);
					printf("why re-entering %s if already exists as %s\n", 
						bufhuman2, bufhuman3);
				}
				else
				{
					assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
						|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
					printf("why re-entering %s if already exists as %s\n",
                        d2pv->dhashkey, key);		
				}
			}
			else if (ret == -ENOMEM)
			{
				RET_ERR("why are we out of memory?\n");
			}
	        setDedupMap(d2pv->iodedupID, d2pv);
#ifdef DEBUG_SS
	        assert(memcmp(key, d2pv->dhashkey, HASHLEN + MAGIC_SIZE) == 0);
#endif
#if defined(STRICT_NO_HASH_COLLISION)
			if (!disksimflag){
			if (!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
				!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
       		{
        		/* For debugging only.... comment later */
				if (get_fulldedup(d2pv, &oldbuf))
    		        RET_ERR("error in get_fulldedup\n");
   	    		getHashKey(oldbuf, BLKSIZE, debugkey);
        		assert(memcmp(debugkey, d2pv->dhashkey, HASHLEN+MAGIC_SIZE)==0);
		        assert(memcmp(debugkey, key, HASHLEN+MAGIC_SIZE) == 0);
				//printf("memcmp of hashkey passed after block insert\n");
				free(oldbuf);
			}}
#endif
			dedupfactor[slist_len(&d2pv->d2pmaps)]++;
#if defined(IOMAPPING_TEST) || defined(DEDUPING_TEST) || defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
	        fprintf(fhashptr, "iodedupID = %u rw=%d ", d2pv->iodedupID,
					rw_flag);
#endif
		}
	}	/* Only if not a zeroblk */
	else
		iodedupID = 0;

#if 0 
	defined(IOMAPPING_TEST) || defined(DEDUPING_TEST) || defined(IODUMPING_TEST)
    ret = updateBlockio(ioblkID, lastblk_flag, iodedupID); //, key);
	if (ret)
		RET_ERR("updateBlockio() error'ed\n");
#else
	if (initflag == INIT_STAGE)
	{
		stime = gettime();	/* START IODEDUP map-update-processblk time */
        ret = processBlockio(ioblkID, lastblk_flag, iodedupID); //, key);
		etime = gettime();	/* END IODEDUP map-update-processblk time */
		ACCESSTIME_PRINT("iodedmap-map-update-component-processblk time: %llu\n", etime - stime);
		if (ret)
			RET_ERR("processBlockio() error'ed\n");
	}
	else /* NOINIT_MAPMISS, NOINIT_MAPHIT, NOINIT_STAGE */
	{
		stime = gettime();	/* START IODEDUP map-update-updateblk time */
        ret = updateBlockio(ioblkID, lastblk_flag, iodedupID); //, key);
		etime = gettime();	/* END IODEDUP map-update-updateblk time */
		ACCESSTIME_PRINT("iodedmap-map-update-component-updateblk time: %llu\n", etime - stime);
		if (ret)
			RET_ERR("updateBlockio() error'ed\n");
	}
#endif

#if defined(PDD_BENCHMARK_STATS)  || defined(SIM_BENCHMARK_STATS)
#if 1
	if (len != 0)
	{
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
			len=MD5HASHLEN_STR-1;
	    char bufhuman[HASHLEN_STR];
	    MD5Human(key, bufhuman);
    	fprintf(fhashptr,
	            "%s %u %x%x%x%x blk= %u\n",
    	        bufhuman, len,
        	buf[len-4], buf[len-3], buf[len-2],
	        buf[len-1],
    	    ioblkID);
	}
	else 
	{
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
			len=MD5HASHLEN_STR-1;
    	fprintf(fhashptr,
	            "%s %u 0000 blk= %u\n",
    	        "zeroblkhash", len,
    	    ioblkID);
	}
#endif
#endif

#if defined(IOMAPPING_TEST) || defined(DEDUPING_TEST) || defined(PDD_REPLAY_DEBUG_SS_DONE)
    //WHERE;
    int num_d2p;
    dedupmap_t *cp;
    D2P_tuple_t *ct;

    if (dedupd2pv)
        cp = dedupd2pv;
    else
        cp = d2pv;

    fprintf(stdout, " %llu", cp->iodedupID);
    fprintf(stdout, " %d", cp->ddirty);

    num_d2p = slist_len(&cp->d2pmaps);
    fprintf(stdout, " num_d2p=%d ", num_d2p);

    if (num_d2p > 0)
    {
        struct slist_head *p;
        fprintf(stdout, "[ ");
        __slist_for_each(p, &cp->d2pmaps)
        {
            ct = slist_entry(p, struct D2P_tuple_t, head);
            fprintf(stdout, "(%d)", ct->dedupfetch);
            fprintf(stdout, " %llu:", ct->ioblkID);
        }
        fprintf(stdout, "]");
    }
    fprintf(stdout, "\n");
#endif

	free(dig);	//savemem
#ifdef STRICT_NO_HASH_COLLISION
	free(debugkey);
#endif
	return 0;

}

#if 0
/*
 * @param[in] buf
 * @param[in] len
 * @param[in] volID
 * @param[in] ioblkID
 * @param[in,out] prechunk
 * @param[in,out] postchunk
 * @param[in,out] seqnextp
 * @return status		
 */
int perfWriteDeduping(unsigned char *buf, int len, __u16 volID, __u32 ioblkID)
{
	int ret = 0;
    //uint32_t ptime;  // To be noted when event occurs, is this needed? FIXME
	int numblks_to_write;
    unsigned char *blkbuf;
    unsigned char *p;
	__u16 curr_len;
	__u32 curr_ioblkID = ioblkID;

	__u32 iter;

	assert(len % BLKSIZE == 0);
	numblks_to_write = len / BLKSIZE;

	p = cdata(chunk);
	for (iter=0; iter < numblks_to_write; iter++)
	{
		blkbuf = malloc(BLKSIZE);
       	memcpy(blkbuf, p, buflen);

        /* Not checking for zero block here, because it's not a possibility
         * unless an existing block can be over-written with zero block. If so,
         * then fix here to handle it. FIXME
         */
    	ret = resumeDeduping(blkbuf, BLKSIZE, curr_ioblkID, NOINIT_STAGE,
						lastblk_flag);
	    if (ret)
    	{
        	RET_ERR("resumeDeduping err in %llu perfWriteDeduping\n", iter);
    	}

		p = buf + BLKSIZE;
		curr_ioblkID++;
        if (blkbuf)
            free(blkbuf);
    }
    return 0;
}
#endif

