/* This file stores the physical-to-dedup mapping in vectors.
 * Physical refers to the logical blocks on the host machine, and
 * Dedup refers to the deduplication fixed block/content.
 * Multiple physical blocks with same content map to the same dedup block!
 */

#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <asm/types.h>
#include <string.h>
#include "vector32.h"
#include "utils.h"
#include "p2d-map.h"
#include "debug.h"
#include "slist.h"				/* slist_head */
#include "pdd_config.h"
#include "iodeduping.h"
#include "ulist.h"
#include <time.h>

#ifdef IOMAPPING_TEST
	extern FILE * fhashptr;
#endif
extern char *V2PmapFile;		/* input V2P mapping file */
extern int runtimemap;
extern FILE * ftimeptr;
inline __u64 gettime(void);

int p2dmaps_alive = 0;

//char zeroblkhash[10];
vector32 * p2dmaps = NULL;

/* create_p2d_mapping_space: Use this to initialize the p2dmaps 1D vector
 * before starting off sim_ioreplay thread.
 * Note that unlike v2fmaps which is a 2D vector because of (volID, ioblkID) 
 * tuples, pd2maps is just a 1D vector which needs to store only ioblkID.
 * So, just initialize vector32 as p2dmaps.
 */
void create_p2d_mapping_space()
{
	if (p2dmaps_alive == 1)
	{
        fprintf(stdout, "p2dmaps already non-NULL\n");
        return;
    }
	p2dmaps_alive = 1;
#ifdef PDDREPLAY_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	p2dmaps = calloc(1, sizeof(vector32));
	vector32_init(p2dmaps);

	return;
}

/* p2dmaps_free: Use this to free the entire p2dmaps mapping. This should
 * be done before exiting the program at the very end.
 */
void free_p2dmaps(void)
{
	__u32 i;		/* For iterating over ioblkID */
	p2d_datum *ptri;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	if (p2dmaps_alive == 0)
	{
		fprintf(stdout, "p2dmaps being freed already\n");
		return;
	}
	p2dmaps_alive = 0;

	for (i = 0; i < vector32_size(p2dmaps); i++)	/* Iterating over ioblkID */
    {
        if ((ptri = (p2d_datum*)vector32_get(p2dmaps, i)) != NULL)
			free(ptri);		/* Freeing the ioblk info */
	}
	free(p2dmaps->data);
	free(p2dmaps);
	fprintf(stdout, "done now\n");
}

/* p2dmaps_set: This function sets a p2d_datum at location(x) 
 * within 1D vector p2dmaps. Use this in processBlockio() and updateBlockio()
 */
void p2dmaps_set(vector32 *p2dmaps, __u32 x, void *e)
{
#if 0
    vector32 *ptr;		/* Holds single volume row */

    ptr = vector32_get(p2dmaps, x);

    if (ptr != NULL)
		free(ptr);
#endif
	vector32_set(p2dmaps, x, e);  /* Add new block to 1D vector */
	//FIXME: instead of vector, use hash-tables on blkID

    return;
}

/* p2dmaps_get: This function retrieves a p2d_datum from location (x)
 * within 1D vector p2dmaps. Use this function in updateBlockio()
 * and getVirttoDedupMap().
 */
void *p2dmaps_get(vector32 *p2dmaps, __u32 x)
{
	void *e;

    if (x >= p2dmaps->size && !runtimemap)
    {
        fprintf(stderr,"p2dmaps_get:index %u>=p2dmaps->size %u\n",
						x, p2dmaps->size);
        return NULL;
    }
	else if (x >= p2dmaps->size)
		return NULL;

    e = vector32_get(p2dmaps, x);
    if (e == NULL)
    {
		if (!runtimemap)
	        fprintf(stderr, "p2dmaps_get:element doesnt exist at index %u\n", x);
        return NULL;
    }

    return e;
}

#if 0
/* 
 * Hash for zero block is to be stored as all zeroes (FIXME change this 
 * all zeroes hash value if it is not okay).
 */
static void note_bhashkey_zerovblkf(p2d_datum *v)
{
    memcpy(v->bhashkey, zeroblkhash, HASHLEN + MAGIC_SIZE);
}
#endif

int notzeroIO_vblk(p2d_datum *p2d)
{
#ifdef REPLAYDIRECT_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(p2d != NULL);
#endif

	if (p2d->iodedupID != 0)
	{
#ifdef REPLAYDIRECT_DEBUG_SS
		fprintf(stdout, "found non-zero GOODBLK\n");
#endif
		return 1;
	}
	else 
	{
#ifdef REPLAYDIRECT_DEBUG_SS
		fprintf(stdout, "found ZEROBLK\n");
#endif
		return 0;
	}
}

#if 0
/* Is invoked from note_p2d_map() */
void note_block_attrsf(p2d_datum *p2d, unsigned char *key,
            __u32 ioblkID, __u16 volID, int lastblk_flag)
{
    p2d->ioblkID = ioblkID; //FIXME: is this redundant?
    p2d->volID = volID; //FIXME: is this redundant?

    if (lastblk_flag == ZEROBLK_FLAG)
    {
        note_bhashkey_zerovblkf(p2d);
    }
	else
	{
    	/* If HASHMAGIC is disabled, MAGIC_SIZE = 0, so wont get copied anyway */
    	memcpy(p2d->bhashkey, key, HASHLEN + MAGIC_SIZE);
	}
	return;
}
#endif

/* Handles both zero iodedupID and non-zero iodedupIDs 
 * Is invoked from processBlockio() and updateBlockio().
 */
int note_p2d_map(p2d_datum *p2d, __u32 iodedupID,
        //unsigned char *key, __u32 ioblkID, __u16 volID, 
		int lastblk_flag)
{
#ifdef DEBUG_SS
	assert(p2d != NULL);
#endif

    if (lastblk_flag == ZEROBLK_FLAG)
        p2d->iodedupID = 0;
	else
		p2d->iodedupID = iodedupID;
	p2d->pdirty = 0;		/* Reset the dirty flag */
    return 0;
}

/** dedup_dirty -- to check whether the specified dedup mapping entry is dirty,
 * 				i.e. points to data that has been written recently, and has not
 * 				been updated in the mapping yet.
 *
 *	@p2d[in]: the P2D entry to be checked
 *	@return: TRUE(1) or FALSE (0)
 */
int dedup_dirty(p2d_datum *p2d)
{
    if (p2d->pdirty)
    {
#ifdef CON_DEBUG_SS
        fprintf(stdout, "Dedup is dirty, so fetch original block itself\n");
#endif
        return 1;
    }
    return 0;
}

/* Look up P2D mapping for given volume (VM) and ioblkID, and return 
 * linked list (p2dlistp) of mapping of "count" number of items.
 * @param[in] ioblkID
 * @param[in] count
 * @return list of p2d maps or NULL if ALL count # of vblks are zeroblks
 */
int getVirttoDedupMap(__u32 ioblkID, __u16 count, 
				struct slist_head *p2dlistp)
{
    int success = 0;
    __u16 i;
    p2d_datum *p2d;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

    for (i = 0; i < count; i++)
    {
		int ret;
        p2d = (p2d_datum*) p2dmaps_get(p2dmaps, ioblkID + i);
        /* A zero vblk will still return p2d map containing iodedupID = 0
         * So, check to see whether we have hit a non-zero block yet.
         * If so, success = 1, else not.
         */
		if (p2d == NULL && !runtimemap)
		{
			RET_ERR("ioblkID %u is beyond the capacity, check %s\n",
				ioblkID + i, V2PmapFile);
		}
		else if (p2d == NULL)
			return -1;
        ret = notzeroIO_vblk(p2d);
		if (ret == 1)
            success = 1;
        	
		/* Add the p2d to p2dlistp */
        slist_add(&p2d->head, p2dlistp);	//add whether zeroblk or not.

#if 0
		else if (ret == 0)
			RET_ERR("ioblkID (%u) has no iodedupID?? Fix this.\n",
				ioblkID + i);
#endif

    }

    /*  If ALL count # of vblks are zeroblks, return so. List is also returned
     */
    if (success == 0)
    {
#ifdef PDDREPLAY_DEBUG_SS_DONE
        fprintf(stderr, "Only zero blocks encountered, no GOODBLK\n");
#endif
		return ZEROBLK_FLAG;
    }

    /*  Retain their p2d maps as-is within the list
     *  and return the list.
     */
    return 0;
}

/* Updating a block boundary, by modifying the p2d map for given vblk
 *
 * @param buf
 * @param ioblkID
 * @param lastblk_flag
 * @return status
 */
int updateBlockio(__u32 ioblkID,
                int lastblk_flag, __u32 iodedupID)
{
    p2d_datum *p2d;
    int rc = 0;
#ifdef SIMREPLAY_DEBUG_SS_DONE
	unsigned long long stime=0, etime=0;

	stime = gettime();	/* START IODEDUP map-updateblk-p2dget time */
#endif
    p2d = (p2d_datum*)p2dmaps_get(p2dmaps, ioblkID);
    if (p2d == NULL && runtimemap)	/* New mapping to be added */
	{
#ifdef SIMREPLAY_DEBUG_SS_DONE
		etime = gettime();	/* END IODEDUP map-updateblk-p2dget time */
		ACCESSTIME_PRINT("iodedmap-map-updateblk-component-p2dget-fail time: %llu\n", etime - stime);

		stime = gettime();	/* START IODEDUP map-updateblk-p2dnew time */
#endif
		p2d = (p2d_datum*)calloc(1, sizeof(p2d_datum)); 
#ifdef SIMREPLAY_DEBUG_SS_DONE
		etime = gettime();	/* END IODEDUP map-updateblk-p2dnew time */
		ACCESSTIME_PRINT("iodedmap-map-updateblk-component-p2dnew time: %llu\n", etime - stime);
#endif
	}
	else if (p2d == NULL)
		RET_ERR("p2dmaps_get(%u) fail in updateBlockio()\n",
                ioblkID);

#ifdef SIMREPLAY_DEBUG_SS_DONE
	stime = gettime();	/* START IODEDUP map-updateblk-notep2d time */
#endif
    rc = note_p2d_map(p2d, iodedupID, lastblk_flag);
#ifdef SIMREPLAY_DEBUG_SS_DONE
	etime = gettime();	/* END IODEDUP map-updateblk-notep2d time */
	ACCESSTIME_PRINT("iodedmap-map-updateblk-component-notep2d time: %llu\n", etime - stime);
#endif
    if (rc)
        RET_ERR("Error in note_p2d_map()\n");

    /* Updating the p2dmaps data structure */
#ifdef SIMREPLAY_DEBUG_SS_DONE
	stime = gettime();	/* START IODEDUP map-updateblk-p2dset time */
#endif
    p2dmaps_set(p2dmaps, ioblkID, (void*)p2d);
#ifdef SIMREPLAY_DEBUG_SS_DONE
	etime = gettime();	/* END IODEDUP map-updateblk-p2dset time */
	ACCESSTIME_PRINT("iodedmap-map-updateblk-component-p2dset time: %llu\n", etime - stime);
#endif

    return 0;
}

/** processBlockio: Processing a block boundary, by creating the p2d map 
 * 					for given vblk.
 *				Used only if known that block ID is coming for first time
 *				to the metadata zone. This is the case during scanning for 
 *				apriori map creation.
 *
 * @param ioblkID[in]:	block ID of the vblk
 * @return status
 */
int processBlockio(__u32 ioblkID,
				int lastblk_flag, __u32 iodedupID) //, unsigned char key[])
{
    p2d_datum *p2d;

    /* Malloc'ed space, to be freed later in resetMappings() */
    p2d = (p2d_datum*)calloc(1, sizeof(p2d_datum));

#ifdef IOMAPPING_TEST
    fprintf(fhashptr, "blkbound=%llu \n", ioblkID);
#endif

	/* Note the elements in the P2D_tuple_t struct */
    if (note_p2d_map(p2d, iodedupID, //key, //ioblkID, volID, 
							lastblk_flag))
        RET_ERR("Error in note_p2d_map()\n");

	/* Add the p2d to the global p2dmaps */
    p2dmaps_set(p2dmaps, ioblkID, (void*)p2d);

#ifdef IOMAPPING_TEST
    fprintf(stdout,"(%llu) ", p2d->ioblkID);
    fprintf(stdout,"===> f(%llu) ", p2d->iodedupID);
#endif

    return 0;
}
