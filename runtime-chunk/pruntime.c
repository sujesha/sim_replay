
#include <asm/types.h>
#include <assert.h>
#include "debug.h"
#include "md5.h"
#include "defs.h"
#include "utils.h"
#include "serveio-utils.h"
#include "vmbunching_structs.h"
#include "request-generic.h"
#include "chunking.h"
#include "replay-defines.h"
#include "unused.h"
#include "simdisk-API.h"
#include "v2p-map.h"
#include "content-gen.h"

extern int disksimflag;
extern int collectformat;
extern const char zeroarray[65537];
extern unsigned char vmaphit_flag;

/* Metadata update in the read (& write)path for runtime map creation for PROVIDED */
//FIXME
int perfReadWriteChunking(struct preq_spec *preq, __u16 volID, __u32 blkID)
{
	int len;
	__u8* buf = NULL;

#ifndef NONSPANNING_PROVIDE	
	/* In PROVIDED, whether disk is simulated or not, the content length has
	 * to be BLKSIZE, because content needs to be Rabin chunked.
	 */
	assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY);
	buf = preq->content;
#else
	assert(DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
	__u8 *simcontent = malloc(BLKSIZE);
	if (simcontent == NULL)
		RET_ERR("malloc failed\n");
    __u32 orig_ioblk;
    if (getVirttoPhysMap(volID, blkID, &orig_ioblk))
        VOID_ERR("getVirttoPhysMap error'ed\n");
	if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
		len = BLKSIZE;
	else 
		len = MD5HASHLEN_STR-1;		//used in disk_read_trap, then changed.
    if (orig_ioblk == preq->ioblk)
        memcpy(simcontent, preq->content, BLKSIZE);
    else
		disk_read_trap(volID, blkID, simcontent, len);
	if (collectformat)
	{
		unsigned char tempbuf[MD5HASHLEN_STR-1];
		memcpy(tempbuf, simcontent, MD5HASHLEN_STR-1);
		memset(simcontent, 0, BLKSIZE);
		generate_BLK_content(simcontent, tempbuf, MD5HASHLEN_STR-1, BLKSIZE);
	}
	buf = simcontent;
#endif

	len = BLKSIZE;	//set here, not before!

	/* Setting Globals to be used during write chunking, in processChunk() */
	if (initialize_globals_for_writechunking(0, blkID, blkID))
	{
		RET_ERR("Error in initialize_globals_for_writechunking()\n");
	}

	/* Check if block is zero block when disk is not being simulated */
	if (memcmp(buf, zeroarray, BLKSIZE) == 0)
	{	
        if (resumeDynChunking(buf, len, volID, blkID, ZEROBLK_FLAG, preq->rw))
			RET_ERR("resumeDynChunking(%u, %u) failed for zero in perfReadWriteChunking.\n", volID, blkID);
	}
	else	/* not a zero block => good block */
	{
		if (resumeDynChunking(buf, len, volID, blkID, GOODBLK_FLAG, preq->rw))
			RET_ERR("resumeDynChunking(%u, %u) failed for good in perfReadWriteChunking\n", volID, blkID);
	}

#ifdef NONSPANNING_PROVIDE
	free(simcontent);
#endif	
	return 0;
}

/* Metadata update for PROVIDED FIXME*/
int p_mapupdate(struct preq_spec **preql, struct vm_pkt *blkReq, int vop_iter)
{
    int ret = 0, i=vop_iter;
    __u32 blkID;

#if defined (PDDREPLAY_DEBUG_SS_DONE) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(vop_iter >= 0);
    assert(preql != NULL);
    assert(*preql != NULL);
	assert(vmpkt != NULL);
#endif

	__u32 firstvBlkID = getVirtBlkID(blkReq);
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	__u16 volID = (__u16) ret;

	if (vmaphit_flag)
	{
		__u16 revertvolID;
		__u32 revertvBlkID;
        /* If metadata already exists, only need to update the dedupfetch
         * flag per chunk of the blocks fetched for the read request.
         * There will be multiple i only when metadata exists, and
         * only single block (vop_iter) if metadata doesnt exist.
         * So, here invoke updateDedupfetch().
         */
		//return 0;	//do nothing.

		/* FIXME: Re-think regarding this update dedupfetch. It should happen
		 * for the blocks that were actually fetched i.e. preql
		 * and not for the blocks that were originally requested, i.e.blkReq.
		 * If update dedupfetch done for originally requested blkReq,
		 * performance is worse than if not updated at all.
		 */
        //return updateDedupfetch(volID, firstvBlkID);
		if (revertPhystoVirtMap((*preql+(i)), &revertvolID, &revertvBlkID))
			RET_ERR("error in revertPhystoVirtMap\n");
		return updateDedupfetch(revertvolID, revertvBlkID);
	}

	/* Handling one block at a time corresponding to multi-block request */
		blkID = firstvBlkID + i;

		/* Metadata update */
		ret = perfReadWriteChunking((*preql+(i)), volID, blkID);
        if (ret)
            RET_ERR("perfReadWriteChunking err in %u pread_mapupdate\n", 
					(*preql+(i))->ioblk);

	return 0;
}
