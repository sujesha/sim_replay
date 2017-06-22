#include <asm/types.h>
#include <assert.h>
#include "debug.h"
#include "serveio-utils.h"
#include "serveio.h"
#include "vmbunching_structs.h"
#include "replay-defines.h"

extern int disksimflag;
extern int collectformat;

#ifdef PRO_STATS
	unsigned long stotalreq = 0;	/* Including read/write reqs */
	unsigned long stotalblk = 0;	/* Including read/write blks */

	unsigned long stotalreadreq = 0;	/* Read req received */
	unsigned long stotalwritereq = 0;	/* Write req received */

	unsigned long stotalblkread = 0;	/* Count of blks to-be-read */
	unsigned long stotalblkwrite = 0;	/* Count of blks to-be-written */

	/* Following asserts should hold on above variables :-
	 * -- stotalreadreq + stotalwritereq == stotalreq
	 * -- stotalblkread + stotalblkwrite == stotalblk
	 */

#endif 

/* This is the standard V2P lookup to get pblk IDs for fetching
 * The blkReq can specify fetching of block of any size (but multiple
 * of BLKSIZE). 
 *
 * @blkReq[in]: VM I/O request --- blkReq->bytes should be multiple of BLKSIZE
 * @preql[out]: List of mapped pblks
 * @nreq[out]: Number of pblks 
 * @return: status
 */
int standardReadRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq)
{
	int i;
	int ret;
	__u16 volID;
	__u32 vBlkID;
#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s, nbytes=%d\n", __FUNCTION__, blkReq->nbytes);
	assert(preql != NULL);
	assert(blkReq->nbytes > 0);
#endif 

	if (disksimflag)
	{
		/* Vanilla never needs to do scanning in reality, and doing sequential
		 * replay of requests similar to CONFIDE or PROVIDE is not useful for
		 * any evaluation, because only those two can compare the similarity of
		 * content, not Vanilla. So Vanilla is, for now, to run only in the
		 * regular case DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY.
		 * But this is also used for IODEDUP replay!
		 */
		assert((DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY)
				|| (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
				|| (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
				|| (DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY)
				|| DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY
				|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
		assert(blkReq->nbytes == BLKSIZE);
	}

	if (blkReq->nbytes % BLKSIZE != 0)
		RET_ERR("Num of bytes requested (%u) should be multiple of BLKSIZE\n",
				blkReq->nbytes);

	*nreq = 0;
    vBlkID = getVirtBlkID(blkReq);
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	volID = (__u16) ret;

	for (i=0; i< (int)(blkReq->nbytes/BLKSIZE); i++)
	{
		if (elongate_preql(preql, nreq))
        {
        	RET_ERR("realloc error for preql\n");
        }
		if (disksimflag)
			assert(blkReq->content != NULL);
		else
			assert(blkReq->content == NULL);
	
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
	       	create_preq_spec(volID, vBlkID+i,
                BLKSIZE, blkReq->rw, blkReq->content + i*(MD5HASHLEN_STR-1),
                0, BLKSIZE-1, *preql+(*nreq-1));
		else
	       	create_preq_spec(volID, vBlkID+i,
                BLKSIZE, blkReq->rw, blkReq->content + i*BLKSIZE,
                0, BLKSIZE-1, *preql+(*nreq-1));
	}

	return 0;
}

int standardWriteRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq)
{
	int i;
	int ret;
	__u16 volID;
	__u32 vBlkID;
#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(preql != NULL);
	assert(blkReq->nbytes >= BLKSIZE);
#endif 

	if (disksimflag)
		assert(blkReq->nbytes == BLKSIZE);
	assert(blkReq->content != NULL);

	if (blkReq->nbytes % BLKSIZE != 0)
		RET_ERR("Num of bytes requested (%u) should be multiple of BLKSIZE\n",
				blkReq->nbytes);

	*nreq = 0;
    vBlkID = getVirtBlkID(blkReq);
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	volID = (__u16) ret;

	for (i=0; i< (int)(blkReq->nbytes/BLKSIZE); i++)
	{
		if (elongate_preql(preql, nreq))
        {
        	RET_ERR("realloc error for preql\n");
        }
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
    	    create_preq_spec(volID, vBlkID+i,
                BLKSIZE, blkReq->rw, blkReq->content + i*(MD5HASHLEN_STR-1),
                0, BLKSIZE-1, *preql+(*nreq-1));
		else
	        create_preq_spec(volID, vBlkID+i,
                BLKSIZE, blkReq->rw, blkReq->content + i*BLKSIZE,
                0, BLKSIZE-1, *preql+(*nreq-1));
	}
#if defined (PDDREPLAY_DEBUG_SS)
	fprintf(stdout, "nreq = %d\n", *nreq);
#endif
	return 0;
}
