#include <asm/types.h>
#include <assert.h>
#include <string.h>
#include "serveio-utils.h"
#include "v2p-map.h"
#include "utils.h"
#include "debug.h"
#include "md5.h"
#include "replay-defines.h"
#include "blkidtab-API.h"

int runtimemap = 0;

extern int preplayflag;
extern int disksimflag;
extern int collectformat;

int elongate_preql(struct preq_spec **preql, int *nreq)
{
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
    (*nreq)++;
    *preql = realloc(*preql, *nreq * sizeof(struct preq_spec));
//fprintf(stdout, "no eror here\n");
    if (*preql == NULL)
    {
        RET_ERR("malloc error for preql\n");
    }
    return 0;
}

void directcreate_preq_spec(__u32 ioblkID, __u32 bytes, int rw,
        __u8 *content, __u16 start, __u16 end, struct preq_spec *preq)
{
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    assert(preq != NULL);
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
	
	preq->ioblk = ioblkID;	/* No need for V2P mapping lookup, just direct assign */
    preq->bytes = bytes;
    preq->rw = rw;
    if (!rw)
    {
        /* write request contains content as well */
#ifdef PDD_REPLAY_DEBUG_SS_DONE
		assert(content != NULL);
#endif
		gen_malloc(preq->content, __u8, preq->bytes);
        memcpy(preq->content, content, preq->bytes);

        /* For write request, start and end offsets are dont care */
        preq->start = 0;
        preq->end = 0;
    }
    else
    {
        preq->content = NULL;
        preq->start = start;
        preq->end = end;
    }
}

/* This is only to aid in simulation.
 * In real implementation, volID and blockID would be known within the
 * VM address space, after the request has been served. But in our 
 * simulation, we had continued to use the struct preq_spec itself. 
 * Hence, in this function, we will quickly map the blkidkey entry
 * of struct preq_spec into corresponding volID, blockID, particularly
 * to be used before invoking updateDedupfetch() for PROVIDED.
 */
int revertPhystoVirtMap(struct preq_spec *preq, __u16* volID, __u32* blockID)
{
	char *volid_str = NULL;
	char *vblkid_str = NULL;
	char *ptr = preq->blkidkey;
	char *rest = ptr;
	unsigned int tempvolID, tempblockID;
	assert(ptr != NULL);

	if (readnexttoken(&ptr, ":", &rest, &volid_str))
		RET_ERR("readnexttoken failed in %s\n", __FUNCTION__);
	sscanf(volid_str, "%u", &tempvolID);

	if (readnexttoken(&ptr, ":", &rest, &vblkid_str))
		RET_ERR("readnexttoken failed in %s\n", __FUNCTION__);
	sscanf(vblkid_str, "%u", &tempblockID);

	*volID = (__u16)tempvolID;
	*blockID = (__u32)tempblockID;

	return 0;
}

void create_preq_spec(__u16 volID, __u32 blockID, __u32 bytes, int rw,
        __u8 *content, __u16 start, __u16 end, struct preq_spec *preq)
{
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    assert(preq != NULL);
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
#ifdef SIMREPLAY_DEBUG_SS_DONE
		fprintf(stdout, "blockID=%u\n", blockID);
#endif

    assert(preq != NULL);
    if (getVirttoPhysMap(volID, blockID, &preq->ioblk))
        VOID_ERR("getVirttoPhysMap error'ed\n");
#if defined (PDDREPLAY_DEBUG_SS)
	fprintf(stdout, "(%u, %u) => %u\n", volID, blockID, preq->ioblk);
#endif

    preq->bytes = bytes;
    preq->rw = rw;
	preq->done = 0;
	preq->bcachefound = 0;
    /* For write request, start and end offsets are dont care */
    preq->start = start;
    preq->end = end;

	if (disksimflag)
	{
		char skey[20];

		/* construct key into blkid hash-table */
		construct_key_volid_blkid(volID, blockID, skey);
		preq->blkidkey = strdup(skey);	//free in sim_map_n_process_bunch()
	}
	else
		preq->blkidkey = NULL;


	/* If disk simulation, both read and write requests have content */
	//if ((!preplayflag && disksimflag) || !preq->rw)
	//	assert(content != NULL);
	if (disksimflag || !preq->rw)
		assert(content != NULL);
	else if (preq->rw || preplayflag)
		assert(content == NULL);

	if (content)
	{
		if ((DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY))
		{
			int len = MD5HASHLEN_STR-1;
			gen_malloc(preq->content, __u8, len);
    	    memcpy(preq->content, content, len);
		}
		else
		{
			assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY || 
				DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
			gen_malloc(preq->content, __u8, BLKSIZE);
	        memcpy(preq->content, content, BLKSIZE);
		}
	}
	else
	{
		preq->content = NULL;
	}
}

void copy_preq_spec(struct preq_spec *preq, struct preq_spec *dest)
{
#ifdef DEBUG_SS
    assert(preq != NULL && dest != NULL);
#endif

	dest->ioblk = preq->ioblk;
    dest->bytes = preq->bytes;
    dest->rw = preq->rw;
    if (!preq->rw)
    {
        gen_malloc(dest->content, __u8, preq->bytes);
        memcpy(dest->content, preq->content, preq->bytes);

        /* For write request, start and end offsets are dont care */
        dest->start = 0;
        dest->end = 0;
    }
    else
    {
		if (preq->content)
		{
        	gen_malloc(dest->content, __u8, preq->bytes);
	        memcpy(dest->content, preq->content, preq->bytes);
		}
		else
	        dest->content = NULL;
        dest->start = preq->start;
        dest->end = preq->end;
    }

	if (preq->blkidkey)
		dest->blkidkey = strdup(preq->blkidkey);
	else
		dest->blkidkey = NULL;
}
