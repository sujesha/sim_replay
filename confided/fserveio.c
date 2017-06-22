/* This file has the interface that receives I/O requests
 * one-by-one and serves them by PROVIDED
 */

#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include "fixing.h"
#include "slist.h"
#include "f2pv-map.h"
#include "debug.h"
#include "defs.h"
#include "per-input-file.h"
#include "serveio-utils.h"
#include "serveio.h"
#include "sync-disk-interface.h"
#include "ulist.h"
#include "utils.h"
#include "vmbunching_structs.h"
#include "voltab.h"
#include "v2f-map.h"
#include "v2p-map.h"
#include "pdd_config.h"
#include "request-generic.h"
#include "replay-plugins.h"
#include "fruntime.h"
#include "fixedtab.h"
#include "debugg.h"
#include "replay-defines.h"
#include <time.h>

unsigned char fmapdirty_flag = 0;
unsigned char fmaphit_flag = 0;
__u32 fmaphit_fixedID = 0;

extern __u64 compulsory_misses;
extern __u64 capacity_misses;
__u64 fmap_hits = 0;
__u64 fmap_misses = 0;
__u64 fmap_dirties = 0;
__u64 fixed_dedup_hits = 0;
__u64 fixed_self_hits = 0;
__u64 fixed_dedup_misses = 0;
__u64 fixed_self_misses = 0;
__u64 fmap_self_is_leader = 0;
__u64 fmap_self_is_not_leader = 0;
__u64 fmapmiss_cachehits = 0;
__u64 fmapdirty_cachehits = 0;
__u64 fmapmiss_cachemisses = 0;

extern int intraonlyflag;
extern int warmupflag;
extern struct fixedtab fixedtab;
extern int runtimemap;
extern int disksimflag;
extern int collectformat;
inline __u64 gettime(void);
extern FILE * ftimeptr;
extern vector16 * v2fmaps;

#ifdef PRO_STATS
	unsigned long ctotalreq = 0;	/* Including read/write reqs */
	unsigned long ctotalblk = 0;	/* Including read/write blks */

	unsigned long corigblkread = 0;	/* Original blks-to-be-read */
	unsigned long corigblkwrite = 0;	/* Original blks-to-be-written */

	unsigned long ctotalreadreq = 0;	/* Read req received */
	unsigned long ctotalwritereq = 0;	/* Write req received */

	unsigned long ctotalblkread = 0;	/* Count of blks to-be-read */
	unsigned long ctotalblkwrite = 0;	/* Count of blks to-be-written */
	unsigned long con_zeroblksread = 0;	/* Count of zeroblks so-not-to-be-read*/

	unsigned long con_blkread = 0;	/* Blk read on CONFIDED success */
	unsigned long con_fallback_blkread = 0;	/* Blk read on CONFIDED fail */

	/* Following asserts should hold on above variables :-
	 * -- totalreadreq + totalwritereq == totalreq
	 * -- totalblkread + totalblkwrite == totalblk
	 * -- con_blkread + fallback_blkread == totalblkread
	 * -- fallback_blkread <= origblkread
     * -- origblkwrite == totalblkwrite (since we do not optimize writes)
	 */

	/* Some derived information :-
	 * -- extra for CONFIDED read: totalblkread - (origblkread)
	 * -- extra for CONFIDED write: 0
	 * -- total extra: totalblkread - origblkread
	 */
#endif 

Node * newReusableFixedIDUList = NULL;
//static pthread_mutex_t newReusableF_mutex;

extern Node * currReusableFixedIDUList;     /* from f2pv-map.c */
extern pthread_mutex_t currReusableF_mutex;  /* from f2pv-map.c */
extern const char zeroarray[65537];

/* Fetch f2v containing the (volID, vBlkID) because this mapping is used to 
 * analyze chunk and block boundaries (mostly during block writes)
 */
F2V_tuple_t* get_nondeduped_f2v(fixedmap_t *f2pv, __u16 volID, 
				__u32 vBlkID)
{
	F2V_tuple_t *f2vt;
	struct slist_head *p;

	__slist_for_each(p, &f2pv->f2vmaps)
	{
		f2vt = slist_entry(p, F2V_tuple_t, head);
		if (vblkIDMatch(f2vt->volID, f2vt->blockID, volID, vBlkID))
			return f2vt;	/* Found */
	}

	return NULL;
}

/* mark_new_dedupfetchF: This is invoked when we do new map update (i.e.
 * 		for runtime map update upon read, or updating after new write. In both
 * 		cases, the new block is in cache right now, so this is the best 
 * 		candidate for being dedupfetch, so mark it so, and remove any old one 
 * 		if present. 
 * 		Should be invoked before add_f2v_tuple_to_map()
 */
void unmark_old_dedupfetchF(fixedmap_t *f2pv, __u16 volID)
{
	struct slist_head *p;

	/* Un-marking the other dedupfetch */
	__slist_for_each(p, &f2pv->f2vmaps)
	{
		F2V_tuple_t *f2vt = slist_entry(p, F2V_tuple_t, head);
		if (!intraonlyflag && f2vt->dedupfetch)	/* Found old tuple */
		{
			/* Disabled it and return */
			f2vt->dedupfetch = 0;	/*  old  */
			return;
		}
		else if (intraonlyflag && f2vt->volID==volID && f2vt->dedupfetch)
		{
			/* Disabled it and return */
			f2vt->dedupfetch = 0;	/*  old  */
			return;
		}
	}

	/* Should never reach here if intraonly flag is 0 */
	if (!intraonlyflag)
		assert(0);
}

/* mark_another_dedupfetchF: This is invoked if the f2v with the
 * 		dedupfetch flag is just about to be deleted from the fixedmap.
 * 		If so, we need to mark another f2v of this fixedmap as dedupfetch=1.
 * 		However, if there was only one f2v in the f2pv (then it is the
 * 		one about to be deleted), then just ignore.
 * 		FIXME: This function can/should be used for optimization, but
 * 		for now, we just mark the "next" f2v as dedupfetch=1.
 */
void mark_another_dedupfetchF(fixedmap_t *f2pv,
		__u16 *leader_volIDp, __u32 *leader_blkIDp, __u16 volID)
{
	struct slist_head *p;

	if (!intraonlyflag && slist_len(&f2pv->f2vmaps) == 1)
	{
		/* If there was only one f2v in the f2pv (then it is the
		 * 		one about to be deleted), then just ignore.
		 */
		return;
	}

	/* Marking a new dedupfetch */
    __slist_for_each(p, &f2pv->f2vmaps)
    {
      	F2V_tuple_t *f2v = slist_entry(p, F2V_tuple_t, head);
        if (!intraonlyflag && !f2v->dedupfetch) /* Found 1st f2v with dedupfetch disabled */
        {
			/* Enable new leader and return */
            f2v->dedupfetch = 1;

			/* Noting the newleaderkey information to be sent with writes */
			if (leader_volIDp)
				*leader_volIDp = f2v->volID;
			if (leader_blkIDp)
				*leader_blkIDp = f2v->blockID;

			return;
        }
		else if (intraonlyflag && f2v->volID==volID && !f2v->dedupfetch)
		{
			f2v->dedupfetch = 1;

			/* Noting the newleaderkey information to be sent with writes */
			if (leader_volIDp)
				*leader_volIDp = f2v->volID;
			if (leader_blkIDp)
				*leader_blkIDp = f2v->blockID;

			return;
		}
    }

	if (!intraonlyflag)
	{
		/* Should never reach here */
		assert(0);
	}
}

/* Find f2v tuple entry corresponding to (volID, vBlkID)
 * Return 0 for success
 */
int del_f2v_from_f2vmaps(fixedmap_t *f2pv, __u16 volID, __u32 vBlkID,
		__u16 *leader_volIDp, __u32 *leader_blkIDp)
{
	F2V_tuple_t* f2v = NULL;

	f2v = get_nondeduped_f2v(f2pv, volID, vBlkID);
	if (f2v == NULL)
	{
		RET_ERR("volID %u, vBlkID %u  not found\n", volID, vBlkID);
	}

	if (f2v->dedupfetch == 1)
		mark_another_dedupfetchF(f2pv, leader_volIDp, leader_blkIDp, volID);
	remove_f2v_tuple_from_map(f2v, f2pv);

	return 0;
}

/* add_chunkID_to_recyclelist: When chunks are over-written due to block-writes,
 * those chunk IDs can be added to recycle list so that they may be used at
 * the next possible instance.
 */
void add_fixedID_to_recyclelist(fixed_id_t fixedID)
{
	//pthread_mutex_lock(&newReusableF_mutex);		
	
	newReusableFixedIDUList = addtoUList(newReusableFixedIDUList, 
			&fixedID, sizeof(fixed_id_t));

	//pthread_mutex_unlock(&newReusableF_mutex);
}

/* recyclefixedID(x)
 *
 * 		Check if x is dedup or unique.
 * 		If unique (i.e. has only 1 V2F tuple), add x to newReusableFixedIDUList
 * 		If dedup, then unburden the mapping and free() any elements 
 * 		that had been previously malloc'ed.
 * 		return dedupstatus. (is this needed?)
 *
 * @param[in] fixedID
 * @param[in] zeroflag
 * @param[in] volID
 * @param[in] blockID
 * @return status
 */
int recyclefixedID(fixed_id_t fixedID, int nozero_flag,
						__u16 volID, __u32 blockID,
						__u16 *leader_volIDp, __u32 *leader_blkIDp)
{
	fixedmap_t *f2pv;
#ifdef SIMREPLAY_DEBUG_SS
	if (fixedID == 3750409)
		printf("fixedID=3750409 is getting deleted. Is it an only entry?\n");
#endif

	f2pv = getFixedMap(fixedID);
#ifdef DEBUG_SS
 	assert(f2pv != NULL); /* Within loop, getChunkMap() should be success */	
#endif

	if (slist_len(&f2pv->f2vmaps) > 1)
	{
		/* This fixed-blk is a deduplicated one, so fixedID not recycled */
		/* Just remove the corresponding f2v element from f2pv->f2vmaps.
		 */
		del_f2v_from_f2vmaps(f2pv, volID, blockID, 
				leader_volIDp, leader_blkIDp);
	}
	else
	{
		/* No dedup, so chunkID to be recycled */
		del_f2v_from_f2vmaps(f2pv, volID, blockID, //f2v freed
				leader_volIDp, leader_blkIDp);
		hashtab_remove(fixedtab.table, f2pv->fhashkey); //f2pv unlinked
		setFixedMap(fixedID, NULL);	//node freed from hashtab
		//free(f2pv);	
		/* A fixedID of 1 is used as a dummy to indicate when a block is
		 * dirtied by way of the first request to it being a write request.
		 * When the next write request is received, we can reach here for
		 * to reset, know that fixedID==1 doesnt necessarily mean that it
		 * is available for recycling. It may even be that it is the above
		 * dummy value. So, just ignore it in either case.
		 */

		if (nozero_flag && fixedID != DUMMY_ID)
			add_fixedID_to_recyclelist(fixedID);	//fixedID reusable
	}

	return 0;
}

static int add_new_to_old_recyclelistF()
{
	//pthread_mutex_lock(&currReusableF_mutex);		
	//pthread_mutex_lock(&newReusableF_mutex);		

	if (newReusableFixedIDUList != NULL)
	{
		currReusableFixedIDUList = appendUList(currReusableFixedIDUList, 
											&newReusableFixedIDUList);
		assert(newReusableFixedIDUList == NULL);
	}

	//pthread_mutex_unlock(&newReusableF_mutex);		
	//pthread_mutex_unlock(&currReusableF_mutex);		

	return 0;
}

/* To reset the v2f mapping, the fixedIDUList and free up malloc'ed memory */
int resetMappingsF(v2f_datum *v2f, __u16 volID, __u32 blockID, 
		__u16 *leader_volIDp, __u32 *leader_blkIDp)
{
	int ret;
    fixed_id_t fixedID;

    /* As per our implementation of note_v2f_map(), a zero vblk
     * has a v2f mapping having a fixedID == 0, indicating 
     * zero fixedblock. Thus, whether zero block or no, every vblk
     * will have a v2f map and a non-empty fixedID field.
     */
    fixedID = v2f->fixedID;

    if (notzeroF_vblk(v2f))
    {
		ret = recyclefixedID(fixedID, 1, volID, blockID, leader_volIDp,
				leader_blkIDp);
    }
    else
    {
		ret = recyclefixedID(fixedID, 0, volID, blockID, leader_volIDp,
                leader_blkIDp);
    }

	add_new_to_old_recyclelistF();

    /* Free memory, was malloc'ed in note_v2f_map() */
    //free(v2f);	should this be done here? //FIXME

    if (ret)
    {
    	RET_ERR("Error in initAndRecoverFixedIDs(%u, %u)\n",
             volID, blockID);
    }

	return ret;
}

/* mappingTrimScanF(): to scan through the mappings of all vblks
 *      being written, and for every vblk initAndRecoverFixedID()
 *      to initialize all its fixed-block-related elements and for every
 *      fixedID x in its fixedIDUList, recyclefixedID(x)
 *      (Assert that first vblk in V2F tuple is same as vblk being handled.)
 */
static int mappingTrimScanF(struct vm_pkt *blkReq, Node ** leader_volIDUListp, 
						Node ** leader_blkIDUListp)
{
	struct slist_head v2flist;
	int i = 0, rc;
	struct slist_head *p;
	__u16 leader_volID = DONTCARE_VOLID;
	__u32 leader_blkID = 0;
	int ret;

#ifdef DEBUG_SS
	assert(blkReq != NULL);
#endif
	INIT_LIST_HEAD(&v2flist);

	__u32 vBlkID = getVirtBlkID(blkReq);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "mappingTrimScanF vmname=%s\n", blkReq->vmname);
#endif
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	__u16 volID = (__u16) ret;
	__u32 numBlocks = getNumBlocks(blkReq);


	/* Get V2F map of the vblks being written */
    rc = getVirttoFixedMap(volID, vBlkID, numBlocks, &v2flist);
	if (rc && !runtimemap)
	{
		RET_ERR("some error in getVirttoFixedMap\n");
	}
	else if (rc)
	{
		/* Since mapping doesnt exist, create dummy map and set dirty flag */
		for (i=0; i < (int)numBlocks; i++)
		{
			/* These are dummy dont-care values */
			*leader_volIDUListp = addtoUList(*leader_volIDUListp, 
				&leader_volID, sizeof(leader_volID));
			*leader_blkIDUListp = addtoUList(*leader_blkIDUListp,
				&leader_blkID, sizeof(leader_blkID));
		}

		/* Note here that vblk is dirty */
		for (i=0; i < (int)numBlocks; i++)
		{
			v2f_datum *v2f = (v2f_datum*)calloc(1, sizeof(v2f_datum));
			v2f->fdirty = 1;
			v2f->fixedID = DUMMY_ID;	//should be non-zero!
		    /* Updating the v2fmaps data structure */
		    v2fmaps_set(v2fmaps, volID, vBlkID+i, (void*)v2f);
		}

		return 0;
	}
	if (slist_len(&v2flist) != (int) numBlocks)
	{
		RET_ERR("Number of mappings fetched %d is not equal to number"
						" requested %u\n", slist_len(&v2flist), numBlocks);
	}

	/* Iterate through list of v2f maps, and through fixedIDUList per vblk */
	i = 0;
	__slist_for_each(p, &v2flist)
	{
    	v2f_datum *v2f;
		v2f = slist_entry(p, v2f_datum, head);
		leader_volID = DONTCARE_VOLID;	/* init */
		leader_blkID = 0;	/* init */

		/* If metadata already dirty, then nothing to reset here */
		if (fixed_dirty(v2f))
		{
			*leader_volIDUListp = addtoUList(*leader_volIDUListp, 
				&leader_volID, sizeof(leader_volID));
			*leader_blkIDUListp = addtoUList(*leader_blkIDUListp,
				&leader_blkID, sizeof(leader_blkID));
			continue;	/* metadata dirty, no need to reset here */
		}
		v2f->fdirty = 1;	/*Proceeding to make metadata dirty, mark it */

		if (resetMappingsF(v2f, volID, vBlkID+i, &leader_volID, &leader_blkID))
			RET_ERR("resetMappingsF error\n");

		/* Creating list for newleaderkey/newleader_ioblk */
		*leader_volIDUListp = addtoUList(*leader_volIDUListp, 
				&leader_volID, sizeof(leader_volID));
		*leader_blkIDUListp = addtoUList(*leader_blkIDUListp,
				&leader_blkID, sizeof(leader_blkID));

		i++;
	}

	return 0;
}

/* get_deduped_f2v: Aim is to locate the deduped vblk (which is to
 *      be fetched instead of the originally requested vblk). Input
 *      here is the f2pv and we locate the deduped vblk based on
 *      dedupfetch flag. This is invoked via get_idx_into_vblklist()
 * 		This is the CONFIDED redirection logic in play.
 * @f2pv[in]: fixedmap of above concerned fixedblock
 * @return: the f2v tuple marked with dedupfetch:1 for this f2pv
 */
F2V_tuple_t * get_deduped_f2v(fixedmap_t *f2pv, __u16 volID)
{
    struct slist_head *p;
    struct F2V_tuple_t *f2v = NULL;

	/*
     * F2V_tuple_t has dedupfetch:1 flag, so identify the dedup 
     *      tuple, calculate vblkid corresponding to requested block and 
     *      hence get index of "needed" block.
     */
	if (f2pv->fixedID == 3750409)
	{
       	printf("%s: [", __FUNCTION__);
    	__slist_for_each(p, &f2pv->f2vmaps)
	    {
    	    f2v = slist_entry(p, struct F2V_tuple_t, head);
			printf(" %u", f2v->blockID);
		}
		printf(" ]\n");
	}

	/* Iterating through each f2v within given fixedmap */
    __slist_for_each(p, &f2pv->f2vmaps)
    {
        f2v = slist_entry(p, struct F2V_tuple_t, head);
		if (intraonlyflag && f2v->volID == volID && f2v->dedupfetch)
			return f2v;
        else if (f2v->dedupfetch)	/* Found f2v with dedupfetch enabled */
			return f2v;
	}
	if (!intraonlyflag)
	{
    	/* At least one of the f2v should have dedupfetch == 1, so
	     * we should never reach here!
    	 */
		assert(0);
	}
	else
	{
		/* For multi-VM traces, if there is no entry already for the volID
		 * being searched for, we will reach here and return an f2v
		 * corresponding to another VM. Check for this in caller, and
		 * act accordingly.
		 */
		return f2v;
	}
}

__u16 get_fullvblkF(struct preq_spec **preql, int *nreq, v2f_datum *v2f,
		__u8 *content, __u16 volID)
{
	fixedmap_t *f2pv = NULL;
	F2V_tuple_t *deduped_f2v;
	fixed_id_t nextID;
#if defined(REPLAYDIRECT_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s: vblkID = %u\n", __FUNCTION__, v2f->blockID);
#endif 

	nextID = v2f->fixedID;
#ifdef SIMREPLAY_DEBUG_SS_DONE
		fprintf(stdout, "nextID=%u\n", nextID);
#endif
	f2pv = getFixedMap(nextID);
#ifdef DEBUG_SS
	assert(f2pv != NULL);
#endif
	if (disksimflag)
		assert(content != NULL);
	if (fixed_dirty(v2f))
    {
        /* Since fixed is dirty, will have to fallback to original read */
        return 0;
    }

	/* volID parameter is especially to simulate intra-only similarity */
	deduped_f2v = get_deduped_f2v(f2pv, volID);
    if (deduped_f2v == NULL)
        RET_ERR("could not get_deduped_f2pv for nextID=%u\n", nextID);

	/* In multi-VM traces with intraonlyflag set, need to check that volID
	 * also matches, else fallback to original read.
	 */
	if (intraonlyflag && deduped_f2v->volID != volID)
		return 0;

	if (elongate_preql(preql, nreq))
	{
    	RET_ERR("realloc error for preql\n");
	}
	create_preq_spec(deduped_f2v->volID, deduped_f2v->blockID, BLKSIZE, 
				1, content, 0, BLKSIZE - 1, *preql+(*nreq - 1));

	return BLKSIZE;
}

/* This is CONFIDED functionality for every read request.
 * Assumes that num bytes requested is integral multiple of BLKSIZE.
 * This function intercepts a read request, perform chunk deduplication logic
 * and substitutes original read request with alternates (if dedup deems it)
 */
int confideReadRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq)
{
	__u32 vBlkID;
	__u16 volID;
	int ret;
	v2f_datum *v2f;
	unsigned int i = 0, rc=0;
	struct slist_head v2flist;
	__u16 numbytes_accounted = 0;
	__u16 rem_bytes;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

#ifdef DEBUG_SS
	assert(blkReq != NULL);
	assert(preql != NULL && *preql == NULL);

 	/* Assumes that num bytes requested is integral multiple of BLKSIZE */
	assert(blkReq->nbytes % BLKSIZE == 0);	/* Found to be always true */
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

	for (i=0; i<blkReq->nbytes/BLKSIZE; i++)
	{
		assert(i==0 ||
				(!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY &&
				 !DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY &&
				 !DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY));	
				/* if disk simulation or collect format, do not expect
				 * multi-block requests, only single block at a time for now.
				 */
		vBlkID++;
		rem_bytes = BLKSIZE;

	    /* Get V2F map of current blockID.
		 * Since fetch is only for 1 vblk => if it is zeroblk, then v2f == NULL 
		 */
		INIT_LIST_HEAD(&v2flist);
	    rc = getVirttoFixedMap(volID, vBlkID, 1, &v2flist);
		if (!rc)
		{
	    	/* V2F found but check its dirty-flag */
#ifdef DEBUG_SS
			assert(slist_len(&v2flist) == 1);
#endif
    		v2f = slist_entry(slist_first(&v2flist), v2f_datum, head);
		    if (fixed_dirty(v2f)) /* dirty map */
	    	{
		        /* Since fixed is dirty, have to fallback to original read */
				fmapdirty_flag = 1;
				numbytes_accounted = 0;
				if (!warmupflag)
					fmap_dirties++;
		    }
			else
			{
				if (*nreq < 0)
					*nreq = 0;	/* initialize here */
	
				numbytes_accounted = get_fullvblkF(preql, nreq, v2f, 
							blkReq->content, volID);
				if (!warmupflag)
				{
					__u32 orig_ioblk;
				    if (getVirttoPhysMap(volID, vBlkID, &orig_ioblk))
				        VOID_ERR("getVirttoPhysMap error'ed\n");
					if ((*preql+(*nreq-1))->ioblk == orig_ioblk)
						fmap_self_is_leader++;
					else
						fmap_self_is_not_leader++;
				}
			}
		}
		else
			numbytes_accounted = 0;	//since no mapping found or ZEROBLK_FLAG

		if (rc == ZEROBLK_FLAG)	//from above getVirttoFixedMap() call
		{
			numbytes_accounted = BLKSIZE;	//no disk fetch since zeroblk
			rem_bytes = 0;
			*nreq = 0;
			//fmap_hits++;	//dont increment here, it is handled below.
#ifdef PRO_STATS
			if (!warmupflag) {
				con_zeroblksread++;
				ctotalblk++;
			}
#endif
			//dont return here, since this is only 1 blk in multi-block request!
		}
		else if (rc && !runtimemap)			
		{
			/* V2F not found, and this was supposed to be apriori map! */
			RET_ERR("Couldnt find V2F map for specified read req (%u, %u)\n",	
					volID, vBlkID);
		}
		else
		{
			/* V2F dirty or V2F not found with runtimemap map, or V2F found */
	        if (numbytes_accounted==0)	/* indicates no alternatives found */
    	    {
				if (disksimflag)
					assert(blkReq->content != NULL); /* 1 block only */
				else
					assert(blkReq->content == NULL);

				if (*nreq < 0)
					*nreq = 0;	/* initialize here */

				/* V2F not found with runtime map, create disk request
				 * with original block number
				 */
				if (elongate_preql(preql, nreq))
				{
					RET_ERR("realloc error for preql\n");
				}
#ifdef SIMREPLAY_DEBUG_SS_DONE
				fprintf(stdout, "vBlkID here=%u\n", vBlkID);
#endif
				create_preq_spec(volID, vBlkID, 
						BLKSIZE, blkReq->rw, blkReq->content, 
						0, BLKSIZE-1, *preql+(*nreq-1));
				rem_bytes -= BLKSIZE;
				if (!warmupflag)
					fmap_misses++;
				//compulsory_misses++;	dont count here, count only if MISSed
		    }	
			else
			{
				/* V2F found, and redirected requests already created
				 * above either as ZEROBLK or in get_fullvblkF()
				 */
				rem_bytes -= numbytes_accounted;
				if (!warmupflag)
					fmap_hits++;
				fmaphit_flag = 1;	//set flag for metadata hit
				fmaphit_fixedID = v2f->fixedID; //used in resumeFixing to
												//handle inconsisten traces!
			}
		}

#ifdef PRO_STATS	
		if (!warmupflag) {
			ctotalblk += (*nreq);
			ctotalblkread +=(*nreq);

    	    if (numbytes_accounted==0)
	        {
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
        		fprintf(stdout, "CONFIDED could not get alternates\n");
#endif
    	        con_fallback_blkread++;
	        }
			else if (numbytes_accounted == BLKSIZE && *nreq==0)
    	    {
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
	            fprintf(stdout, "CONFIDED just serves zeroblk!\n");
#endif
        	}				
    	    else if (numbytes_accounted == BLKSIZE)
		    {
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
	    	    fprintf(stdout, "CONFIDED got alternates!\n");
#endif
		        con_blkread += (*nreq);
		    }
		}
#endif
		/* Whether alternative or not, rem_bytes should be 0, else error */
        if (rem_bytes > 0)
       	{
	        RET_ERR("fatal error in confideReadRequest\n");
	    }
	}	/* loop for multiple vblks requested within single vm_pkt */

#ifdef PRO_STATS	
	if (!warmupflag) {
		ctotalreq++;
		ctotalreadreq++;
		corigblkread += (blkReq->nbytes/BLKSIZE);
	}
#endif

	return 0;
}

void* f_mapupdate_sub(void *arg)
{
	int rc = 0;
	struct preq_spec **preql;
	struct vm_pkt *blkReq;
	int vop_iter;
	struct mapupdate_info *mip;
	unsigned long long stime=0, etime=0;

	mip = (struct mapupdate_info*)arg;
	preql = mip->preql;
	blkReq = mip->blkReq;
	vop_iter = mip->vop_iter;

	stime = gettime(); /* START CONFIDED map-update for write time */
	rc = f_mapupdate(preql, blkReq, vop_iter);
	if (rc < 0)
		fatal(NULL, ERR_USERCALL, "Error in f_mapupdate\n");

    etime = gettime();  /* END CONFIDED map-update for write time */
    ACCESSTIME_PRINT("confided-map-update-for-write time: %llu %d\n",
                     etime - stime, vop_iter);

	return NULL;
}

/* This is CONFIDED functionality for every write request.
 * This function intercepts a write request, updates mapping tables and 
 * returns control. It is assumed that a single write request received 
 * here could potentially have write content for multiple blocks, 
 * indicated by "bytes" within struct vm_pkt.
 *
 * Basic function:
 * 
 * Split into 2 possibilities:-
 * ----------------------------
 * Possibility I: Existing block being over-written
 * Possibility II: New block to write (earlier pointed to zero chunk/block)
 *
 * Output for write request is just a mapped io_pkt containing the pblk
 * instead of (volID, vblkID). 
 *
 * Return 0 upon success */
int confideWriteRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq)
{
	int ret = 0;
	unsigned char *buf = NULL;
	unsigned long long stime=0, etime=0;
	__u32 blkID;
	__u16 volID;
	__u16 *leader_volIDp = NULL;
	__u32 *leader_blkIDp = NULL;
	Node *leader_volIDUList = NULL, *leader_blkIDUList = NULL;
	int numitems, i;

#ifdef DEBUG_SS
	assert(blkReq != NULL);
	assert(preql != NULL && *preql == NULL);
#endif

	if (disksimflag)
		assert(blkReq->nbytes == BLKSIZE);

	__u32 firstvBlkID = getVirtBlkID(blkReq);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "blkReq->vmname=%s\n", blkReq->vmname);
#endif
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	volID = (__u16) ret;
	ret = 0;

	/* Get data buf from blkReq since it is a write request */
	if (retrieveBuf(&buf, blkReq))
		RET_ERR("retrieveBuf() error'ed\n");

	stime = gettime();	/* START CONFIDED map-invalidate time */
	/* Scan through and reset mappings of all vblks being written */
	if (mappingTrimScanF(blkReq, &leader_volIDUList, &leader_blkIDUList))
		RET_ERR("mappingTrimScanF() error'ed\n");
	etime = gettime();	/* END CONFIDED map-invalidate time */
	ACCESSTIME_PRINT("confided-invalidate time: %llu %d rw(%d)\n",
		 etime - stime,
         blkReq->nbytes/BLKSIZE,
		 blkReq->rw);

	/* Consider multi-block writes */
	*nreq = 0;
	blkID = firstvBlkID;

	assert(DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY || 
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
	if (disksimflag)
		assert(blkReq->nbytes == BLKSIZE);

	/* Map update upon write done later in f_mapupdate_sub() thread */ 

	/* Creating disk write requests, and noting newleader_ioblk */
	numitems = ulistLen(leader_volIDUList);
	for (i=0; i < numitems; i++)
	{
		/* Create to-be-executed requests */
		if (elongate_preql(preql, nreq))
		{
			RET_ERR("realloc error for preql in confideWriteRequest\n");
		}
		create_preq_spec(volID, blkID, BLKSIZE, blkReq->rw, 
					blkReq->content + (*nreq-1) * BLKSIZE, 
					0, 0, *preql+(*nreq-1));

		leader_volIDp = (__u16*)popUList(&leader_volIDUList);
		leader_blkIDp = (__u32*)popUList(&leader_blkIDUList);

		if (*leader_volIDp != DONTCARE_VOLID)		
		{
	    	if (getVirttoPhysMap(*leader_volIDp, *leader_blkIDp, 
					&((*preql+(*nreq-1))->newleader_ioblk)))
    	    	VOID_ERR("getVirttoPhysMap error'ed\n");
		}
		else
		{
			/* If newleader set to same as old, indicates this is dont care! 
			 * Use this condition in add_to_cache()
			 */
			(*preql+(*nreq-1))->newleader_ioblk = (*preql+(*nreq-1))->ioblk;
		}

		free(leader_volIDp);
		free(leader_blkIDp);

		blkID++;
	}
	free(buf);
	assert(leader_volIDUList == NULL && leader_blkIDUList == NULL);

#ifdef PRO_STATS
	if (!warmupflag) {
	ctotalreq++;
	ctotalwritereq++;
	ctotalblk += (*nreq);
	ctotalblkwrite += (*nreq);
	corigblkwrite += (*nreq);
	}
#endif

#ifdef DEBUG_SS
	assert(*nreq == (int)blkReq->nbytes/BLKSIZE);
#endif

	return ret;
}

