
#include <assert.h>
#include "debug.h"
#include "md5.h"
#include "utils.h"
#include "fixing.h"
#include "serveio-utils.h"
#include "pdd_config.h"
#include "vmbunching_structs.h"
#include "request-generic.h"
#include "replay-defines.h"

extern int preplayflag;
extern int disksimflag;
extern int collectformat;
extern const char zeroarray[65537];

/* Metadata update in the read path for runtime map creation for CONFIDED 
 * and also in write pateh
 */
int perfReadWriteFixing(struct preq_spec *preq, __u16 volID, __u32 blkID)
{
	int len;
	__u8 *buf = NULL;

	/* If disk is being simulated, even read requests have non-NULL content.
	 */
	if (disksimflag || !preq->rw)
		buf = preq->content;

	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
		len = MD5HASHLEN_STR-1;
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY ||
  				DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
		len = BLKSIZE;
	}

	/* Check if block is zero block.   */
	if (memcmp(buf, zeroarray, BLKSIZE) == 0)
	{	
        if (resumeFixing(buf, len, volID, blkID, NOINIT_STAGE, ZEROBLK_FLAG,
					!preq->rw, preq->rw))
			RET_ERR("resumeFixing(%u, %u) failed for zero in perfReadWriteFixing.\n",
                    volID, blkID);
	}
	else	/* not a zero block => good block */
	{
		if (resumeFixing(buf, len, volID, blkID, NOINIT_STAGE, GOODBLK_FLAG,
					!preq->rw, preq->rw))
			RET_ERR("resumeFixing(%u, %u) failed for good in perfReadWriteFixing\n",
                    volID, blkID);
	}

	return 0;
}

/* Metadata update for CONFIDED */
int f_mapupdate(struct preq_spec **preql, struct vm_pkt *blkReq, int vop_iter)
{
    int ret = 0, i=vop_iter;
    __u32 blkID;

#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(vop_iter >= 0);
    assert(preql != NULL);
    assert(*preql != NULL);
#endif

	__u32 firstvBlkID = getVirtBlkID(blkReq);
	if ((ret = getVolNum(blkReq)) < 0)	//One Volume corresponds to one VM
	{
		RET_ERR("failed in getVolNum\n");
	}
	__u16 volID = (__u16) ret;

	/* Handling one packet at a time */

		if ((*preql+(i))->done) /* read req block was found in cache */
			return 0;

		/* If we are here, either it is write requests, or
		 * it is a read request with cache miss. However, status of read req
		 * metadata existence is not known. We need to update metadata
		 * for all write requests, but only for those read requests whose
		 * metadata doesnt exist. This check is done within resumeFixing()
		 * because alongwith that check, we also need to take care of
		 * inconsisten trace records, if any.
		 */
		blkID = firstvBlkID + i;

		/* Metadata update */
		ret = perfReadWriteFixing((*preql+(i)), volID, blkID);
        if (ret)
            RET_ERR("perfReadFixing err in %u fread_mapupdate\n", 
					(*preql+(i))->ioblk);

	return 0;
}
