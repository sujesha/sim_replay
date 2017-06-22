/* Contains code that is common to PROVIDED and CONFIDED and STANDARD */

#include <asm/types.h>
#include <assert.h>
#include <string.h>
#include "defs.h"
#include "vmbunching_structs.h"
#include "debug.h"
#include "pdd_config.h"
#include "utils.h"
#include "v2p-map.h"
#include "md5.h"
#include "replay-defines.h"

#if 0
//TODO: Copy c2v content to another c2v and use list_del on this one
//Return this one
C2V_tuple_t* pop_c2v_from_c2vmaps(struct chunkmap_t *c2pv, __u16 volID, 
                                                            __u32 vBlkID)
{
}
#endif

extern int disksimflag;
extern int collectformat;
extern int preplayflag;

int vblkIDMatch(__u16 newvolID,__u32 newblkID, __u16 volID, __u32 vBlkID)
{
    if (newvolID == volID && newblkID == vBlkID)
        return 1;
    else
        return 0;
}

/** getVirtBlkID -- given the request, retrieve the virtual block ID
 *
 * @blkReq[in]: Read/Write request to be serviced
 * @return: virtual block ID of the (first) block being requested
 */
__u32 getVirtBlkID(struct vm_pkt *blkReq)
{
#ifdef DEBUG_SS
    assert(BLKSIZE == K4_SIZE);
#endif
    return (blkReq->block);
}

/* retrieveBuf -- used to retrieve the buffer content within write requests 
 *
 * @buf[out]: buffer of content
 * @blkReq[in]: Write request to be serviced
 * @return: status
 */
int retrieveBuf(unsigned char **buf, struct vm_pkt *blkReq)
{
#ifdef DEBUG_SS
    assert(buf != NULL && *buf == NULL);
#endif
    if (blkReq == NULL || blkReq->nbytes == 0)
        RET_ERR("The data buffer is either empty or of size zero\n");
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "retrieveBuf vmname=%s\n", blkReq->vmname);
#endif

	/* Allocate memory to hold content and copy content over */
	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
		DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
	{
    	gen_malloc(*buf, unsigned char, MD5HASHLEN_STR-1);
	    memcpy(*buf, blkReq->content, MD5HASHLEN_STR-1);
		//(*buf)[HASHLEN_STR-1] = '\0';	no null char to be copied
	}
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
    	gen_malloc(*buf, unsigned char, blkReq->nbytes);
	    memcpy(*buf, blkReq->content, blkReq->nbytes);
	}

    return 0;
}

/** getVolNum -- given the request, retrieve the volume (VM) ID
 *
 * @blkReq[in]: Read/Write request to be serviced
 * @return: volume ID of the block(s) being requested
 */
int getVolNum(struct vm_pkt *blkReq)
{
    int ret;
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "vmname=%s\n", blkReq->vmname);
#endif
    ret = get_volID(blkReq->vmname);
    if (ret < 0)
	{
        RET_ERR("get_volID returned error for %s\n", blkReq->vmname);
	}

    return ret;
}

inline __u32 getNumBlocks(struct vm_pkt *blkReq)
{
    if (blkReq->nbytes % BLKSIZE != 0)
    {
        RET_ERR("Number of bytes (%u) not multiple of BLKSIZE %d\n",
                        blkReq->nbytes, BLKSIZE);
    }
    return ((__u32) (blkReq->nbytes/BLKSIZE));
}

inline u32int getNBytes(struct vm_pkt *blkReq)
{
    return blkReq->nbytes;
}


