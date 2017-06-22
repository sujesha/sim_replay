#include <asm/types.h>
#include <string.h>
#include <assert.h>
#include "chunking.h"
#include "c2pv-map.h"
#include "debug.h"
#include "defs.h"
#include "rabin.h"
//#include "rabin-prototypes.h"
//#include "ulist.h"				/* Node */
#include "utils.h"
#include "v2c-map.h"
#include "unused.h"
#ifdef PROCHUNKING_TEST
	#include "prochunking_test_stub.h"
#endif
#include "replay-defines.h"
#include "serveio.h"

#if defined (PROMAPPING_TEST) || defined(PDD_REPLAY_DEBUG_SS)
	extern FILE * fhashptr;
#endif

Node * chunkIDUList = NULL;
Node * iochunkIDUList = NULL;

#if 0
extern Node * chunkIDUList;
extern Node * iochunkIDUList;
#endif

/* Extern (Globals) to be used during write chunking, in processChunk() */
extern __u16 iolastBlkOffsetminus1;		/* can not be -1 */
extern __u32 ioprevBoundaryBlkNum;
extern unsigned char vmaphit_flag;

/* Return 0 for success */
/* createInitialChunkBuf: Appending "buf" to "leftover" chunk 
 * 		and return the whole content as "chunk" 
 * 	
 * @chunk[out]: output chunk 
 * @buf[in]: data buffer to be appended
 * @len[in]: length of data buffer
 * @leftover[in]: chunk to which above data buffer is to be appended
 * @bytes_left[out]: notes length of data in "leftover" chunk before it's used 
 */
int createInitialChunkBuf(struct chunk_t **chunk, unsigned char *buf, 
		__u16 len, struct chunk_t **leftover, __u16 *bytes_left)
{
    __u16 bytes_read=0; //, offset;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#ifdef DEBUG_SS
	assert(*chunk == NULL);
#endif

    /* Check how much data left over, create an initial chunk */
    if (*leftover != NULL)
    {
        *bytes_left = csize(*leftover);
#ifdef PROCHUNKING_DEBUG_SSS
        WHERE;
#endif
    }
    else
    {
        *bytes_left = 0;
#ifdef PROCHUNKING_DEBUG_SSS
        WHERE;
#endif
    }

    bytes_read = len;

    /* No data left over from last iteration and also nothing new read in */
    if(*bytes_left + bytes_read == 0)
    {
        /* This could happen when no previous data, and zero block was read 
         * This is not an error condition   */
#ifdef PROCHUNKING_DEBUG_SSS
        WHERE;
#endif
        return 0;
    }

    /* Check if input buffer size exceeds system maximum */
    if((len + *bytes_left) > SSIZE_MAX_128MB)
		RET_ERR("requested buffer size exceeds system maximum\n");

    /* Malloc chunk and its mbuffer */
    *chunk = alloc_chunk_t(len + *bytes_left);
    if (*chunk == NULL)
		RET_ERR("alloc_chunk_t failed\n");
    
    /* Create data buffer for chunking */
    mergeLeftovers(chunk, leftover, buf, len, *bytes_left);
#ifdef DEBUG_SS
    assert(*leftover == NULL);
#endif

	return 0;
}

int updateDedupfetch(__u16 volID, __u32 blockID)
{
    int chunkidx = 0, numchunks;
    chunk_id_t val;
    int ret = 0;
    C2V_tuple_t* c2v;
    struct slist_head v2clist, *p;
    v2c_datum *v2c = NULL;
    chunkmap_t *c2pv = NULL;

#ifdef SIMREPLAY_DEBUG_SS_DONE
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    INIT_LIST_HEAD(&v2clist);
    ret = getVirttoChunkMap(volID, blockID, 1, &v2clist);
    if (ret)
    {
        RET_ERR("error getVirttoChunkMap (%u, %u) in spite of vmaphit_flag=1\n",
				volID, blockID);
    }
    if (slist_len(&v2clist) != 1)
    {
        RET_ERR("Number of mappings fetched %d is not equal to number"
                     " requested %u\n", slist_len(&v2clist), 1);
    }

    p = slist_first(&v2clist);
    v2c = slist_entry(p, v2c_datum, head);
    if (cvblk_dirty(v2c))
    {
        RET_ERR("metadata should not be dirty if vmaphit_flag=1\n");
    }

    numchunks = ulistLen(v2c->chunkIDUList);
    for(chunkidx=0; chunkidx < numchunks; chunkidx++)
    {
        val = *(chunk_id_t*)getIndexedNode(v2c->chunkIDUList, chunkidx);
		if (val == 0)
		{
			/* A zero chunk need not be updated for dedupfetch. So just
			 * skip it.
			 */
			continue;
		}
        c2pv = getChunkMap(val);
		assert(c2pv != NULL);

        c2v = get_nondeduped_c2v(c2pv, volID, blockID);
        if (c2v == NULL)
        {
            RET_ERR("volID %u, blockID %u not found\n", volID, blockID);
        }
        if (c2v->dedupfetch == 0)
        {
            unmark_old_dedupfetch(c2pv);
            c2v->dedupfetch = 1;
        }
    }
    return 0;
}

#if defined(NONSPANNING_PROVIDE) || defined(SPANIMMEDIATE_PROVIDE)
int resumeDynChunking(unsigned char *buf, __u16 len, __u16 volID, 
		__u32 blockID, int lastblk_flag, int rw_flag)
{
	struct chunk_t *chunk;
	int ret;

    /* Malloc chunk and its mbuffer */
    chunk = alloc_chunk_t(len);
    if (chunk == NULL)
        RET_ERR("alloc_chunk_t failed\n");
	memcpy(cdata(chunk), buf, len);

	lastblk_flag = ONLYBLK;

    /* Not checking for zero block here, because it's not a possibility
     * unless an existing block can be over-written with zero block. If so,
     * then fix here to handle it.
     */
    ret = resumeChunking(cdata(chunk), len, volID, blockID, NOINIT_STAGE,
                    lastblk_flag, NULL, rw_flag);
    if (ret)
    {
        RET_ERR("resumeChunking err for %u resumeDynChunking\n", blockID);
    }
	free_chunk_t(&chunk);
	return 0;
}
#else
int resumeDynChunking(unsigned char *buf, __u16 len, __u16 volID, 
		__u32 blockID, int lastblk_flag, int rw_flag)
{
	UNUSED(buf);
	UNUSED(len);
	UNUSED(volID);
	UNUSED(blockID);
	UNUSED(lastblk_flag);
	return 0;
}
#endif

/** Resume the chunking process that happens during the initial scanning
 * phase, by adding the now-received input buffer to any leftover buffer
 * from before. Thus, every block is chunked and static variables are
 * readied to be used when next block is received for "resumption" of
 * the chunking process.
 * V2C tuple consists of chunkIDUList[] to indicate which chunks does
 * this vblk map into, alongwith start and end offsets.
 *
 * Creation of chunkIDUList[] when a block pointing to 1 chunk only :-
 * 1. Block wholly within 1 chunk
 * 2. Block starts somewhere within but end coincides with chunk
 * 3. Block start coincides with chunk and ends somewhere within chunk
 * 4. Blocks start and end coincide with chunk
 * In case 1, when previous block boundary was encountered, the chunk
 * 		was still "under construction", so the processBlock() or updateBlock()
 * 		had noted only the start_ and end_offset_into_chunk and left the
 *		chunkIDUList[] empty for the time being. The *coincidingp was set
 * 		as WASNOTREADY so that whenever next a chunk boundary is encountered,
 *		that (new or dedup) chunkID may be added to previous block's v2c
 * 		by invoking fixPrevBlock(). Since the end boundary of this block
 *		is also not coinciding, hence again *coincidingp set to WASNOTREADY
 *		for next time, and so on.
 * In case 2, when previous block boundary was encountered, the chunk
 *      was still "under construction", so the processBlock() or updateBlock()
 *      had noted only the start_ and end_offset_into_chunk and left the
 *      chunkIDUList[] empty for the time being. The *coincidingp was set
 *      as WASNOTREADY so that whenever next a chunk boundary is encountered,
 *      that (new or dedup) chunkID may be added to previous block's v2c
 *      by invoking fixPrevBlock(). Since the end boundary of this block
 *		is coinciding, so *coincidingp set to WASREADY, to indicate that
 *		this block's chunkIDUList is complete.
 * In case 3, either this is the very first block & chunk, or prev block 
 *      & chunk had coinciding end boundaries. In both cases, no chunk
 *      end boundary has yet been encountered since this block began.
 *		So, BEFOREBOUND the value of *coincidingp should be WASREADY
 *		and AFTERBOUND, it should be WASNOTREADY.
 *  In case 4, either this is the very first block & chunk, or prev block 
 *      & chunk had coinciding end boundaries. Also, current chunk and 
 *      block boundaries also coincide. Here, @ both BEFOREBOUND and
 *		AFTERBOUND, *coincidingp should be WASREADY.
 *
 * Creation of chunkIDUList[] when a block pointing to more than 1 chunk :-
 * A. Block start coincides with chunk and ends arbitrarily in some chunk
 * B. Block starts and ends arbitrarily
 * C. Block starts arbitrarily and end coincides with chunk
 * In case A, either this is the very first block & chunk, or prev block 
 *      & chunk had coinciding end boundaries. In both cases, no chunk
 *      end boundary has yet been encountered since this block began.
 *      So, BEFOREBOUND the value of *coincidingp should be WASREADY
 *      and AFTERBOUND, it should be WASNOTREADY. The WASNOTREADY
 *		will ensure that fixPrevBlock() is invoked to add the (new or 
 *		dedup) chunkID, whenever next it is encountered, to the chunkIDUList[]
 *		of the block.
 * In case B, when previous block boundary was encountered, 
 *		*coincidingp == WASNOTREADY and when this block boundary 
 *		encountered, again set the same WASNOTREADY. The WASNOTREADY
 *      will ensure that fixPrevBlock() is invoked to add the (new or 
 *      dedup) chunkID, whenever next it is encountered, to the chunkIDUList[]
 *      of the block.
 * In case C, when previous block boundary was encountered, 
 *      *coincidingp == WASNOTREADY and when this block boundary 
 *      encountered, *coincidingp == WASREADY.
 *
 * @param[in] buf
 * @param[in] len
 * @param[in] volID
 * @param[in] blockID
 * @param[in] initflag
 * @param[in] lastblk_flag
 * @param[in,out] seqnextp
 */
int resumeChunking(unsigned char *buf, __u16 len, __u16 volID, 
		__u32 blockID, int initflag, int lastblk_flag, 
		struct chunkmap_t **seqnextp, int rw_flag)
{
	/* Since this function could be called by scanning phase 
	 * (initflag==1) and by online phase (initflag==0), so we have
	 * 2 copies of each static variable.
	 */
    static struct chunk_t *leftover=NULL; /* static, to remember in next call */
    static struct chunk_t *chunk=NULL;   /* static, to remember in next call */
    static struct chunk_t *ioleftover=NULL;/* static, to remember in next call*/
    static struct chunk_t *iochunk=NULL;  /* static, to remember in next call */
#ifndef NONSPANNING_PROVIDE
	static int coinciding = 1;
	static int iocoinciding = 1;
#endif

	Node **chunkIDUListp = NULL;	/* no need for static, new for each write */

	/* These are pointers to the static variables above */
	struct chunk_t **ldp = NULL;			/* leftover double pointer */
	struct chunk_t **cdp = NULL;   		/* chunk double pointer */
#ifndef NONSPANNING_PROVIDE
	int foundchunk = 0;
	int *coincidingp;
#endif

    __u16 bytes_left=0; //left over from previous iteration
    __u16 foundclen;
    short split = 0;
    //uint32_t ptime;  /* To be noted when event occurs, is this needed? TODO
    chunk_id_t cNum = 0;		/* cant be -1 because unsigned */
	int ret;
#ifndef NONSPANNING_PROVIDE
	__u32 endblkID;		/* used in updateBegChunkMap */
#endif
	__u16 blklen = len;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (initflag == INIT_STAGE)
	{
		ldp = &leftover;
		cdp = &chunk;
		chunkIDUList = NULL;
		chunkIDUListp = &chunkIDUList;
#ifndef NONSPANNING_PROVIDE
		coincidingp = &coinciding;
#endif
	}
	else
	{ 
		ldp = &ioleftover;
		cdp = &iochunk;
		iochunkIDUList = NULL;
		chunkIDUListp = &iochunkIDUList;
#ifndef NONSPANNING_PROVIDE
		coincidingp = &iocoinciding;
#endif
	}

#ifdef DEBUG_SS
	/* Since this is resumption of chunking, the new "buf" will always
	 * have "len" <= BLKSIZE. Previously accumulated buf will be in leftover.
	 */
	assert(len <= BLKSIZE);
	assert((initflag == INIT_STAGE && (len == BLKSIZE || len == 0)) || 
					(initflag == NOINIT_STAGE));
#endif

#ifndef NONSPANNING_PROVIDE
	if(lastblk_flag==ZEROBLK_FLAG) /* Zero block scan */
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
#ifdef PDD_REPLAY_DEBUG_SS_DONE
		fprintf(stdout, "ZEROBLK_FLAG:");
#endif
		if (initflag == INIT_STAGE)
		{
			if (*ldp != NULL)	/* there was leftover, so make a chunk 
									and update previous blk mappings, if any */
			{
				/* chunk by itself because zero block was encountered at blockID
				 * so, the chunk is to become last chunk of blockID-1 
				 * Need to invoke fixPrevBlock() after processChunk()
				 */ 
				cNum = processChunk(*ldp, 0, blockID-1, volID, INIT_STAGE,
								blklen, CHUNK_BY_ZEROBLK, rw_flag);
				foundchunk = 0;	/* above chunk is for previous block, not current */

				 /* Update previous block(s) chunkIDUList with this cNum if needed*/
				if (*coincidingp == WASNOTREADY)
				{
#ifdef PDD_REPLAY_DEBUG_SS_DONE
	        	    fprintf(stdout, "Updating prevblks due to zero currblk\n");
#endif
					//if (fixPrevBlock(initflag, cNum, blockID-1,volID,WASNOTREADY, 
					if (fixPrevBlock(initflag, cNum, blockID, volID, WASNOTREADY, 
								foundchunk, BEFOREBOUND))
						RET_ERR("Error in fixPrevBlock()\n");
				}
        		*ldp = free_chunk_t(ldp); /* Free chunk and its mbuffer */
			}

			/* Whether there was leftover or no, this is a zeroblk mapping */
	        ret = processBlock(buf, blockID, volID, 0, 
				FORCECOINCIDE, coincidingp, lastblk_flag);
			if (ret)
				RET_ERR("processBlock() error'ed\n");
		}
#if 0
		else
		{
			/* This is for write chunking, incomplete....  */
		}
#endif
        cNum = processChunk(NULL, 0, blockID, volID, initflag, 
						blklen, JUST_UPDATE_NEXT_CHUNKOFFSETS, rw_flag);
		/* Above value of CNum is just a dummy, not an actual chunkID, dont use */
		goto finishup;
	}
#endif

	/* "Leftover" ldp point to a static variable here because it's created for
	 * the first time from within this environment and subsequently
	 * remembered till next call. 
	 */
	ret = createInitialChunkBuf(cdp, buf, len, ldp, &bytes_left);
	if (ret != 0)
	{
		RET_ERR("createInitialChunkBuf err in resumeChunking"
					" with initflag = %d\n", initflag);
	}
#ifdef DEBUG_SS
	assert(*ldp == NULL);
#endif

	/* Though *ldp = NULL here due to createInitialChunkBuf(), the parameter
	 * bytes_left is supposed to contain the length of the leftover buffer
	 * from last invocation, to be given as input to invokeChunking()
	 */
#ifdef NONSPANNING_PROVIDE
	assert(bytes_left == 0 && len == BLKSIZE);
#endif

	/* This while loop is for chunk boundaries found within a single
	 * block's content. When the block content's chunking finishes, 
	 * and there are no more chunks, split == 0 and do-while() ends.
	 */
    do
    {
        /* Perform chunking appropriate to chunking buffer - scan or I/O 
		 * split==1 indicates that chunk boundary is non-coinciding to blk bndry
		 * split==0 indicates either a coinciding boundary or no boundary found
		 */
        split = invokeChunking(*cdp, &len, &bytes_left, &foundclen, initflag);
		if (split == 0)
			break;
#ifdef PROCHUNKING_DEBUG_SSS
            WHERE;
#endif

		/* Else, prepare for next iteration of invokeChunking() */
	    splitChunkBuffer(cdp, ldp, foundclen);
        assert(foundclen == csize(*cdp));
#ifdef PROCHUNKING_DEBUG_SSS
        assert((*ldp) != NULL);
            WHERE;
        assert((*cdp) != NULL);
#endif

        cNum = processChunk((*cdp), csize(*ldp), blockID, volID, initflag, 
						blklen, lastblk_flag, rw_flag);
#ifndef NONSPANNING_PROVIDE
		foundchunk = 1;

		/* If chunk boundary encountered s.t. its starting boundary
		 * was non-coinciding, then previous block(s) need their
		 * chunkIDUList to be appended with this cNum.
		 */
		//FIXME: for write chunking, the initial values in fixPrevBlock()
		//			need some thought. 
		if (isEmptyUList(*chunkIDUListp) && *coincidingp == WASNOTREADY)
		{
			if (fixPrevBlock(initflag, cNum, blockID, volID, WASNOTREADY, 
							foundchunk, BEFOREBOUND))
				RET_ERR("Error in fixPrevBlock()\n");
		}
#endif

        /* For every new block, this will start anew */
    	*chunkIDUListp = addtoUList(*chunkIDUListp, &cNum, sizeof(chunk_id_t)); 

        *cdp = free_chunk_t(cdp); /* Free chunk and its mbuffer */
        *cdp = *ldp;   /* Point chunk to leftover data */
        *ldp = NULL;
    }while (split==1);

#ifdef DEBUG_SS
    assert(foundclen >= csize(*cdp));
	if (len == 0)			/* Zero block encountered at blockID */
		assert(foundclen == bytes_left);
#endif
		
    if (foundclen > csize(*cdp)) 	/* No chunk, but block boundary reached */
    {
		//fprintf(stdout, "No chunk, but block boundary reached:");
		//fprintf(stdout, "BB (%u, %u), foundclen=%u > chunklen=%u:", 
		//				volID, blockID, foundclen, csize(*(cdp)));
#ifdef PROCHUNKING_DEBUG_SSS
        WHERE;
#endif

#ifndef NONSPANNING_PROVIDE
#ifndef PROMAPPING_TEST
#ifndef PRODUMPING_TEST
		if (lastblk_flag == NOPOSTCHUNK_LASTBLK)
		{
			fprintf(stdout, "NOPOSTCHUNK_LASTBLK\n");
			/* If we reached here, we have encountered block boundary
			 * but not a chunk boundary (during write chunking), also  
			 * NOPOSTCHUNK_LASTBLK => has to become chunk by itself
			 * and that new chunkID needs to be added to chunkIDUList
			 * of this vblk.
			 */
			cNum = processChunk((*cdp), 0, blockID, volID, NOINIT_STAGE,
								blklen, lastblk_flag, rw_flag);
			foundchunk = 1;
			/* If chunk boundary encountered s.t. its starting boundary
			 * was non-coinciding, then previous block(s) need their
			 * chunkIDUList to be appended with this cNum.
			 */
			if (isEmptyUList(*chunkIDUListp) && *coincidingp == WASNOTREADY)
			{
				if (fixPrevBlock(initflag, cNum, blockID, volID, WASNOTREADY,
							foundchunk, BEFOREBOUND))
					RET_ERR("Error in fixPrevBlock()\n");
			}
    		*chunkIDUListp = addtoUList(*chunkIDUListp, &cNum, 
					sizeof(chunk_id_t)); 
	        ret = updateBlock(buf, blockID, volID, csize(*cdp), 
					lastblk_flag, COINCIDE, coincidingp, chunkIDUListp);
			if (ret)
				RET_ERR("updateBlock() err'ed\n");
			
		}
		else if (lastblk_flag == POSTCHUNK_FULL_LASTBLK)
		{
#ifdef SIMREPLAY_DEBUG_SS
			fprintf(stdout, "POSTCHUNK_FULL_LASTBLK\n");
#endif
			/* If we reached here, we have encountered block boundary
			 * but not a chunk boundary (during write chunking), also 
			 * POSTCHUNK_FULL_LASTBLK => this is last block of chunking
			 * buffer which had a postchunk.
			 * Thus, previously this was a (post)chunk boundary, 
			 * but it has now vanished due to the block write.
			 * Thus, this chunk piece should be prepended to the next 
			 * block's first chunk's beginning, i.e. seqnextp.
			 * If seqnextp == NULL, can not prepend anything to it. In that
			 * case, call FORCECOINCIDE for blockID
			 * 		(i) add chunkID to new chunkIDUList
			 * 		(ii) and do updateBlock() for blockID
			 * Else, need to update 2 vblks -- postlast & next-after-post
			 * so call NOCOINCIDE case 
			 *		(i) updateBlock() for blockID => NOCOINCIDEFIRST
			 *		(ii) updateBegChunkMap() retrieves chunkID
			 *		(iii) addChunkIDtoPrev2c() for blockID+1
			 * 		(iv) Create new chunkIDUList with only this chunkID in it
			 *		(v) do updateBlock() for blockID+1 => NOCOINCIDESECOND
			 */
			if (seqnextp == NULL)
			{
				/* Can not prepend anything to a NULL chunk (or zero vblk) */
				cNum = processChunk(*cdp, 0, blockID, volID, NOINIT_STAGE,
											blklen, lastblk_flag, rw_flag);
				foundchunk = 1;
				/* If chunk boundary encountered s.t. its starting boundary
				 * was non-coinciding, then previous block(s) need their
				 * chunkIDUList to be appended with this cNum.
				 */
				if (isEmptyUList(*chunkIDUListp) && *coincidingp == WASNOTREADY)
				{
					if (fixPrevBlock(NOINIT_STAGE, cNum, blockID, volID, 
								WASNOTREADY, foundchunk, BEFOREBOUND))
						RET_ERR("Error in fixPrevBlock()\n");
				}
				*chunkIDUListp = addtoUList(*chunkIDUListp, &cNum, 
								sizeof(chunk_id_t));
				ret = updateBlock(buf, blockID, volID, csize(*cdp),
                				POSTCHUNK_FULL_LASTBLK, COINCIDE, 
								coincidingp, chunkIDUListp);
	            if (ret)
    	            RET_ERR("updateBlock() error'ed\n");
			}
			else
			{
				chunk_id_t retrieveID;
	        	ret = updateBlock(buf, blockID, volID, csize(*cdp), 
					lastblk_flag, NOCOINCIDEFIRST, coincidingp, chunkIDUListp);
				if (ret)
					RET_ERR("updateBlock() error'ed\n");
#ifdef DEBUG_SS
				assert(*chunkIDUListp == NULL);
#endif
				updateBegChunkMap(seqnextp, cdp, lastblk_flag, blklen,
					&retrieveID, &endblkID, volID, blockID);
				addChunkIDtoPrev2c(retrieveID, volID, endblkID+1);
    			*chunkIDUListp = addtoUList(*chunkIDUListp, &retrieveID, 
								sizeof(chunk_id_t)); 
	        	ret = updateBlock(NULL, blockID+1, volID, csize(*cdp), 
					lastblk_flag, NOCOINCIDESECOND, coincidingp, chunkIDUListp);
				if (ret)
					RET_ERR("updateBlock() error'ed\n");
			}
		}
		else if (lastblk_flag == POSTCHUNK_PARTIAL_LASTBLK)
		{
			fprintf(stdout, "POSTCHUNK_PARTIAL_LASTBLK\n");
			chunk_id_t retrieveID;

			/* If we reached here, we have reached a "partial" block
			 * boundary, not a "true" block boundary. This happens when
			 * the write buffer has a postchunk which ends in the middle
			 * of some block, i.e post-chunk had a non-coinciding boundary.
			 * Here also, the previous chunk boundary appears to have 
			 * vanished due to the block write. So, this chunk piece 
			 * should be prepended to this block's next chunk's 
			 * beginning, i.e. seqnextp => updateBegChunkMap
			 * @retrieveID: Contains the chunk ID - maybe new or dedup
			 * Also, v2c of this block should change and include retrieveID
			 * in its chunkIDUList (as 2nd one) instead of previous chunkID.
			 */
			updateBegChunkMap(seqnextp, cdp, lastblk_flag, blklen,
				&retrieveID, &endblkID, volID, blockID);
    		*chunkIDUListp = addtoUList(*chunkIDUListp, &retrieveID, 
				sizeof(chunk_id_t)); 
	        ret = updateBlock(buf, blockID, volID, csize(*cdp), 
					lastblk_flag, NOCOINCIDE, coincidingp, chunkIDUListp);
			if (ret)
				RET_ERR("updateBlock() error'ed\n");
		}
		else if (lastblk_flag == NOT_LASTBLK)       /* online */
		{
			fprintf(stdout, "NOT_LASTBLK\n");
			ret = updateBlock(buf, blockID, volID, csize(*cdp),
				lastblk_flag, NOCOINCIDE, coincidingp, chunkIDUListp);
            if (ret)
   	            RET_ERR("updateBlock() error'ed\n");
		}
		else 
#else
		UNUSED(endblkID); 				
		UNUSED(seqnextp);
#endif	//ifndef PRODUMPING_TEST
#else		
		UNUSED(endblkID); 				
		UNUSED(seqnextp);
#endif	//ifndef PROMAPPING_TEST
		if (lastblk_flag == GOODBLK_FLAG || lastblk_flag == SCAN_FIRSTBLK ||
                 lastblk_flag == ULTIMATE_LASTBLK)     /* scanning */
		{
			if (lastblk_flag == ULTIMATE_LASTBLK) /* scanning */
			{
				fprintf(stdout, "ULTIMATE_LASTBLK\n");
	        	cNum = processChunk((*cdp), 0, blockID, volID, initflag,
                        blklen, lastblk_flag, rw_flag);
    	    	foundchunk = 1;

	        	/* If chunk boundary encountered s.t. its starting boundary
		         * was non-coinciding, then previous block(s) need their
		         * chunkIDUList to be appended with this cNum.
		         */
		        if (isEmptyUList(*chunkIDUListp) && *coincidingp == WASNOTREADY)
		        {
		            if (fixPrevBlock(initflag, cNum, blockID, volID, 
						WASNOTREADY, foundchunk, BEFOREBOUND))
		                RET_ERR("Error in fixPrevBlock()\n");
		        }

		        /* For every new block, this will start anew */
    		    *chunkIDUListp = addtoUList(*chunkIDUListp, &cNum, 
											sizeof(chunk_id_t));

				/* ULTIMATE_LASTBLK boundary in scanning phase */
		        ret = processBlock(buf, blockID, volID, csize(*cdp),
					COINCIDE, coincidingp, lastblk_flag);
				if (ret)
					RET_ERR("processBlock() error'ed\n");
			}
			else
			{

#ifdef PDD_REPLAY_DEBUG_SS_DONE
				fprintf(stdout, "GOODBLK\n");
#endif
				/* Ordinary block boundary in scanning phase */
		        ret = processBlock(buf, blockID, volID, csize(*cdp),
					NOCOINCIDE, coincidingp, lastblk_flag);
				if (ret)
					RET_ERR("processBlock() error'ed\n");
			}
		}		
		else
		{
			assert(lastblk_flag != ZEROBLK_FLAG);
			RET_ERR("%s: Unknown lastblk_flag value %d\n", __FUNCTION__, 
					lastblk_flag);
		}
	
   	    *ldp = *cdp;
        *cdp = NULL;

#else	/* NONSPANNING_PROVIDE */
		UNUSED(seqnextp);
		if (lastblk_flag == ONLYBLK)
		{
			cNum = processChunk(*cdp, 0, blockID, volID, NOINIT_STAGE,
					blklen, lastblk_flag, rw_flag);
			*chunkIDUListp = addtoUList(*chunkIDUListp, &cNum,
			    	sizeof(chunk_id_t));
			ret = updateBlock(buf, blockID, volID, csize(*cdp),
                ONLYBLK, COINCIDE, NULL, chunkIDUListp);
            if (ret)
            	RET_ERR("updateBlock() error'ed\n");
		}
        *cdp = free_chunk_t(cdp); /* Free chunk and its mbuffer */
        *ldp = NULL;

#endif	/* NONSPANNING_PROVIDE */
    }
    else	/* Both chunk boundary found and block data ending */
    {
		//fprintf(stdout, "Both chunk boundary found and block data ending:");
		//fprintf(stdout, "CoB (%u, %u) foundclen=%u <= chunklen=%u:", 
		//				volID, blockID, foundclen, csize(*(cdp)));
#ifdef PROCHUNKING_DEBUG_SSS
        WHERE;
#endif
			/* Chunk contains all data that is to become chunk by itself */
			/* It should become a chunk by itself if :-
			 * I. this is last block in the scanning phase => ULTIMATE_LASTBLK
			 * II. zero block as next block in online => NOPOSTCHUNK_LASTBLK
			 * 		(A zero block being encountered in scanning phase is 
			 * 		already handled above as lastblk_flag == ZEROBLK_FLAG. 
			 * III. the occurence of a regular coinciding boundary in
			 * 		cases POSTCHUNK_FULL_LASTBLK & NOT_LASTBLK
			 * IV. the occurence of a regular coinciding boundary in GOODBLK_FLAG
			 */

			/* Since both chunk and block boundaries were found, first perform
			 * chunk processing, and then do block processing, case-by-case
			 */
			assert(lastblk_flag != ZEROBLK_FLAG); /* Zero block from scan phase */
	        cNum = processChunk((*cdp), 0, blockID, volID, initflag,
                        blklen, lastblk_flag, rw_flag);
#ifndef NONSPANNING_PROVIDE
    	    foundchunk = 1;

        	/* If chunk boundary encountered s.t. its starting boundary
	         * was non-coinciding, then previous block(s) need their
	         * chunkIDUList to be appended with this cNum.
	         */
    	    if (isEmptyUList(*chunkIDUListp) && *coincidingp == WASNOTREADY)
        	{
	            if (fixPrevBlock(initflag, cNum, blockID, volID, WASNOTREADY,
                            foundchunk, BEFOREBOUND))
    	            RET_ERR("Error in fixPrevBlock()\n");
        	}
#endif

	        /* For every new block, this will start anew */
	        *chunkIDUListp = addtoUList(*chunkIDUListp, &cNum, sizeof(chunk_id_t));

#ifndef NONSPANNING_PROVIDE
#ifndef PROMAPPING_TEST		
#ifndef PRODUMPING_TEST		
		if (lastblk_flag == POSTCHUNK_PARTIAL_LASTBLK)
		{
			fprintf(stdout, "POSTCHUNK_PARTIAL_LASTBLK\n");
			/* This is not a "true" block boundary. It is a "partial"
			 * block boundary. So, this chunk can become a chunk by
			 * itself ending into this block. (A previous post-chunk 
			 * boundary may or may not have been preserved.)
			 * Since post-chunk would only have extended to the 1st
			 * chunk of this "partial" block, so, we need to processChunk()
			 * and then make that (new or dedup) chunkID as the 1st
			 * one in chunkIDUList while the rest of the IDs from old
			 * chunkIDIUList are appended to it ==> updateBlock()
			 * However, note that, processChunk() and addtoUList() for
			 * that chunkID has already happened in the loop above.
			 * So, we merely need to updateBlock() 
			 */
	        ret = updateBlock(buf, blockID, volID, csize(*cdp), 
					lastblk_flag, COINCIDE, coincidingp, chunkIDUListp);
			if (ret)
				RET_ERR("updateBlock() error'ed\n");
		}
        else if (lastblk_flag == NOPOSTCHUNK_LASTBLK ||
                 lastblk_flag == POSTCHUNK_FULL_LASTBLK ||
                 lastblk_flag == NOT_LASTBLK)
		{
			printf("NOPOSTCHUNK_LASTBLK|POSTCHUNK_FULL_LASTBLK|NOT_LASTBLK\n");
			/* The chunk formed just above is the last chunk of the 
			 * block below. So, their end boundaries are coinciding 
			 * here, indicated by COINCIDE flag in updateBlock().
			 */
			ret = updateBlock(buf, blockID, volID, csize(*cdp),
						lastblk_flag, COINCIDE, coincidingp, chunkIDUListp);
			if (ret)
				RET_ERR("updateBlock() error'ed\n");
		}
		else 
#endif	//ifndef PROMAPPING_TEST
#endif	//ifndef PRODUMPING_TEST
		if (lastblk_flag == ULTIMATE_LASTBLK || lastblk_flag == GOODBLK_FLAG 
				|| lastblk_flag == SCAN_FIRSTBLK)
		{
#ifdef PDD_REPLAY_DEBUG_SS
			if (lastblk_flag == ULTIMATE_LASTBLK)
				fprintf(stdout, "coinciding ULTIMATE_LASTBLK\n");
#endif
			ret = processBlock(buf, blockID, volID, csize(*cdp), 
						COINCIDE, coincidingp, lastblk_flag);
			if (ret)
				RET_ERR("processBlock() error'ed\n");
		}
#else	/* NONSPANNING_PROVIDE */
		if (lastblk_flag == ONLYBLK)
		{
            ret = updateBlock(buf, blockID, volID, csize(*cdp),
	            lastblk_flag, COINCIDE, NULL, chunkIDUListp);
            if (ret)
	                RET_ERR("updateBlock() error'ed\n");			
		}
#endif	/* NONSPANNING_PROVIDE */
		else
		{
			RET_ERR("Unexpected case of lastblk_flag = %d. Fix\n", lastblk_flag);
		}
#ifdef PROCHUNKING_DEBUG_SSS
        WHERE;
#endif
		
    	*cdp = free_chunk_t(cdp); /* Free chunk and its mbuffer */
		*ldp = NULL;
    }

#ifndef NONSPANNING_PROVIDE
finishup:
	if (fixPrevBlock(initflag, 0, blockID, volID, *coincidingp, foundchunk, 
				AFTERBOUND))
    	RET_ERR("Error in fixPrevBlock()\n");	
#endif

	return 0;
}

#ifndef NONSPANNING_PROVIDE
static void note_prechunk_values(struct chunk_t *prechunk, __u16 *pre_len, 
		int *numblks_pre, __u32 *prechunk_blockID, __u32 blockID)
{
    /* Noting the blockID and len of first & potentially "partial" block */
    if (prechunk != NULL)
    {
        *pre_len = csize(prechunk) % BLKSIZE;
        *numblks_pre = csize(prechunk) / BLKSIZE;
        if (*pre_len)    /* partial block for prechunk beginning */
            *prechunk_blockID = blockID - *numblks_pre;
        else            /* starts at offset 0 of block */
            *prechunk_blockID = blockID - *numblks_pre - 1;
    }
    else                /* no prechunk */
    {
        *pre_len = 0;
        *numblks_pre = 0;
        *prechunk_blockID = blockID;
    }
}

void note_postchunk_values(struct chunk_t *postchunk, __u16 *post_len, 
		int *numblks_post, __u32 *postchunk_blockID, 
		__u32 blockID, int numblks_to_write)
{
    /* Noting the blockID and len of last & potentially "partial" block */
    if (postchunk != NULL)  /* yes postchunk */
    {
        *post_len = csize(postchunk) % BLKSIZE;
        *numblks_post = csize(postchunk) / BLKSIZE;
        *postchunk_blockID = blockID + numblks_to_write - 1;
        if (*post_len)   /* partial block for postchunk ending */
            *postchunk_blockID += *numblks_post + 1;
        else            /* ends at last offset of block */
            *postchunk_blockID += *numblks_post;
    }
    else                /* no postchunk */
    {
        *post_len = 0;
        *numblks_post = 0;
        *postchunk_blockID = blockID + numblks_to_write - 1;
    }
}
#endif

/** initialize_globals_for_writechunking
 * This is because, during scan chunking, every buf passed in is 
 * considered to be of BLKSIZE size and chunking is supposed to have
 * started at chunk 1, block 0, offset(s) 0 but in case of write chunking,
 * the prechunk could be having any start offset into block, not 
 * necessarily offset 0 and the starting block can be any as well.
 * So, these global variables are set before the write-chunking starts,
 * and within processChunk(), they are adjusted as normal, using pointers.
 * @param[in] pre_len
 * @param[in] prechunk_blockID
 * @param[in] blockID
 * @return status
 */

int initialize_globals_for_writechunking(int pre_len, 
		u64int prechunk_blockID, u64int blockID)
{
    if (pre_len == 0) /* prechunk_blockID <= blockID */
    {
        /* Prechunk starts at offset 0 of block or no prechunk at all */
        iolastBlkOffsetminus1 = dec_blkoffset(0);
        ioprevBoundaryBlkNum = prechunk_blockID - 1;
    }
    else if (pre_len && prechunk_blockID < blockID)
    {
        /* partial block for prechunk beginning */
        iolastBlkOffsetminus1 = dec_blkoffset(BLKSIZE - pre_len);
        ioprevBoundaryBlkNum = prechunk_blockID;
    }
    else if (pre_len && prechunk_blockID == blockID)
    {
        RET_ERR("This should not happen. Fix.\n");
    }
	return 0;
}

#ifndef NONSPANNING_PROVIDE
/* Following cases are possible during formation of write chunk buffer :-
 * 1. No prechunk or postchunk => vblk(s) being written had chunk(s) starting
 * 		exactly at offset 0 of vblk and ending exactly at last offset.
 * 2. Yes prechunk, no postchunk
 * 		a. Prechunk may start at offset 0 of some pre_blockID
 * 		b. Prechunk may start within some vblk pre_blockID 
 * 3. No prechunk, yes postchunk
 * 		c. Postchunk may end at last offset of some post_blockID
 * 		d. Postchunk may end within some vblk post_blockID
 * 4. Yes prechunk and postchunk
 * 		e. a + c
 * 		f. a + d
 * 		g. b + c
 * 		h. b + d
 * Some cases arise due to length of the prechunk, chunk & postchunk
 * A. 1 block
 * B. multiple exact blocks
 * C. multiple unintegral blocks
 *
 * All these cases are handled in the for loop below. The normal chunking
 * 		cases are :- 1, a, c and e, whereas
 * In cases b, g and h, the starting vblk is "partial". Here,
 * 		the "partial" vblk already has a chunkIDUList, we need to delete
 * 		the last chunkID from this list and then add the new chunk IDs that
 * 		are generated during normal chunking in processBlock(). Here, 
 * 		processBlock() should only append the chunkIDs and not start with
 * 		new list.
 * In cases d, f, h, the ending vblk is "partial". Here also, the 
 * 		"partial" vblk has a chunkIDUList, but we need to delete the
 * 		first chunkID from the old list, first append all the new generated
 * 		chunk IDs and then append the old list (minus the first).
 * @param[in] buf
 * @param[in] len
 * @param[in] volID
 * @param[in] blockID
 * @param[in,out] prechunk
 * @param[in,out] postchunk
 * @param[in,out] seqnextp
 * @return status		
 */
int perfWriteChunking(unsigned char *buf, __u16 len, __u16 volID, __u32 blockID,
	struct chunk_t **prechunk, struct chunk_t **postchunk, 
	struct chunkmap_t **seqnextp)
{
	int ret = 0;
    struct chunk_t *chunk;
    struct chunk_t *left;   /* no static */
    __u16 bytes_left=0; //left over from previous iteration
    //uint32_t ptime;  // To be noted when event occurs, is this needed? FIXME
	int numblks_to_write, numblks_pre, numblks_post;
    unsigned char *blkbuf;
    unsigned char *p;
	__u32 prechunk_blockID;	/* BlockID of first & "partial" block */
	__u16 pre_len;				/* Length of the first & "partial" block 
									due to first chunk of first vblk */
	__u32 postchunk_blockID;	/* BlockID of last & "partial" block */
	__u16 post_len;				/* Length of the last & "partial" block 
									due to last chunk of last vblk */
	__u16 curr_len;
	__u32 curr_blockID;

	__u32 iter;

#ifdef DEBUG_SS
	assert(len % BLKSIZE == 0);
#endif
	numblks_to_write = len / BLKSIZE;

	note_prechunk_values(*prechunk, &pre_len, &numblks_pre, &prechunk_blockID,
							blockID);

	note_postchunk_values(*postchunk, &post_len, &numblks_post, 
						&postchunk_blockID, blockID, numblks_to_write);

	/* Appending "buf" data to "prechunk" and output as chunk */
    ret = createInitialChunkBuf(&chunk, buf, len, prechunk, &bytes_left);
    if (ret)
    {
        RET_ERR("createInitialChunkBuf err in doWriteChunking\n");
    }
	assert(bytes_left == pre_len && *prechunk == NULL);

	/* Appending "postchunk" data to above-created chunk aka "left" */
    left = chunk;
    chunk = NULL;
    ret = createInitialChunkBuf(&chunk, cdata(*postchunk), csize(*postchunk), 
								&left, &bytes_left);
    if (ret)
    {
        RET_ERR("createInitialChunkBuf err in 2nd doWriteChunking\n");
    }
	assert(bytes_left == (csize(chunk) - post_len) && left == NULL);

#ifdef PROCHUNKING_DEBUG_SSS
    WHERE;
    fprintf(stdout, "Chunk buffer ready for write chunking\n");
#endif

	/* Noting total length of entire created chunk buffer */
    curr_len = csize(chunk);
	curr_blockID = prechunk_blockID;

	/* Setting Globals to be used during write chunking, in processChunk() */
	if (initialize_globals_for_writechunking(pre_len,prechunk_blockID,blockID))
	{
		RET_ERR("Error in initialize_globals_for_writechunking()\n");
	}

	p = cdata(chunk);
	/* Chunking the write buffer, (partial) block by (partial) block */
	for (iter=0; iter < postchunk_blockID - prechunk_blockID + 1; iter++)
	{
		__u16 buflen;
		int lastblk_flag = 0;
		if (iter == 0 && pre_len > 0)	/* first iter, "partial" */
		{
			buflen = pre_len;
			lastblk_flag = PRECHUNK_PARTIAL_FIRSTBLK;
#ifdef PROCHUNKING_DEBUG_SSS
			/* "Partial" block buf for initial chunking step (iter==0) */
			unsigned char *str1 = malloc(pre_len);
			unsigned char *str2 = malloc(buflen);
			memcpy(str1, cdata(*prechunk), pre_len);
			memcpy(str2, p, buflen);
			assert(!strcmp(str1, str2));	/* Should hold if copying worked */
#endif
		}
		else if (iter == postchunk_blockID - prechunk_blockID
			&& post_len > 0 && post_len < BLKSIZE) /* last iter, "partial" */
		{
			buflen = post_len;
			lastblk_flag = POSTCHUNK_PARTIAL_LASTBLK;
#ifdef PROCHUNKING_DEBUG_SSS
			if (str1) free(str1);
			if (str2) free(str2);
			/* "Partial" block buf for last chunking step (iter==) */
			str1 = malloc(post_len);
			memcpy(str1, cdata(postchunk) + csize(*postchunk)- post_len,
                            post_len);
			str2 = malloc(buflen);
			memcpy(str2, p, buflen);
			assert(!strcmp(str1, str2));	/* Should hold if copying worked */
#endif
		}
		else		/* middle iterations, normal block size & chunking */
		{
			buflen = BLKSIZE;
			if ((iter == postchunk_blockID - prechunk_blockID)
            		&& postchunk_blockID == blockID + numblks_to_write - 1)
				lastblk_flag = NOPOSTCHUNK_LASTBLK;
			else if ((iter == postchunk_blockID - prechunk_blockID)
					&& postchunk_blockID > blockID + numblks_to_write - 1)
				lastblk_flag = POSTCHUNK_FULL_LASTBLK;
			else
				lastblk_flag = NOT_LASTBLK;
		}
		assert(curr_len >= buflen);
		assert(*p != '\0');
		blkbuf = malloc(buflen);
       	memcpy(blkbuf, p, buflen);

        /* Not checking for zero block here, because it's not a possibility
         * unless an existing block can be over-written with zero block. If so,
         * then fix here to handle it.
         */
    	ret = resumeChunking(blkbuf, buflen, volID, curr_blockID, NOINIT_STAGE,
						lastblk_flag, seqnextp, 0);
	    if (ret)
    	{
        	RET_ERR("resumeChunking err in %u doWriteChunking\n", iter);
    	}

		p = buf + buflen;
		curr_len -= buflen;
		curr_blockID++;
        if (blkbuf)
            free(blkbuf);
    }
	assert(curr_blockID == postchunk_blockID + 1);
	assert(*p == '\0');

	free_chunk_t(&chunk);
    return 0;
}
#endif

chunk_size_t inc_chunkoffset(chunk_size_t val)
{
    if (val == MAXCHUNKSIZE - 1)
        val = 0;
    else
        val++;
    return val;
}

chunk_size_t dec_chunkoffset(chunk_size_t val)
{
    if (val == 0)
        val = MAXCHUNKSIZE - 1;
    else
        val--;
    return val;
}


