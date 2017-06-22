#include <asm/types.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include "fixing.h"
#include "f2pv-map.h"
#include "debug.h"
#include "defs.h"
#include "utils.h"
#include "v2f-map.h"
#include "uhashtab.h"
#include "fixedtab.h"
#include "fserveio.h"
#include "unused.h"
#ifdef CONFIXING_TEST
	#include "profixing_test_stub.h"
#endif
#include "replay-defines.h"

__u32 fcollisions = 0;
__u32 fcollisionstp = 0;

#if defined(CONMAPPING_TEST) || defined(PDD_REPLAY_DEBUG_SS)
	extern FILE * fhashptr;
#endif

extern __u32 dedupfactor[10];
extern __u32 max_dedupfactor;
extern int preplayflag;
extern int disksimflag;
extern int collectformat;
extern struct fixedtab fixedtab;

extern unsigned char fmaphit_flag;
extern __u32 fmaphit_fixedID;

int del_f2v_from_f2vmaps(fixedmap_t *f2pv, __u16 volID, __u32 vBlkID,
		__u16 *leader_volIDp, __u32 *leader_blkIDp);

/* Resume the chunking process that happens during the initial scanning
 * V2F tuple consists of fixedID to indicate which fixed block does
 * this vblk map into.
 *
 * @param[in] buf
 * @param[in] len
 * @param[in] volID
 * @param[in] blockID
 * @param[in] initflag
 * @param[in] lastblk_flag
 */
int resumeFixing(unsigned char *buf, __u16 len, __u16 volID, 
		__u32 blockID, int initflag, int lastblk_flag, int updateleader,
		int rw_flag)
{
    //uint32_t ptime;  /* To be noted when event occurs, is this needed? TODO
	int ret;
	__u32 fixedID;
    //savemem unsigned char key[HASHLEN + MAGIC_SIZE];
	unsigned char *key = malloc(HASHLEN + MAGIC_SIZE);
	f2pv_datum *dedupf2pv = NULL, *f2pv = NULL;
	F2V_tuple_t *f2v = NULL;
#ifdef STRICT_NO_HASH_COLLISION
	unsigned char *oldbuf = NULL;
#endif
#if defined(STRICT_NO_HASH_COLLISION)
    //savemem unsigned char debugkey[HASHLEN + MAGIC_SIZE];
	unsigned char* debugkey = malloc(HASHLEN);
#endif
	const char *checkhuman = "c5a3ce558df556501097480783bfc6f7";
	//avemem char bufhuman[HASHLEN_STR];
	char *bufhuman = malloc(HASHLEN_STR);
#if defined(CONFIXING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	 /* Buf will always have "len" == BLKSIZE. No leftovers.  */
#ifdef DEBUG_SS
		assert(len == BLKSIZE || lastblk_flag == ZEROBLK_FLAG);
		assert(initflag == INIT_STAGE || initflag == NOINIT_STAGE);
#endif

	memset(key, 0, HASHLEN);
	if (len != 0)	/* Not a zero blk */
	{
	    if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
			assert(len == MD5HASHLEN_STR-1);
		else
			assert(len == BLKSIZE);

		/* Hash(+magic) the block */
	    if (getHashKey(buf, len, key))
        	RET_ERR("getHashKey() returned error\n");

#if defined(SIM_BENCHMARK_STATS)
		if (fmaphit_flag)
		{
			if (fmaphit_fixedID == 3750409)
				printf("%s: fmaphit_fixedID == %u for blockID=%u\n",
					__FUNCTION__, fmaphit_fixedID, blockID);

	    	    fprintf(fhashptr, "dedupfixedID = %u rw=%d! ", 
					fmaphit_fixedID, rw_flag);
				if (1)	
				{
				    char bufhuman[HASHLEN_STR];
				    MD5Human(key, bufhuman);
			    	fprintf(fhashptr,
	        		    "%s %u %x%x%x%x blk= %u %u\n",
		    	        bufhuman, len,
        				buf[len-4], buf[len-3], buf[len-2],
				        buf[len-1],
			    	    volID, blockID);
				}
		}
#endif
        if (fmaphit_flag && !disksimflag)
        {
			assert(0);	//not expected for now
            //no need to update metadata if this is read request with
            //metadata hit, and disk is real, not simulated!
            return 0;
        }
        else if (fmaphit_flag)  /* read with metadata hit but disk simulated */
        {
            //ideally, i.e. if traces are perfect, then
            //no need to update metadata here, just return, but.... 
            f2pv_datum *curr_f2pv = NULL;
            //but if the traces dont have consistent read/write requests,
            //in some cases of metadata-hit followed by bcache-miss, the
            //following assert fails! work-around below...
            curr_f2pv = (f2pv_datum*) hashtab_search(fixedtab.table, key);
#ifndef INCONSISTENT_TRACES
            if (curr_f2pv == NULL)
			{
				RET_ERR("%s: why fixedtab NULL after fmaphit for blockID %u "
						"fixedmap_fixedID=%u\n",
					__FUNCTION__, blockID, fmaphit_fixedID);
			}
#endif
            //work-around:
            //since we have already updated the sector-cache with
            //the "offending content", we have to update the existing
            //metadata of this vblk with new fhashkey, and also add
            //that new f2pv entry into fixedtab (or update existing
            //dedupf2pv for the new fhashkey, if that is the case)

            f2pv_datum *fmaphit_f2pv = NULL;    //found during metadata hit
            f2pv_datum *trace_f2pv = NULL;      //due to inconsistent trace,
                                                //if disk is being simulated
#ifdef INCONSISTENT_TRACES
            F2V_tuple_t *trace_f2v = NULL;
#endif
            fmaphit_f2pv = getFixedMap(fmaphit_fixedID);
            assert(fmaphit_f2pv != NULL);
            trace_f2pv = (f2pv_datum*) hashtab_search(fixedtab.table, key);
            if (fmaphit_f2pv == trace_f2pv)
                curr_f2pv = fmaphit_f2pv;   //true if trace is consistent
#ifndef INCONSISTENT_TRACES
			else
			{
				RET_ERR("inconsistence? blockID=%u,fmaphit_fixedID=%u, "
						"key=%s\n", blockID, fmaphit_fixedID, key);
			}
#else
            else    //begin: fix for inconsistent trace
            {
                //trace is inconsistent, so we have to do some
                //impromptu metadata updates:-

                if (trace_f2pv == NULL)
                {
                    __u32 fixedID;
                    trace_f2pv = (f2pv_datum*) calloc(1, sizeof(f2pv_datum));
                    INIT_LIST_HEAD(&trace_f2pv->f2vmaps);
                    fixedID = getNextFixedNum(initflag);
                    trace_f2v = calloc (1, sizeof(F2V_tuple_t));

/***************************************************************                
                    if (bcache_already_had_flag)
                    {
                        note_fixed_attrs(trace_f2pv, key, fixedID,
                            ccache_already_had_obj_ioblkID);
                        note_f2v_tuple(trace_f2v, 
                            ccache_already_had_obj_ioblkID);
                    }
                    else
                    {
                        note_fixed_attrs(trace_f2pv, key, fixedID, ioblkID);
                        note_f2v_tuple(trace_f2v, ioblkID);
                    }
***************************************************************/                 
                    /* We are here upon a ccache miss after metadata hit */
                    //assert(bcache_already_had_flag == 0);
                    note_fixed_attrs(trace_f2pv, key, fixedID);
                    note_f2v_tuple(trace_f2v, volID, blockID);
					trace_f2v->dedupfetch = 1;
	                add_f2v_tuple_to_map(trace_f2v, trace_f2pv);
                    ret = hashtab_insert(fixedtab.table, trace_f2pv->fhashkey,
                            trace_f2pv);
					assert(ret == 0);
                    setFixedMap(trace_f2pv->fixedID, trace_f2pv);
                    ret = updateBlockf(blockID, volID, lastblk_flag, fixedID);
                    if (ret)
                        RET_ERR("updateBlockf() error'ed\n");
                }
                else
                {
                    __u32 fixedID;
                    fixedID = trace_f2pv->fixedID;
					if (NULL == get_nondeduped_f2v(trace_f2pv, volID, blockID))
					{
                	    trace_f2v = calloc (1, sizeof(F2V_tuple_t));
            	        //assert(bcache_already_had_flag == 0); //not true
        	            note_f2v_tuple(trace_f2v, volID, blockID);
						assert(!intraonlyflag);	//not expected for now
						unmark_old_dedupfetchF(trace_f2pv, volID);
						trace_f2v->dedupfetch = 1;
                    	add_f2v_tuple_to_map(trace_f2v, trace_f2pv);
					}
                    ret = updateBlockf(blockID, volID, lastblk_flag, fixedID);
                    if (ret)
                        RET_ERR("updateBlockf() error'ed\n");
                }

                //if current vblk is the only one in old sector-list,
                //then delete fmaphit_f2pv from hash-table, else let
                //it stay there.
                if (slist_len(&fmaphit_f2pv->f2vmaps) > 1)
                {
                    del_f2v_from_f2vmaps(fmaphit_f2pv, volID, blockID,
							NULL, NULL);
                }
                else
                {
                    del_f2v_from_f2vmaps(fmaphit_f2pv, volID, blockID,
							NULL, NULL);
                    hashtab_remove(fixedtab.table, fmaphit_f2pv->fhashkey);
                    setFixedMap(fmaphit_fixedID, NULL);   //f2pv displaced
					//need to free fmaphit_f2pv
                }

                //mark trace_f2pv as curr_f2pv for self-hits/misses below!
                curr_f2pv = trace_f2pv;

            }//end: fix for inconsistent trace
#endif

            assert(curr_f2pv != NULL);
			/***********************************
            //update the ioblkID for counts of self-hits/misses
            //copied from iodeduping.c, if change in 1 place, change both
            if (!bcache_already_had_flag)   //set in __arc_add 
                curr_f2pv->ioblkID = ioblkID;   //ensure counts line up -- self
            else
                curr_f2pv->ioblkID = ccache_already_had_obj_ioblkID;
			**********************************/
            return 0;
        }

	    /* Note the F2V tuple for this fixed */
	    f2v = calloc (1, sizeof(F2V_tuple_t));
    	note_f2v_tuple(f2v, volID, blockID);

		if (blockID == 10 || blockID == 33414267 || blockID == 10100928
				|| blockID == 34600770)
		{
			char *tempkey = malloc(HASHLEN+1);
			memcpy(tempkey, key, HASHLEN);
			tempkey[HASHLEN] = '\0';
			fprintf(stdout, "%s: key for blk %u = %s\n", __FUNCTION__, 
						blockID, tempkey);
			free(tempkey);
		}

		dedupf2pv = (f2pv_datum*) hashtab_search(fixedtab.table, key);
		if (dedupf2pv)
		{
#ifdef STRICT_NO_HASH_COLLISION
			oldbuf = NULL;
			if (blockID == 10 || blockID == 33414267 || 
					blockID == 10100928 || blockID == 34600770 ||
					dedupf2pv->fixedID==3750409)
				fprintf(stdout, "%s: checking get_fullfixed for dedup "
					"block = %u\n", __FUNCTION__, blockID);
			if (get_fullfixed(dedupf2pv, &oldbuf, buf))
				RET_ERR("error in get_fullfixed\n");
			if ((!collectformat && memcmp(buf, oldbuf, BLKSIZE) != 0) ||
				(collectformat && memcmp(buf, oldbuf, HASHLEN_STR-1) != 0))
			{
				char bufhuman2[HASHLEN_STR];
	    		MD5Human(key, bufhuman2);
				/* This is a case of hash-collision but different content */
				fprintf(stdout, "case hash-collision %s but diff content,ie.\n"
						"buf=%0x\noldbuf=%0x, fixedID=%u\n", bufhuman2, 
						*(unsigned int*)buf, *(unsigned int*)oldbuf, 
						dedupf2pv->fixedID);
				fcollisions++;
				free(oldbuf);
				dedupf2pv = NULL;
				goto newfixed;
			}
			free(oldbuf);
#endif
			fcollisionstp++;

			if (NULL == get_nondeduped_f2v(dedupf2pv, volID, blockID))
			{
				/* Currently, updating leader for both reads and writes,
				 * but could potentially use below check on "updateleader" flag 
				 * to update leader for only one of the cases and not the
				 * other. For example, read requests can send updateleader==1
				 * and write requests send parameter updateleader=0 or viceversa
				 */
				if (updateleader)
				{
					unmark_old_dedupfetchF(dedupf2pv);	
	    	    	f2v->dedupfetch = 1;	//mark new
				}
				else
				{
					unmark_old_dedupfetchF(dedupf2pv);	
	    	    	f2v->dedupfetch = 1;	//mark new
	    	    	//f2v->dedupfetch = 0;	//dont mark new
				}
	        	add_f2v_tuple_to_map(f2v, dedupf2pv);
			}
			else
			{
				free(f2v);
			}
			fixedID = dedupf2pv->fixedID;
#ifdef SIMREPLAY_DEBUG_SS
			if (fixedID == 3750409)
				fprintf(stdout, "%s: fixedID=%u with dedupfetch=0 "
					"for blockID=%u, key=%s\n", __FUNCTION__, fixedID, 
						blockID, key);
#ifdef SIMREPLAY_DEBUG_SS_DONE
			fprintf(stdout, "dedupfixedID = %u ", dedupf2pv->fixedID);
#endif
			/* Noting max_dedupfactor */
			if ((unsigned)slist_len(&dedupf2pv->f2vmaps) > max_dedupfactor)
			{
				max_dedupfactor = slist_len(&dedupf2pv->f2vmaps);
				//printf("Update max_dedupfactor = %u\n", max_dedupfactor);
			}
			if (slist_len(&dedupf2pv->f2vmaps) <= 9)
				dedupfactor[slist_len(&dedupf2pv->f2vmaps)]++;
#endif
#if defined(CONMAPPING_TEST) || defined(CONFIXING_TEST) || defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
    	    fprintf(fhashptr, "dedupfixedID = %u rw=%d ", 
					dedupf2pv->fixedID, rw_flag);
#else 
		UNUSED(rw_flag);			
#endif
		}
		else
		{
#ifdef STRICT_NO_HASH_COLLISION
newfixed:
#endif
    		MD5Human(key, bufhuman);
			if (strcmp(bufhuman, checkhuman) == 0)
				printf("c5a3ce558df556501097480783bfc6f7 found for blockID=%u, buf=%s\n",
					blockID, buf);
			f2pv = (f2pv_datum*) calloc(1, sizeof(f2pv_datum));
			INIT_LIST_HEAD(&f2pv->f2vmaps);
			fixedID = getNextFixedNum(initflag);
#ifdef SIMREPLAY_DEBUG_SS
			if (fixedID == 3750409)
				fprintf(stdout, "%s: fixedID=%u with dedupfetch=1 "
					"for blockID=%u, key=%s\n", __FUNCTION__, fixedID, 
						blockID, key);
#endif
			note_fixed_attrs(f2pv, key, fixedID);
	        f2v->dedupfetch = 1;
			add_f2v_tuple_to_map(f2v, f2pv);
			ret = hashtab_insert(fixedtab.table, f2pv->fhashkey, f2pv);
			if (ret == -EEXIST)
			{
				char bufhuman2[HASHLEN_STR], bufhuman3[HASHLEN_STR];
	    		MD5Human(f2pv->fhashkey, bufhuman2);
	    		MD5Human(key, bufhuman3);
				printf("-EEXIST found for blockID=%u, buf=%s\n", blockID, buf);
				printf("why re-entering %s if already exists as %s\n", 
								bufhuman2, bufhuman3);
			}
			else if (ret == -ENOMEM)
			{
				RET_ERR("why are we out of memory?\n");
			}
	        setFixedMap(f2pv->fixedID, f2pv);
#ifdef DEBUG_SS
	        assert(memcmp(key, f2pv->fhashkey, HASHLEN + MAGIC_SIZE) == 0);
#endif
#if defined(STRICT_NO_HASH_COLLISION)
	        /* For debugging only.... comment later */
			if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928 ||
					f2pv->fixedID==3750409)
				fprintf(stdout, "%s: checking get_fullfixed for new "
						"block = %u\n", __FUNCTION__, blockID);
			if (get_fullfixed(f2pv, &oldbuf, buf))
	            RET_ERR("error in get_fullfixed\n");
			if (collectformat)
		        getHashKey(oldbuf, HASHLEN_STR-1, debugkey);
			else
		        getHashKey(oldbuf, BLKSIZE, debugkey);
	        assert(memcmp(debugkey, f2pv->fhashkey, HASHLEN+MAGIC_SIZE) == 0);
	        assert(memcmp(debugkey, key, HASHLEN+MAGIC_SIZE) == 0);
			//printf("memcmp of hashkey passed after block insert\n");
			free(oldbuf);
#endif
			dedupfactor[slist_len(&f2pv->f2vmaps)]++;
#if defined(CONMAPPING_TEST) || defined(CONFIXING_TEST) || defined(PDD_BENCHMARK_STATS)  || defined(SIM_BENCHMARK_STATS)
	        fprintf(fhashptr, "fixedID = %u rw=%d ", f2pv->fixedID, rw_flag);
#endif
		}
	}	/* Next is a zeroblk */
	else
		fixedID = 0;

#if defined(CONMAPPING_TEST) || defined(CONFIXING_TEST) || defined(CONDUMPING_TEST)
    ret = processBlockf(blockID, volID, lastblk_flag, fixedID); //, key);
	if (ret)
		RET_ERR("processBlockf() error'ed\n");
#else
	if (initflag == INIT_STAGE)
	{
        ret = processBlockf(blockID, volID, lastblk_flag, fixedID); //, key);
		if (ret)
			RET_ERR("processBlockf() error'ed\n");
	}
	else 
	{
        ret = updateBlockf(blockID, volID, lastblk_flag, fixedID); //, key);
		if (ret)
			RET_ERR("updateBlockf() error'ed\n");
	}
#endif

#if defined(PDD_BENCHMARK_STATS) || defined(SIM_BENCHMARK_STATS)
	if (len != 0)
	{
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY || 
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
			len=MD5HASHLEN_STR-1;
	    char bufhuman[HASHLEN_STR];
	    MD5Human(key, bufhuman);
    	fprintf(fhashptr,
	            "%s %u %x%x%x%x blk= %u %u\n",
    	        bufhuman, len,
        	buf[len-4], buf[len-3], buf[len-2],
	        buf[len-1],
    	    volID, blockID);
	}
	else
	{
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
			len=MD5HASHLEN_STR-1;
    	fprintf(fhashptr,
	            "%s %u 0000 blk= %u %u\n",
    	        "zeroblkhash", len,
    	    volID, blockID);
	}
#endif

#if defined(CONMAPPING_TEST) || defined(CONFIXING_TEST) || defined(PDD_REPLAY_DEBUG_SS_DONE)
    //WHERE;
    int num_f2v;
    fixedmap_t *cp;
    F2V_tuple_t *ct;

    if (dedupf2pv)
        cp = dedupf2pv;
    else
        cp = f2pv;

    fprintf(stdout, " %llu", cp->fixedID);
    //fprintf(stdout, " %d", cp->fdirty);

    num_f2v = slist_len(&cp->f2vmaps);
    fprintf(stdout, " num_f2v=%d ", num_f2v);

    if (num_f2v > 0)
    {
        struct slist_head *p;
        fprintf(stdout, "[ ");
        __slist_for_each(p, &cp->f2vmaps)
        {
            ct = slist_entry(p, struct F2V_tuple_t, head);
            fprintf(stdout, "(%d)", ct->dedupfetch);
            fprintf(stdout, " %u", ct->volID);
            fprintf(stdout, " %llu:", ct->blockID);
        }
        fprintf(stdout, "]");
    }
    fprintf(stdout, "\n");
#endif

	free(key);
	free(bufhuman);
#if defined(STRICT_NO_HASH_COLLISION)
	free(debugkey);	//savemem
#endif
	return 0;
}

#if 0
/*
 * @param[in] buf
 * @param[in] len
 * @param[in] volID
 * @param[in] blockID
 * @param[in,out] prechunk
 * @param[in,out] postchunk
 * @param[in,out] seqnextp
 * @return status		
 */
int perfWriteFixing(unsigned char *buf, int len, __u16 volID, __u32 blockID)
{
	int ret = 0;
    //uint32_t ptime;  // To be noted when event occurs, is this needed? FIXME
	int numblks_to_write;
    unsigned char *blkbuf;
    unsigned char *p;
	__u16 curr_len;
	__u32 curr_blockID = blockID;

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
         * then fix here to handle it.
         */
    	ret = resumeFixing(blkbuf, BLKSIZE, volID, curr_blockID, NOINIT_STAGE,
						lastblk_flag);
	    if (ret)
    	{
        	RET_ERR("resumeFixing err in %llu perfWriteFixing\n", iter);
    	}

		p = buf + BLKSIZE;
		curr_blockID++;
        if (blkbuf)
            free(blkbuf);
    }
    return 0;
}
#endif

