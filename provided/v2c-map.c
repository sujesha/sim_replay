/* This file stores the virtual-to-chunk mapping in c++ vectors.
 */

#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <asm/types.h>
#include <string.h>
#include "vector16.h"
#include "vector32.h"
#include "utils.h"
#include "v2c-map.h"
#include "v2p-map.h"
#include "debug.h"
#include "slist.h"				/* list_hed */
#include "pdd_config.h"
#include "chunking.h"
#include "serveio.h"
#include "ulist.h"
#include "unused.h"

#if defined(PROMAPPING_TEST)  || defined(PDD_REPLAY_DEBUG_SS)
	extern FILE * fhashptr;
#endif

int v2cmaps_alive = 0;

//char zeroblkhash[10];
vector16 * v2cmaps = NULL;

extern int runtimemap;

/* Extern'ed from chunking.c */
extern Node * chunkIDUList;
extern char *V2PmapFile;		/* input V2P mapping file */

/* create_v2c_mapping_space: Use this to initialize the v2cmaps 2D vector
 * before starting off pro_scan_and_process or pdd_preplay threads
 */
void create_v2c_mapping_space()
{
	if (v2cmaps_alive == 1)
	{
        fprintf(stdout, "v2cmaps already non-NULL\n");
        return;
    }
	v2cmaps_alive = 1;
#ifdef PDDREPLAY_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	v2cmaps = calloc(1, sizeof(vector16));
	vector16_init(v2cmaps);

	return;
}

/* v2cmaps_free: Use this to free the entire v2cmaps mapping. This should
 * be done before exiting the program at the very end.
 */
void free_v2cmaps(void)
{
	__u16 i;		/* For iterating over volID */
	__u32 j;		/* For iterating over vblkID */
	vector32 *ptri;
	v2c_datum *ptrj;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	if (v2cmaps_alive == 0)
	{
		fprintf(stdout, "v2cmaps being freed already\n");
		return;
	}
	v2cmaps_alive = 0;

	for (i = 0; i < vector16_size(v2cmaps); i++)	/* Iterating over volID */
    {
        if ((ptri = vector16_get(v2cmaps, i)) != NULL)	
        {
            for (j = 0; j < vector32_size(ptri); j++)/* Iterating over vblkID */
            {
                if ((ptrj = (v2c_datum*) vector32_get(ptri, j)) != NULL)
				{
					if (ptrj->chunkIDUList)
						freeUList(ptrj->chunkIDUList);
					free(ptrj);		/* Freeing the vm_info node */
				}
			}
			free(ptri->data);
			free(ptri);		/* Freeing the vector per volume */
		}
	}
	free(v2cmaps->data);
	free(v2cmaps);
	fprintf(stdout, "free_v2cmaps done now\n");
}

/* v2cmaps_set: This function sets a v2c_datum at location(x,y) 
 * within 2D vector v2cmaps. Use this in processBlock() and updateBlock()
 */
void v2cmaps_set(vector16 *v2cmaps, __u16 x, __u32 y, void *e)
{
    vector32 *ptr;		/* Holds single volume row */

    ptr = vector16_get(v2cmaps, x);

	//Lock volume row
    if (ptr == NULL)
    {
        /* Encountered first blk of this VM, so create VM's vector */
        ptr = calloc(1, sizeof(vector32));
        vector32_init(ptr);
        vector16_set(v2cmaps, x, ptr);  /* Add newly malloc'ed to 2D vector */
    }
    vector32_set(ptr, y, e);
	//Unlock volume row

    return;
}

/** v2cmaps_get: This function retrieves a v2c_datum from location (x,y)
 * 		within 2D vector v2cmaps. Use this function in updateBlock()
 * 		and getVirttoChunkMap().
 * @v2cmaps[in]: The existing V2C maps
 * @x[in]: Volume ID to be looked up into v2cmaps
 * @y[in]: Block ID to be looked up into v2cmaps
 * @return: V2C found at (x,y) in v2cmaps
 */
void *v2cmaps_get(vector16 *v2cmaps, __u16 x, __u32 y)
{
    vector32 *ptr;	/* to hold 1D array corresponding to vol ID x */
	v2c_datum *e;	/* to hold output V2C */

    if (x >= v2cmaps->size)
    {
		if (!runtimemap)
	        fprintf(stderr, "v2cmaps_get: index %u>=v2cmaps->size %u\n", 
						x, v2cmaps->size);
        return NULL;
    }

    ptr = vector16_get(v2cmaps, x);	/* 1D array corresponding to vol ID */

	//Lock volume row? FIXME
    if (ptr == NULL)
    {
		if (!runtimemap)
        	fprintf(stderr, "v2cmaps_get: VM doesnt exist at index %u\n", x);
        return NULL;
    }
	e = (v2c_datum*) vector32_get(ptr, y);	/* requested element */
	//Unlock volume row? FIXME
	
    if (e==NULL && !runtimemap)
        fprintf(stderr, "v2cmaps_get:VM doesnt have block at index %u\n", y);

    return e;
}

/* multichunk_vblk: Checks whether given v2c maps to two or
 * 		more chunks.
 */
int multichunk_vblk(v2c_datum *v2c)
{
	if (ulistLen(v2c->chunkIDUList) > 1)
		return 1;
	else
		return 0;
}

int cvblk_dirty(v2c_datum *v2c)
{
    if (v2c->cdirty)
    {
#ifdef PRO_DEBUG_SS
        fprintf(stdout, "Block is dirty, so fetch original block itself\n");
#endif
        return 1;
    }
    return 0;
}

#if 0
/* 
 * Hash for zero block is to be stored as all zeroes (FIXME change this 
 * all zeroes hash value if it is not okay).
 */
static void note_bhashkey_zerovblk(v2c_datum *v)
{
    memcpy(v->bhashkey, zeroblkhash, HASHLEN + MAGIC_SIZE);
}
#endif

/** notzero_vblk -- checking if vblk is a zero block.
 *
 * @v2c[in]: V2C of the block being checked.
 * @return: flag to indicate zero blk of not.
 */
int notzero_vblk(v2c_datum *v2c)
{
	chunk_id_t *cid;
#ifdef REPLAYDIRECT_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
#if 0
    if (memcmp((v2c->bhashkey, zeroblkhash, HASHLEN + MAGIC_SIZE)))
        return 1;
    else
        return 0;
#endif

#ifdef METADATAUPDATE_UPON_WRITES
	/* Even if the block is a zero block, it should have at least one chunkID */
	if (ulistLen(v2c->chunkIDUList) < 1)
		RET_ERR("%s: unexpected chunklist len = %u\n", __FUNCTION__,
			ulistLen(v2c->chunkIDUList));
#else
	/* If metadata is not being updated upon writes, if first access to a 
	 * block is a write, a v2c is created with NULL chunkIDUList and if the
	 * next access is also a write, here we get 0 length of chunkIDUList.
	 * It doesnt matter, it is not an error. So return 1 to indicate non-zero.
	 */
	if (ulistLen(v2c->chunkIDUList) == 0)
	{
		assert(v2c->cdirty == 1);
		return 1;
	}
#endif

	/* Get the first chunkID from the list for this block */
	cid = getIndexedNode(v2c->chunkIDUList, 0);
	if (*cid != 0)	/* chunkID != 0 indicates non-zero block */
	{
#ifdef REPLAYDIRECT_DEBUG_SS
		fprintf(stdout, "found non-zero GOODBLK\n");
#endif
		return 1;
	}
	else /* First chunkID == 0 indicates zero block */
	{
#ifdef REPLAYDIRECT_DEBUG_SS
		fprintf(stdout, "found ZEROBLK\n");
#endif
		return 0;
	}
}

#if 0
/* Is invoked from note_v2c_map() */
void note_block_attrs(v2c_datum *v2c, unsigned char *key,
            __u32 blockID, __u16 volID, int coincide_flag)
{
	UNUSED(blockID);
	UNUSED(volID);
    //v2c->blockID = blockID; //FIXME: is this redundant?
    //v2c->volID = volID; //FIXME: is this redundant?

    /* FORCECOINCIDE indicates that previous vblk & chunk's end boundaries
     * have coincided due to the fact that curr vblk is zero block. Store
     * some "special value" as hash of zero block 
     */
    if (coincide_flag == FORCECOINCIDE)
    {
        //assert(!memcmp(key, "", HASHLEN + MAGIC_SIZE));
        note_bhashkey_zerovblk(v2c);
        return;
    }

    /* If HASHMAGIC is disabled, MAGIC_SIZE = 0, so wont get copied anyway */
    memcpy(v2c->bhashkey, key, HASHLEN + MAGIC_SIZE);

}
#endif

/* Handles both zero chunkID and non-zero chunkIDs 
 * Is invoked from processBlock() and updateBlock().
 */
int note_v2c_map(v2c_datum *v2c, chunk_size_t *lastoffsetminus1,
        //unsigned char *key, __u32 blockID, __u16 volID, 
		Node **chunkIDUListp, 
		int len_tillnow, int coincide_flag, int lastblk_flag)
{
#ifdef DEBUG_SS
	assert(v2c != NULL);
#endif
    //note_block_attrs(v2c, key, blockID, volID, coincide_flag);

    /* FORCECOINCIDE indicates that previous vblk & chunk's end boundaries
     * have coincided due to the fact that curr vblk is zero block.
     * Since this is zero vblk, its v2c map shows chunkIDUList containing
     * single chunk with ID = 0 and both start and end offsets into the
     * chunk are "dont care" value (it is dec_chunkoffset(0) here, change
     * this later if it is not okay).
     * The end_offset_into_chunk value has already been noted for
     * the previous vblk in the previous iteration. So, we need not
     * worry about it here. 
     */
    if (coincide_flag == FORCECOINCIDE)
    {
		chunk_id_t zeroc = 0;
#ifdef DEBUG_SS
		assert(isEmptyUList(*chunkIDUListp));
#endif
        v2c->start_offset_into_chunk = dec_chunkoffset(0);
        v2c->end_offset_into_chunk = dec_chunkoffset(0);

        /* zero chunk in list*/
        *chunkIDUListp = addtoUList(*chunkIDUListp, &zeroc, sizeof(chunk_id_t));
        return 0;
    }

#if 0
/* The manipulation of *lastoffsetminus1 is already happening in the 
 * caller i.e., updateBlock() and processBlock(). Not needed here.
 */
    /* Earlier, (prevBoundaryChunkNum.chunkLength-1 == lastoffsetminus1)
     * was supposed to be the check for previous block & chunk having
     * coinciding boundaries, but now replacing it with below logic of
     * checking for empty chunkIDUList. In that case, 
     * prevBoundaryChunkNum.chunkLength had to be here post-facto and not 
     * previously initialized in last round, because in last round, the 
     * chunk is not yet ready except for its chunk number. So we couldnt
     * do a length comparison at that time, which is required here */

    /* This is called when a block boundary is encountered.
     * If at this time, the chunkIDUList is empty, it implies that ever
     * since this block's chunking began, no chunk boundary was 
     * encountered so far => this is either the very first block (to chunk)
     * or the previous block & chunk end boundaries had coincided! */
    if (ulistLen(chunkIDUList) == 0) 
    {
        /*start next time at offset 0 of the chunk */
        *lastoffsetminus1 = dec_chunkoffset(0); 
    }
#endif

    /* Only the start and end offsets need to be noted separately. The chunk
     * IDs (including start and end) are already listed in chunkIDUList 
	 */
    switch (lastblk_flag)
	{
		case POSTCHUNK_FULL_LASTBLK:
		case NOPOSTCHUNK_LASTBLK:
		case NOT_LASTBLK:
		case GOODBLK_FLAG:
		case SCAN_FIRSTBLK:
		case ULTIMATE_LASTBLK:
		case ONLYBLK:
    			v2c->start_offset_into_chunk = inc_chunkoffset(*lastoffsetminus1);
    			v2c->end_offset_into_chunk = len_tillnow - 1;
                break;
		case POSTCHUNK_PARTIAL_LASTBLK:
				v2c->start_offset_into_chunk = inc_chunkoffset(*lastoffsetminus1);
                break;
    }
	v2c->cdirty = 0;	/* Reset the dirty flag */

    return 0;
}

/* Is invoked only for scanning phase, so use chunkIDUList */
int linkChunkIDUList(v2c_datum *v2c)
{
    /* Linking the list to the v2c mapping node */
    v2c->chunkIDUList = chunkIDUList;
    chunkIDUList = NULL;

#ifdef PDD_REPLAY_DEBUG_SS_DONE
	if (v2c->chunkIDUList == NULL)
	{
		fprintf(stdout, "[NULL]\n");
		return 0;
	}
	int num_chunkid, k;
	num_chunkid = ulistLen(v2c->chunkIDUList);
	fprintf(stdout, "[ ");
	for (k = 0; k < num_chunkid; k++)
	{
		chunk_id_t chunkID;
		chunkID = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, k);
		fprintf(stdout, "%u ", chunkID);
	}
	fprintf(stdout, "]");
	fprintf(stdout, "chunkIDUList NULLified\n");
#endif

    return 0;
}

/** getVirttoChunkMap -- Look up V2C mapping for given volume (VM) and vblkID, 
 * and return linked list (v2clistp) of mapping of "count" number of items.
 *
 * @volID[in]: volume ID of requested block(s)
 * @vBlkID[in]: block ID of first requested block
 * @count[in]: number of blockwhose V2C to be retrieved
 * @v2clistp[out]: list of v2c maps or NULL if ALL count # of vblks are zeroblks
 * @return: status
 */
int getVirttoChunkMap(__u16 volID, __u32 vBlkID, __u16 count, 
				struct slist_head *v2clistp)
{
    int success = 0;
    __u32 i;
    v2c_datum *v2c;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

    for (i = 0; i < count; i++)
    {
		int ret;
        v2c = (v2c_datum*) v2cmaps_get(v2cmaps, volID, vBlkID + i);
        /* A zero vblk will still return v2c map containing chunkID = 0
         * So, check to see whether we have hit a non-zero block yet.
         * If so, success = 1, else not.
         */
		if (v2c == NULL)
		{
			if (!runtimemap)
			{
				RET_ERR("vblkID %u beyond the capacity of VMID %u, check %s\n",
					vBlkID + i, volID, V2PmapFile);
			}
			else
				return -1;
		}
        ret = notzero_vblk(v2c);
		if (ret < 0)
		{
			printf("%s: notzero_vblk err for block %u\n", __FUNCTION__, 
				vBlkID + i);
			return -2;
		}
		if (ret == 1)
            success = 1;
#if 0
		else if (ret == -1)	/* error condition check */
			RET_ERR("vblk (%u, %u) has empty chunkIDUList?? Fix this.\n",
			volID, vBlkID + i);
#endif

        /* Add the v2c to v2clistp */
        slist_add(&v2c->head, v2clistp);
    }

    /*  If ALL count # of vblks are zeroblks, i.e. we didn't find any non-zero
     *  block, return NULL.
     */
    if (success == 0)
    {
#ifdef SIMREPLAY_DEBUG_SS
        printf("Only zero blocks encountered at (%u, %u), no GOODBLK\n",
						volID, vBlkID);
#endif
		return ZEROBLK_FLAG;
    }

    /*  Else retain their v2c maps as-is within the list
     *  and return the list.
     */
    return 0;
}

/* addChunkIDtoPrev2c: Adding input chunkID to the v2c of blockID-1 .
 * 					This is used because sometimes when a block boundary is 
 * 					encountered, its "last" chunkID may still be "under 
 * 					construction". In this case, whenever the chunk becomes 
 * 					ready (i.e. when a future block is processed), it needs 
 * 					to be added to the former WASNOTREADY block.
 *
 * @chunkID[in]: to-be-added chunkID
 * @volID[in]: volume ID
 * @blockID[in]: vblk whose v2c is to be manipulated
 */
void addChunkIDtoPrev2c(chunk_id_t chunkID, __u16 volID, __u32 bID)
{
	v2c_datum *v2c;
	__u32 blockID = bID - 1;	/* to get previous block */

	/* Retrieve v2c of volID, blockID */
	v2c = (v2c_datum*) v2cmaps_get(v2cmaps, volID, blockID);

	/* Add chunkID to chunkIDUList */
	v2c->chunkIDUList = addtoUList(v2c->chunkIDUList, &chunkID, 
					sizeof(chunk_id_t));

	return;
}

#ifndef NONSPANNING_PROVIDE
/* While encountering a block boundary (BEFOREBOUND), chunk may still be 
 * "under construction" if the previous boundary was non-coinciding. 
 * fixPrevBlock() is invoked in such a case, so that all the blocks
 * from *prevBoundaryBlkNump to currBlockID-1 can have their v2c
 * modified with chunkID added to their chunkIDUList. Thus,
 * one or more blocks can be affected by a single invocation of this.
 * It is also invoked after every block boundary (AFTERBOUND) if it is 
 * non-coinciding (WASNOTREADY), so that *prevBoundaryBlkNump can be 
 * noted for next time.
 *
 * @param[in] chunkID
 * @param[in] currBlockID
 * @param[in] chunklist_stat
 * @param[in] foundchunk
 * @param[in] before_flag
 *                          
 */
int fixPrevBlock(int initflag, chunk_id_t chunkID, __u32 currblockID,
              __u16 volID, int chunklist_stat, int foundchunk, int before_flag)
{
    static __u32 prevBoundaryBlkNum = 0;	/*static, to remember */
	static __u16 preVolID= 0;				/* static, to remember */
	static __u16 iopreVolID= 0;				/* static, to remember */

    /* These are pointers to the static variable or global variables above */
    __u32 *prevBoundaryBlkNump;
	__u16 *preVolIDp;

    if (initflag == INIT_STAGE)
    {
        prevBoundaryBlkNump = &prevBoundaryBlkNum;
		preVolIDp = &preVolID;
    }
    else
    {
        prevBoundaryBlkNump = &ioprevBoundaryBlkNum;
		preVolIDp = &iopreVolID;
    }

#ifdef PRO_DEBUG_SS
    WHERE;
#endif

	/* Invoked after processing of every block boundary.... */
    if (before_flag == AFTERBOUND && initflag == INIT_STAGE)
    {
        if (chunklist_stat == WASREADY)
        {
			__u32 lastblk;
			/* This when zeroblk or coinciding chunk-blk boundaries.
             * So, mark to adjust v2c of blockID+1 onwards, next time 
			 */
		    if (getLastvBlk(*preVolIDp, &lastblk))
		        RET_ERR("volume ID %u has no info\n", *preVolIDp);
			if (currblockID == lastblk && volID == *preVolIDp)
			{
				/* This is the last block of this volume, so marking should be
				 * for the first block of next volume 
				 */
#ifdef SIMREPLAY_DEBUG_SS
 	           fprintf(stdout, "fixPrevBlock mark first block of next volume\n");
#endif
                *prevBoundaryBlkNump = 0;
                *preVolIDp = volID + 1;     /* assuming INIT_STAGE */
			}
			else
			{
	            *prevBoundaryBlkNump = currblockID + 1;
				*preVolIDp = volID;
			}
        }
        else if (foundchunk)
        {	
            /* Not a zeroblk and found at least one chunk for this blk.
			 * So, mark to adjust v2c of blockID onwards, next time 
			 */
            *prevBoundaryBlkNump = currblockID;
			*preVolIDp = volID;
        }
        //Else !foundchunk || chunklist_stat == WASNOTREADY, no change, exit
        return 0;
    }

	/* Invoked before processing of the block boundary.... */
    /* We will reach here if and only if (before_flag == BEFOREBOUND)
     * and we found chunk in current block
     */
    if (chunklist_stat == WASNOTREADY && initflag == INIT_STAGE)
    {
		__u32 blk;
		/* If previous block(s) were in previous volID, but now we are in
		 * the next volID (this is possible during apriori mapping scan),
		 * then we need to do 2 loops, instead of just 1. This condition
		 * can be identified as *prevBoundaryBlkNump > currblockID.
		 */
		if (*prevBoundaryBlkNump > currblockID)			/* across 2 volID */
		{
#ifdef SIMREPLAY_DEBUG_SS
			fprintf(stdout, "fixPrevBlock across 2 volID\n");
#endif
			__u32 lastblk;
		    if (getLastvBlk(*preVolIDp, &lastblk))
		        RET_ERR("volume ID %u has no info\n", *preVolIDp);
			assert(*prevBoundaryBlkNump <= lastblk);

			/* For the block(s) *prevBoundaryBlkNump to lastblk, find v2c
             * and add chunkID to their chunkIDUList
             */
			for (blk=*prevBoundaryBlkNump; blk<=lastblk; blk++)
				addChunkIDtoPrev2c(chunkID, *preVolIDp, blk+1);

			/* For the block(s) 0 to blockID-1, find v2c
             * and add chunkID to their chunkIDUList
             */
			for (blk=0; blk<=currblockID-1; blk++)
                addChunkIDtoPrev2c(chunkID, volID, blk+1);
		}
		else											/* within same volID */
		{
#ifdef SIMREPLAY_DEBUG_SS_DONE
			fprintf(stdout, "fixPrevBlock within same volID\n");
#endif
        	/* For the block(s) *prevBoundaryBlkNump to blockID-1, find v2c
			 * and add chunkID to their chunkIDUList
			 */
			assert(volID == *preVolIDp);
        	for (blk=*prevBoundaryBlkNump; blk<=currblockID-1; blk++)
    	        addChunkIDtoPrev2c(chunkID, volID, blk+1);
		}
    }

    return 0;
}
#endif

#ifndef PROMAPPING_TEST
#ifndef PRODUMPING_TEST

/* subChunkIDUList: substitute the old chunkIDUList with new one.
 *
 * @volID[in]: the volume this block belongs to
 * @blockID[in]: blockID of vblk whose v2c is being manipulated
 * @v2c[in|out]: v2c of vblk being manipuated, output to caller
 * @chunkIDUList[in|out]: new chunkID list
 */
int subChunkIDUList(__u16 volID, __u32 blockID, v2c_datum **v2c, 
				Node ** chunkIDUListp)
{
#ifdef DEBUG_SS
	assert(*v2c != NULL);
#endif

	/* check if NULL chunkIDUList (possible if this block is being written
	 * and therefore mappingTrimScan() has been done earlier. We need to
	 * perform the substitution, the old one needs to be replaced only if
	 * it exists in the first place.
	 */
	if ((*v2c)->chunkIDUList != NULL)
	{
		/* Yes, old one exists */
		RET_ERR("shouldnt the old one have been reset earlier?\n");
		if (resetMappings(*v2c, volID, blockID))
			RET_ERR("resetMappings error'ed\n");
	//?? free(v2c); /* Free memory, was malloc'ed in note_v2c_map() */

	}

	(*v2c)->chunkIDUList = appendUList((*v2c)->chunkIDUList, chunkIDUListp);
	assert(*chunkIDUListp == NULL);

	return 0;
}

/* replaceChunkID: Give a v2c and its chunkIDUList, replace it with
 * 				a new one (chunkIDUListp), discard the first few (till pos)
 * 				chunkIDs from old list, and copy remaining from old list
 * 				into new list.
 * @volID[in]: volume ID
 * @blockID[in]: vblk whose v2c is being manipulated
 * @pos[in]: position into old chunklist till which chunkIDs to be discarded
 * @v2c[in|out]: v2c of vblk volID
 * @chunkIDUList[in|out]: new chunkID list
 */
int replaceChunkID(__u16 volID, __u32 blockID, int pos, v2c_datum **v2c,
				Node ** chunkIDUListp)
{
	int chunkidx = 0;
	Node * tempchunkIDUList, *currchunkIDUList = NULL;
	chunk_id_t *chunkIDp = NULL;
	int numchunks = ulistLen(*chunkIDUListp);
#ifdef DEBUG_SS
	assert(*v2c != NULL);
#endif

	/* Store previous chunkIDUList in temp */
	tempchunkIDUList = (*v2c)->chunkIDUList;

	/* Copy all chunkIDs from chunkIDUListp to currchunkIDUList */
	for (chunkidx = 0; chunkidx < numchunks; chunkidx++)
	{
		chunkIDp = (chunk_id_t*)popUList(chunkIDUListp);
		//dont free chunkIDp here, since it is needed in addtoUList below!
		currchunkIDUList = addtoUList(currchunkIDUList, chunkIDp, 
						sizeof(chunk_id_t));
		free(chunkIDp);
	}
#if defined(DEBUG_SS) || (SIMREPLAY_DEBUG_SS)
	assert(*chunkIDUListp == NULL);
#endif

	/* recycle chunkIDs from 1 to "pos" from tempchunkIDUList and
	 * copy the remaining to currchunkIDUList 
	 */
	numchunks = ulistLen(tempchunkIDUList);
	for (chunkidx = 0; chunkidx < numchunks; chunkidx++)
	{
		chunkIDp = (chunk_id_t*)popUList(&tempchunkIDUList);
		if (chunkidx < pos)
		{
			/* Since we are gonna recycle, we can free chunkIDp here */
			chunk_id_t val = *chunkIDp;
			free(chunkIDp);
			if (recyclechunkID(val, 1, volID, blockID))
				RET_ERR("recyclechunkID error'ed\n");
		}
		else
		{
			currchunkIDUList = addtoUList(currchunkIDUList, chunkIDp,
                        sizeof(chunk_id_t));
			free(chunkIDp);
		}
	}
	assert((*v2c)->chunkIDUList == NULL);

	(*v2c)->chunkIDUList = currchunkIDUList;

	return 0;
}

/** Updating a block boundary, by modifying the v2c map for given vblk
 * Assumes that all relevant chunkIDs are already added to new chunkIDUList
 * For Coinciding boundaries => chunkID was added within loop, whereas
 * For Non-coinciding boundaries => chunkID added to list just before this call
 * Assuming that write never writes a zero block. If this is not correct, 
 * fix the zero block handling here. //FIXME
 *
 * @param buf
 * @param blockID
 * @param lastChunkID
 * @param len_tillnow
 * @param lastblk_flag
 * @param[out] coinciding_stat
 * @return status
 */
int updateBlock(unsigned char *buf, __u32 blockID, __u16 volID,
                int len_tillnow, int lastblk_flag, int coincide_flag, 
				int *coinciding_stat, Node **chunkIDUListp)
{
    v2c_datum *v2c;
    int rc = 0;
    //static chunk_size_t iolastoffsetminus1 = dec_chunkoffset(0);
    static chunk_size_t iolastoffsetminus1 = MAXCHUNKSIZE - 1;

    //savemem unsigned char key[HASHLEN + MAGIC_SIZE];
	unsigned char *key = malloc(HASHLEN + MAGIC_SIZE);

    v2c = (v2c_datum*)v2cmaps_get(v2cmaps, volID, blockID);
    if (v2c == NULL && !runtimemap)
	{
        RET_ERR("v2cmaps_get(%u, %u) fail in updateBlock()\n", 
				volID, blockID);
	}
	else if (v2c == NULL)
	{
		/* In case of runtime PROVIDE, need to create new mapping */
		v2c = (v2c_datum*)calloc(1, sizeof(v2c_datum));
	}
	else
	{
		if (v2c->chunkIDUList)
			if (resetMappings(v2c, volID, blockID))
				RET_ERR("resetMappings failed\n");
	}

#ifndef NONSPANNING_PROVIDE
    if (lastblk_flag == POSTCHUNK_FULL_LASTBLK)
    {
        /* We reach here for a block boundary s.t. that block is the last
         * block in postchunk and it is a "true" block boundary, not "partial".
         * There can be 3 cases here:-
         * I) NOCOINCIDEFIRST ==> only block boundary encountered, not chunk.
         *      So, new iochunkIDUList to be substituted for old.
         * II) NOCOINCIDESECOND ==> only block boundary encountered, not chunk.
         *      new iochunkIDUList[] has a single chunkID that needs to replace
         *      the first chunkID in existing chunkIDUList.
         * III) COINCIDE => both chunk and block boundary encountered.
         *      So, new iochunkIDUList to be substituted for old.
         * And *coinciding_stat = WASREADY coz no more chunkIDs to add later.
         */
        switch (coincide_flag)
        {
            case NOCOINCIDEFIRST: 
				rc = subChunkIDUList(volID, blockID, &v2c, chunkIDUListp);
                break;
            case NOCOINCIDESECOND: 
				rc = replaceChunkID(volID, blockID, 1, &v2c, chunkIDUListp);
                break;
            case COINCIDE:  
				rc = subChunkIDUList(volID, blockID, &v2c, chunkIDUListp);
                break;
        }
        *coinciding_stat = WASREADY;
    }
    else if (lastblk_flag == NOPOSTCHUNK_LASTBLK)
    {
        /* We reached here if the write buffer had no postchunk, so
         * the last chunk piece has become a chunk by itself. This 
         * could be either COINCIDE (chunk boundary also found) or 
         * FORCECOINCIDE (chunk boundary not found but next block zero).
         */
        rc = subChunkIDUList(volID, blockID, &v2c, chunkIDUListp);
        *coinciding_stat = WASREADY;
    }
    else if (lastblk_flag == POSTCHUNK_PARTIAL_LASTBLK)
    {
        /* If we reached here, we encountered a "partial" block boundary.
         * Thus, previously this block's first chunk had been considered
         * as postchunk for a block write. 
         */
        switch(coincide_flag)
        {
            case NOCOINCIDE:
                /* Zero or more chunks have been encountered since this
                 * block began and those chunkIDs are present in iochunkIDUList.
                 * However, a previous chunk boundary seems to have now been
                 * overwritten s.t. the boundary has vanished (NOCOINCIDE),
                 * thus the first 2 chunkIDs of old list will be replaced with
                 * all chunkIDs from new iochunkIDUList. Note that the "last"
                 * chunk has already been added to the end of new iochunkIDUList
                 * by the caller, resumeChunking().
                 */
                rc = replaceChunkID(volID, blockID, 2, &v2c, chunkIDUListp);
                break;
            case COINCIDE:
                /* Zero or more chunks have been encountered since this
                 * block began and those chunkIDs are present in iochunkIDUList.
                 * Seems that a previous chunk boundary has not vanished
                 * or has at least been retained at same position (COINCIDE),
                 * thus only the first chunkID of old list will be replaced with
                 * all chunkIDs from new iochunkIDUList. Note that the "last"
                 * chunk has already been added to the end of new iochunkIDUList
                 * by the caller, resumeChunking().
                 */
                rc = replaceChunkID(volID, blockID, 1, &v2c, chunkIDUListp);
                break;
        }
        *coinciding_stat = WASREADY;
    }
    else
    {
        assert(lastblk_flag == NOT_LASTBLK);
        rc = subChunkIDUList(volID, blockID, &v2c, chunkIDUListp);
    }
#else
	if (lastblk_flag == ONLYBLK)
	{
		rc = subChunkIDUList(volID, blockID, &v2c, chunkIDUListp);
	}
	else
	{
		RET_ERR("%s: Unknown lastblk_flag value %d\n", __FUNCTION__, 
				lastblk_flag);
	}
#endif
    if (rc)
        RET_ERR("error here\n");

    /* Hash(+magic) the block */
    if (getHashKey(buf, BLKSIZE, key))
        RET_ERR("getHashKey() returned error\n");

    //TODO: look into interpretation of len_tillnow because whether vblk
    //          is "partial" vs "full", has different implications
    rc = note_v2c_map(v2c, &iolastoffsetminus1, //key, //blockID, volID,
        chunkIDUListp, len_tillnow, coincide_flag, lastblk_flag);
    if (rc)
        RET_ERR("Error in note_v2c_map()\n");

    /* Updating the v2cmaps data structure */
    v2cmaps_set(v2cmaps, volID, blockID, (void*)v2c);

#ifndef NONSPANNING_PROVIDE
    /* Note static elements for next time */
    if (coincide_flag == COINCIDE ||
        coincide_flag == FORCECOINCIDE ||
        lastblk_flag != NOT_LASTBLK)    /* if lastblk, then reset elements */
    {
        iolastoffsetminus1 = dec_chunkoffset(0);
        *coinciding_stat = WASREADY;
    }
    else
    {
        assert(len_tillnow != 0);
        iolastoffsetminus1 = len_tillnow - 1;
        *coinciding_stat = WASNOTREADY;
    }
#else
	UNUSED(coinciding_stat);
#endif
    if (coincide_flag != FORCECOINCIDE)
    	assert(*chunkIDUListp == NULL);
	free(key);	//savemem
    return 0;
}
#endif	//ifndef PRODUMPING_TEST
#endif	//ifndef PROMAPPING_TEST

/** processBlock: Processing a block boundary, by creating the v2c map 
 * 					for given vblk.
 *
 * @param buf[in]: Data of the vblk
 * @param blockID[in]:	block ID of the vblk
 * @param lastChunkID[in]: chunkID of the last chunk of this vblk
 * @param len_tillnow[in]: leftover chunk piece when vblk boundary encountered
 * @param coincide_flag[in]: whether chunk/vblk boundary coinciding
 * @param endcoincide_stat[out]: outputs whether the vblk boundary WASREADY
 * 						or WASNOTREADY
 * @return status
 */
int processBlock(unsigned char *buf, __u32 blockID, __u16 volID,
                int len_tillnow, int coincide_flag, 
				int *endcoincide_stat, int lastblk_flag)
{
    v2c_datum *v2c;
    static chunk_size_t lastoffsetminus1 = MAXCHUNKSIZE - 1;
#if 0
    unsigned char key[HASHLEN + MAGIC_SIZE];
	if (lastblk_flag != ZEROBLK_FLAG)
	{
	    /* Hash(+magic) the block */
    	if (getHashKey(buf, BLKSIZE, key))
        	RET_ERR("getHashKey() returned error\n");
	}
#else
	UNUSED(buf);
#endif

    /* Malloc'ed space, to be freed later in resetMappings() */
    v2c = calloc(1, sizeof(v2c_datum));

		/* If coincide_flag == FORCECOINCIDE, it indicates that
         * This block is zero block, and the "force coincide" has
         * happened for the end of previous block and chunk. However,
         * this is known only when this zero block was encountered,
         * hence needs to be handled here.
         * So, need to fix up chunkID into v2c map of blockID-1
         * and also mark blockID as zero block => fixPrevBlock() in caller.
         * Note that blockID-1 would have already reached here in the
         * previous iteration with flag NOCOINCIDE and we would have
         * already handled it accordingly, i.e. noted its hashkey & blockID.
         * So, we need not do note_block_attrs() again here for blockID-1.
         * Even the v2c map for blockID-1 has already been noted in
         * previous iteration. The only difference between normal
         * COINCIDE chunk boundary and FORCECOINCIDE chunk boundary
         * is that the chunk has end_offset_into_block as offset 
         * into blockID-1 rather than blockID => processChunk() in caller.
         */

#ifdef PROMAPPING_TEST
    fprintf(fhashptr, "blkbound=%u start_offset_into_chunk=%u end_offset_into_chunk=%u\n", blockID, inc_chunkoffset(lastoffsetminus1), len_tillnow - 1);
#endif

	/* Note the elements in the V2C_tuple_t struct */
    if (note_v2c_map(v2c, &lastoffsetminus1, //key, //blockID, volID,
        &chunkIDUList, 
		len_tillnow, coincide_flag, lastblk_flag))
        RET_ERR("Error in note_v2c_map()\n");

	/* Link the chunkIDUList to the v2c map */
    if (linkChunkIDUList(v2c))
        RET_ERR("Error in linkChunkIDUList\n");

	/* Add the v2c to the global v2cmaps */
    v2cmaps_set(v2cmaps, volID, blockID, (void*)v2c);

#if defined(PROMAPPING_TEST) || defined(PDD_REPLAY_DEBUG_SS)
	if (blockID==847342)
	{
		int k, num_chunkid;
	    fprintf(stdout,"(%u, ", volID);
	    fprintf(stdout,"%u) ", blockID);
	    fprintf(stdout,"startoff=%u ", v2c->start_offset_into_chunk);
		if (v2c->chunkIDUList != NULL)
		{
		    num_chunkid = ulistLen(v2c->chunkIDUList);
	    	fprintf(stdout, "num_chunkid=%u ", num_chunkid);  /* f */
			if (num_chunkid > 0)
			{
				fprintf(stdout, "chunklist=[");
		    	for (k = 0; k < num_chunkid; k++)
			    {   
		    		chunk_id_t ID;
		        	ID = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, k);
			        fprintf(stdout, " %u", ID);   /* g */
			    }
				fprintf(stdout, " ] ");
				}
		}
		else
		{
			fprintf(stdout, "num_chunkid=0 chunklist=[NULL]\n");
		}
	    fprintf(stdout,"endoff=%u ", v2c->end_offset_into_chunk);
		fprintf(stdout, "\n");
	}
#endif

    /* Note static elements for next time */
    if (coincide_flag == COINCIDE || coincide_flag == FORCECOINCIDE || lastblk_flag == ULTIMATE_LASTBLK)
    {
        lastoffsetminus1 = dec_chunkoffset(0);
    }
    else
    {
#ifdef DEBUG_SS
        assert(len_tillnow != 0);
#endif
        lastoffsetminus1 = len_tillnow - 1;
    }

    /* Setting up endcoincide_stat to be used in fixPrevBlock @ AFTERBOUND */
    if (coincide_flag == FORCECOINCIDE || coincide_flag == COINCIDE || lastblk_flag == ULTIMATE_LASTBLK)
        *endcoincide_stat = WASREADY;
    else if (coincide_flag == NOCOINCIDE)
        *endcoincide_stat = WASNOTREADY;

    assert(chunkIDUList == NULL);
    return 0;
}


