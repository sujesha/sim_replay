/* This file stores the virtual-to-fixed mapping in vectors.
 */

#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <asm/types.h>
#include <string.h>
#include "vector16.h"
#include "vector32.h"
#include "utils.h"
#include "v2f-map.h"
#include "debug.h"
#include "slist.h"				/* slist_head */
#include "pdd_config.h"
#include "fixing.h"
#include "ulist.h"

#ifdef CONMAPPING_TEST
	extern FILE * fhashptr;
#endif


int v2fmaps_alive = 0;
extern char *V2PmapFile;		/* input V2P mapping file */
extern int runtimemap;

//char zeroblkhash[10];
vector16 * v2fmaps = NULL;

/** create_v2f_mapping_space: Use this to initialize the v2fmaps 2D vector
 * 		before starting off con_scan_and_process or pdd_freplay threads.
 */
void create_v2f_mapping_space()
{
	if (v2fmaps_alive == 1)
	{
        fprintf(stdout, "v2fmaps already non-NULL\n");
        return;
    }
	v2fmaps_alive = 1;
#ifdef PDDREPLAY_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	v2fmaps = calloc(1, sizeof(vector16));
	vector16_init(v2fmaps);

	v2fmaps_resize(v2fmaps, 0, 54642298);

	return;
}

/** v2fmaps_free: Use this to free the entire v2fmaps mapping. This should
 * 				be done before exiting the program at the very end.
 */
void free_v2fmaps(void)
{
	__u16 i;		/* For iterating over volID */
	__u32 j;		/* For iterating over vblkID */
	vector32 *ptri;
	v2f_datum *ptrj;
#if defined(PDDREPLAY_DEBUG_SS) || defined(SIM_REPLAY)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (v2fmaps_alive == 0)
	{
		fprintf(stdout, "v2fmaps being freed already\n");
		return;
	}
	v2fmaps_alive = 0;

	for (i = 0; i < vector16_size(v2fmaps); i++)	/* Iterating over volID */
    {
        if ((ptri = vector16_get(v2fmaps, i)) != NULL)	
        {
#if 0
						v2f_datum* v2f32791714 = (v2f_datum*)vector32_get(ptri, 32791714);
						if (v2f32791714 != NULL)
						{
							v2f_datum* v2fcopy = malloc(sizeof(v2f_datum));
							memcpy(v2fcopy, v2f32791714, sizeof(v2f_datum));
							fprintf(stdout, "%s: string version = %s\n", 
								__FUNCTION__, (char *)(v2f32791714));
							free(v2f32791714);
						}
#else
            for (j = 0; j < vector32_size(ptri); j++)/* Iterating over vblkID */
            {
                if ((ptrj = (v2f_datum*) vector32_get(ptri, j)) != NULL)
				{
					if (j==32791714)
					{
					fprintf(stdout, "sizeof(v2f_datum*)=%ld\n",sizeof(v2f_datum*));
					fprintf(stdout, "%s:freeing index=%u, address=%p\n", __FUNCTION__, j, ptrj);
					}
					free(ptrj);		/* Freeing the vm_info node */
					if (j==32791714)
					fprintf(stdout, "%s:freed index=%u\n", __FUNCTION__, j);
#if 0
					if (j != 32791714)
					{
						/* Checking whether above free() affected 32791714 */
						v2f_datum* v2f32791714 = (v2f_datum*)vector32_get(ptri, 32791714);
						if (v2f32791714 != NULL)
						{
							v2f_datum* v2fcopy = malloc(sizeof(v2f_datum));
							memcpy(v2fcopy, v2f32791714, sizeof(v2f_datum));
							fprintf(stdout, "%s: string version = %s\n", 
								__FUNCTION__, (char *)(v2f32791714));
						}
					}
#endif
				}
			}
#endif
			free(ptri);		/* Freeing the vector per volume */
		}
	}
	free(v2fmaps->data);
	free(v2fmaps);
	fprintf(stdout, "done now\n");
}

/* To resize per VM or volume */
void v2fmaps_resize(vector16 *v2fmaps, __u16 x, __u32 size)
{
    vector32 *ptr;		/* Holds single volume row */
    ptr = vector16_get(v2fmaps, x);
    if (ptr == NULL)
	{
        ptr = calloc(1, sizeof(vector32));
        vector32_init(ptr);
        vector16_set(v2fmaps, x, ptr);  /* Add newly malloc'ed to 2D vector */
	}

	vector32_resize(ptr, size);
}

/** v2fmaps_set: This function sets a v2f_datum at location(x,y) 
 * 	within 2D vector v2fmaps. Use this in processBlockf() and updateBlockf().
 */
void v2fmaps_set(vector16 *v2fmaps, __u16 x, __u32 y, void *e)
{
    vector32 *ptr;		/* Holds single volume row */
	//v2f_datum *tempv2f;

    ptr = vector16_get(v2fmaps, x);

	//TODO:Lock volume row, if multithreaded.
    if (ptr == NULL)
    {
        /* Encountered first blk of this VM, so create VM's vector */
        ptr = calloc(1, sizeof(vector32));
        vector32_init(ptr);
        vector16_set(v2fmaps, x, ptr);  /* Add newly malloc'ed to 2D vector */
    }

#if 0
	tempv2f = (v2f_datum*)vector32_get(ptr, y);
	if (tempv2f)
	{
		if (y==32791714)
			fprintf(stdout, "%s: freeing after vector32_get for index=%u, "
							"address=%p\n",__FUNCTION__, y, tempv2f);
		free(tempv2f);
	}
#endif

#if 0
	if (y != 32791714)
	{
		/* Checking whether above free() affected 32791714 entry */
		v2f_datum* v2f32791714 = (v2f_datum*)vector32_get(ptr, 32791714);
		if (v2f32791714 != NULL)
		{
			v2f_datum* v2fcopy = malloc(sizeof(v2f_datum));
			memcpy(v2fcopy, v2f32791714, sizeof(v2f_datum));
			fprintf(stdout, "%s: string version = %s\n", __FUNCTION__, 
						(char *)(v2f32791714));
		}
//		else
//			fprintf(stdout, "%s: v2f32791714 is NULL\n", __FUNCTION__);
	}
	else
	{
		
	}
#endif

	if (y==32791714)
		fprintf(stdout, "%s: invoking vector32_set for index=%u\n",
			__FUNCTION__, y);
    vector32_set(ptr, y, e);
	//TODO:Unlock volume row, if multithreaded.

#if 0
	if (y == 32791714)
	{
		v2f_datum* v2fcopy = malloc(sizeof(v2f_datum));
		v2f_datum* v2f32791714 = (v2f_datum*)vector32_get(ptr, 32791714);
		assert(v2f32791714 != NULL);
		memcpy(v2fcopy, v2f32791714, sizeof(v2f_datum));
	}
#endif

    return;
}

/* v2fmaps_get: This function retrieves a v2f_datum from location (x,y)
 * within 2D vector v2fmaps. Use this function in updateBlockf()
 * and getVirttoFixedMap().
 */
void *v2fmaps_get(vector16 *v2fmaps, __u16 x, __u32 y)
{
    vector32 *ptr;
	void *e;

    if (x >= v2fmaps->size)
    {
		if (!runtimemap)
	        fprintf(stderr,"v2fmaps_get:index %u>=v2fmaps->size %u\n",
						x,v2fmaps->size);
        return NULL;
    }

    ptr = vector16_get(v2fmaps, x);
	//Lock volume row
    if (ptr == NULL)
    {
		if (!runtimemap)
        	fprintf(stderr, "v2fmaps_get:VM doesnt exist at index %u\n", x);
        return NULL;
    }
	e = (v2f_datum*) vector32_get(ptr, y);
	//Unlock volume row

	if (e==NULL && !runtimemap)
       	fprintf(stderr, "v2fmaps_get:VM doesnt have block at index %u\n", y);

    return e;
}

#if 0
/* 
 * Hash for zero block is to be stored as all zeroes (FIXME change this 
 * all zeroes hash value if it is not okay).
 */
static void note_bhashkey_zerovblkf(v2f_datum *v)
{
    memcpy(v->bhashkey, zeroblkhash, HASHLEN + MAGIC_SIZE);
}
#endif

int notzeroF_vblk(v2f_datum *v2f)
{
#ifdef REPLAYDIRECT_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(v2f != 0);
#endif

	if (v2f->fixedID != 0)
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
/* Is invoked from note_v2f_map() */
void note_block_attrsf(v2f_datum *v2f, unsigned char *key,
            __u32 blockID, __u16 volID, int lastblk_flag)
{
    v2f->blockID = blockID; //FIXME: is this redundant?
    v2f->volID = volID; //FIXME: is this redundant?

    if (lastblk_flag == ZEROBLK_FLAG)
    {
        note_bhashkey_zerovblkf(v2f);
    }
	else
	{
    	/* If HASHMAGIC is disabled, MAGIC_SIZE = 0, so wont get copied anyway */
    	memcpy(v2f->bhashkey, key, HASHLEN + MAGIC_SIZE);
	}
	return;
}
#endif

/* Handles both zero fixedID and non-zero fixedIDs 
 * Is invoked from processBlockf() and updateBlockf().
 */
int note_v2f_map(v2f_datum *v2f, __u32 fixedID,
        //unsigned char *key, __u32 blockID, __u16 volID, 
		int lastblk_flag)
{
#ifdef DEBUG_SS
	assert(v2f != NULL);
#endif
    //note_block_attrsf(v2f, key, blockID, volID, lastblk_flag);

    if (lastblk_flag == ZEROBLK_FLAG)
        v2f->fixedID = 0;
	else
		v2f->fixedID = fixedID;
	v2f->fdirty = 0;	/* Reset the dirty flag */
    return 0;
}

/* fixed_dirty --- shows that this virt blk in CONFIDED has been dirtied
 * 			recently and the metadata is yet to be updated.
 */
int fixed_dirty(v2f_datum* v2f)
{
    if (v2f->fdirty)
    {
        return 1;
    }
    return 0;
}

/* Look up V2F mapping for given volume (VM) and vblkID, and return 
 * linked list (v2flistp) of mapping of "count" number of items.
 * @param[in] volID
 * @param[in] vBlkID
 * @param[in] count
 * @return list of v2f maps or NULL if ALL count # of vblks are zeroblks
 */
int getVirttoFixedMap(__u16 volID, __u32 vBlkID, __u16 count, 
				struct slist_head *v2flistp)
{
    int success = 0;
    __u16 i;
    v2f_datum *v2f;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

    for (i = 0; i < count; i++)
    {
		int ret;
        v2f = (v2f_datum*) v2fmaps_get(v2fmaps, volID, vBlkID + i);
        /* A zero vblk will still return v2f map containing fixedID = 0
         * So, check to see whether we have hit a non-zero block yet.
         * If so, success = 1, else not.
         */
		if (v2f == NULL)
		{
			if (!runtimemap)
			{
				RET_ERR("vblkID %u beyond the capacity of VMID %u, check %s\n",
					vBlkID + i, volID, V2PmapFile);
			}
			else
				return -1;
		}
        ret = notzeroF_vblk(v2f);
		if (ret == 1)	/* either non-zero blk or dirty blk */
            success = 1;
#if 0
		else if (ret == -1)
			RET_ERR("vblkID (%u, %u) has no fixedID?? Fix this.\n",
				vBlkID + i, volID);
#endif

        /* Add the v2f to v2flistp */
        slist_add(&v2f->head, v2flistp);
    }

    /*  If ALL count # of vblks are zeroblks, i.e. we didn't any non-zero
     *  block, free up v2flistp list and return NULL.
     */
    if (success == 0)
    {
#ifdef PDDREPLAY_DEBUG_SS_DONE
        fprintf(stderr, "Only zero blocks encountered, no GOODBLK\n");
#endif
		return ZEROBLK_FLAG;
    }

    /*  Else retain their v2f maps as-is within the list
     *  and return the list.
     */
    return 0;
}

#ifndef CONMAPPING_TEST
#ifndef CONDUMPING_TEST

/* Updating a block boundary, by modifying the v2f map for given vblk
 * Assuming that write never writes a zero block. If this is not correct, 
 * fix the zero block handling here. //FIXME
 * Is also used for reads with runtime mapping
 *
 * @param buf
 * @param blockID
 * @param volID
 * @param lastblk_flag
 * @return status
 */
int updateBlockf(__u32 blockID, __u16 volID,
                int lastblk_flag, __u32 fixedID) //, unsigned char key[])
{
    v2f_datum *v2f;
    int rc = 0;

    v2f = (v2f_datum*)v2fmaps_get(v2fmaps, volID, blockID);
    if (v2f == NULL && runtimemap)	/* New mapping to be added */
		v2f = (v2f_datum*)calloc(1, sizeof(v2f_datum));
    else if (v2f == NULL)
        RET_ERR("v2fmaps_get(%u, %u) fail in updateBlockf()\n", 
				volID, blockID);

    rc = note_v2f_map(v2f, fixedID, //key, //blockID, volID, 
					lastblk_flag);
    if (rc)
        RET_ERR("Error in note_v2f_map()\n");

    /* Updating the v2fmaps data structure */
    v2fmaps_set(v2fmaps, volID, blockID, (void*)v2f);

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "After v2fmaps_set, ");
    fprintf(stdout,"(%u, ", volID);
    fprintf(stdout,"%u) ", blockID);
    fprintf(stdout,"===> f(%u) \n", v2f->fixedID);
#endif
    return 0;
}
#endif	//ifndef CONDUMPING_TEST
#endif	//ifndef CONMAPPING_TEST

/** processBlock: Processing a block boundary, by creating the v2f map 
 * 					for given vblk.
 *
 * @param blockID[in]:	block ID of the vblk
 * @param volID[in]: volID of this vblk
 * @return status
 */
int processBlockf(__u32 blockID, __u16 volID,
				int lastblk_flag, __u32 fixedID) //, unsigned char key[])
{
    v2f_datum *v2f;

    /* Malloc'ed space, to be freed later in resetMappings() */
    v2f = (v2f_datum*)calloc(1, sizeof(v2f_datum));

#if defined(CONMAPPING_TEST)
    fprintf(fhashptr, "blkbound=%llu \n", blockID);
#endif

	/* Note the elements in the V2F_tuple_t struct */
    if (note_v2f_map(v2f, fixedID, //key, //blockID, volID, 
							lastblk_flag))
        RET_ERR("Error in note_v2f_map()\n");

	/* Add the v2f to the global v2fmaps */
    v2fmaps_set(v2fmaps, volID, blockID, (void*)v2f);

#if defined(CONMAPPING_TEST) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout,"(%u, ", volID);
    fprintf(stdout,"%u) ", blockID);
    fprintf(stdout,"===> f(%u) ", v2f->fixedID);
#endif

    return 0;
}
