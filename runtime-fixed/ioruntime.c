
#include <assert.h>
#include "debug.h"
#include "md5.h"
#include "defs.h"
#include "utils.h"
#include "serveio-utils.h"
#include "contentcache.h"
#include "iodeduping.h"
#include "ioruntime.h"
#include "replay-defines.h"
#include <time.h>

inline __u64 gettime(void);
extern FILE * ftimeptr;

extern int warmupflag;
extern int disksimflag;
extern int collectformat;
extern unsigned char cmaphit_flag;

/* Metadata update in the read (& write) path for runtime map creation for IODEDUP */
int perfReadWriteDeduping(struct preq_spec *preq)
{
	int ret = 0, len;
	__u8* buf = NULL;

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

	ret = resumeDeduping(buf, len, preq->ioblk, NOINIT_STAGE,
                        DONTCARE_LASTBLK, preq->rw);
    if (ret)
  		RET_ERR("resumeDeduping err in %u perfReadDeduping\n",
                    preq->ioblk);

	return 0;
}

/* io_mapupdate --- Metadata update for IODEDUP , one blk at a time.
 * 		Is invoked via thread for map update during writes, 
 * 		and invoked inline for runtime map update during reads.
 *
 * @preql[in]: list of blks for whom map to be updated
 * @vop_iter[in]: index into list
 */
int io_mapupdate(struct preq_spec **preql, int vop_iter)
{
	int i=vop_iter, ret;
#if defined (SIMREPLAY_DEBUG_SS_DONE) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(vop_iter >= 0);
    assert(preql != NULL);
    assert(*preql != NULL);
#endif

	/* Handling one packet at a time */

		if ((*preql+(i))->done)	/* read requests had bcache or ccache hit */
			return 0;

		/* Metadata update */
		ret = perfReadWriteDeduping((*preql+(i)));
        if (ret)
            RET_ERR("perfReadDeduping err in %u ioread_mapupdate\n", 
					(*preql+(i))->ioblk);
	return 0;
}

/* Content cache update for IODEDUP */
int ioread_contentcacheupdate(struct preq_spec **preql, int vop_iter)
{
	int i=vop_iter;
	unsigned long long stime=0, etime=0;

	/* Handling one packet at a time */

		if ((*preql+(i))->done) /* read requests had bcache or ccache hit */
			return 0;

		stime = gettime();	/* START IODEDUP content-cache update time */
		if (overwrite_in_contentcache((*preql+(i)), 0))
			RET_ERR("error overwrite_in_contentcache: ioread_contentcacheupdate\n");
		etime = gettime();	/* END IODEDUP content-cache update time */
		ACCESSTIME_PRINT("content-cache-update-for-read time: %llu %d\n",
						 etime - stime, vop_iter);
	return 0;
}
