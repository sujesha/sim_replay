#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include "iodeduping.h"
#include "deduptab.h"
#include "d2pv-map.h"
#include "debug.h"
#include "ioserveio.h"
#include "serveio.h"
#include "serveio-utils.h"
#include "utils.h"
#include "vector32.h"
#include "pdd_config.h"
#include "unused.h"

#if defined(IOMAPPING_TEST) || defined(IODEDUPING_TEST)
	extern FILE * fhashptr;
#endif

int dedupmap_alive = 0;		/* dedup mapping space has been created or not */
/* ID 0 indicates zero block/chunk and 
 * ID 1 is DUMMY_ID
 */
dedup_id_t dedupNum = 2;	/* ID of the dedup element (counter variable) */

Node * currReusableDedupIDUList = NULL;	/* list of IDs usable for new elements*/
//pthread_mutex_t currReusableIO_mutex;	/* mutex to access above list */
extern struct deduptab deduptab;		/* hash-table for dedup elements */
extern int disksimflag;

/* This is vector indexed by iodedupID, pointing to dedup-chunks 
 * in the hashtable structure */
vector32 * dedupmap_by_did = NULL;

/** create_dedupmap_mapping_space: Use this to initialize dedupmap 2D vector
 * 		before starting off con_scan_and_process or pdd_freplay threads.
 * 		This space will get freed up in the end, in free_dedupmap().
 */
void create_dedupmap_mapping_space(void)
{
	/* First, check and set the status flag for dedup mapping space creation */
	if (dedupmap_alive == 1)
	{
		fprintf(stdout, "dedupmap already non-NULL\n");
		return;
	}
	dedupmap_alive = 1;
    fprintf(stdout, "In %s\n", __FUNCTION__);
	
	/* Initialize d2pv mapping space */
	dedupmap_by_did = calloc(1, sizeof(vector32));
	if (dedupmap_by_did == NULL)
		VOID_ERR("couldnt malloc dedupmap_by_did\n");
	vector32_init(dedupmap_by_did);
	vector32_resize(dedupmap_by_did, 20000000);

	/* Initialize deduptab */
	if (deduptab_init(&deduptab, BLKTAB_SIZE))
	{
		VOID_ERR("deduptab_init failed\n");
		deduptab_exit(&deduptab);
	}
	return;
}

/** free_dedupmap -- free up the dedup mapping space, earlier created in
 * 					create_dedupmap_mapping_space().
 */
void free_dedupmap(void)
{
    __u32 i;        /* For iterating over iodedupID */
    dedupmap_t *ptri;

	/* First, check and reset status flag for dedup mapping space creation 
	 * This is so that the same space is not freed twice. 
	 */
	if (dedupmap_alive == 0)
	{
		fprintf(stdout, "dedupmap is not alive, exit\n");
		return;
	}
	dedupmap_alive = 0;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	/* Iterating over all entries in the dedup mapping space, free one-by-one */
    for (i = 0; i < vector32_size(dedupmap_by_did); i++)
    {
        if ((ptri = (dedupmap_t *) vector32_get(dedupmap_by_did,i)) != NULL)
		{
            //free(ptri);     /* Freeing dedupmap node done in deduptab_exit */
			int j;
            struct slist_head *p;
            struct D2P_tuple_t *d2p;

			/* A single dedup element (D) can point to multiple blks, i.e. D2P
			 * Get the number of such blk for this dedup element, and
			 * iterate through them, freeing each in turn.
			 */
            int len = slist_len(&ptri->d2pmaps);
            for (j=0; j<len;j++)
            {
                p = slist_first(&ptri->d2pmaps);	/* get (new) first D2P */
                d2p = slist_entry(p, struct D2P_tuple_t, head);
                slist_del(&d2p->head, &ptri->d2pmaps);	/* delete from list */
				free(d2p);								/* free */
            }
#ifdef DEBUG_SS
			/* Once all D2P are deleted from list, d2pmaps should be empty */
            assert(slist_empty(&ptri->d2pmaps));
#endif
			hashtab_remove(deduptab.table, ptri->dhashkey); //d2pv unlinked
			setDedupMap(i, NULL);   //d2pv free
		}
    }
	free(dedupmap_by_did->data);	/* free dedup mapping space 1 */
	free(dedupmap_by_did);	/* free dedup mapping space 2 */

	/* Free deduptab */
	deduptab_exit(&deduptab);		/* free the corresponding hashtable */
	fprintf(stdout, "done now\n");
}	

#if 0
//Removed because we want to check dirty-ness from P2D, not D2PV
/** dedup_dirty -- to check whether the specified dedup mapping entry is dirty,
 * 				i.e. points to data that has been written recently, and has not
 * 				been updated in the mapping yet.
 *
 *	@d2pv[in]: the dedup entry to be checked
 *	@return: TRUE(1) or FALSE (0)
 */
int dedup_dirty(dedupmap_t* d2pv)
{
    if (d2pv->ddirty)
    {
#ifdef CON_DEBUG_SS
        fprintf(stdout, "Dedup is dirty, so fetch original block itself\n");
#endif
        return 1;
    }
    return 0;
}
#endif

/** notzero_dedup: Check whether the dedup specified by d2pv dedupmap
 *      is a zero dedup i.e. mapped by a zero vblk.
 *
 * @d2pv[in]: specified dedupmap
 * @return : TRUE(1) or FALSE (0)
 */
int notzero_dedup(dedupmap_t *d2pv)
{
    /* If the dedup is a zero dedup, it will have iodedupID = 0 */
    if (0 == d2pv->iodedupID)
        return 1;
    else
        return 0;
}

/** getNextDedupNum -- get the next value from the counter variable.
 * 		In scanning phase, iodedupIDs only keep increasing. 
 * 		In the online (IO) phase, use currReusableiodedupIDList[] as well. 
 * 		However, the global dedupNum variable still needs atomic
 * 		increment and fetch, since it is use in the online (IO) phase as well.
 *
 * @initflag[in]: flag to indicate whether this is scanning phase or IO
 * @return: dedup ID to be used for the newly found dedup element
 */
//FIXME: Would the dedupNum counter rollover?
dedup_id_t getNextDedupNum(int initflag)
{
    dedup_id_t f;
	//UNUSED(initflag);

#if 1
    dedup_id_t *dNum, val;
	/* We want to re-use IDs */
    if (initflag == NOINIT_STAGE)   /* online phase */
    {
        //pthread_mutex_lock(&currReusableIO_mutex);
        if (ulistLen(currReusableDedupIDUList))
        {
            dNum = (dedup_id_t*)popUList(&currReusableDedupIDUList);
			val = *dNum;
			free(dNum);		//alloc in popUList()
            return val;
        }
        //pthread_mutex_unlock(&currReusableIO_mutex);
    }
#endif

    /* If you are here, one of the following is true:-
     * 1. empty currReusableDedupIDUList in online phase, or
     * 2. scanning phase
     *
     * Use this to stay atomic
     * uint64_t atomic_inc_64_nv(volatile uint64_t *target);
     * http://www.cognitus.net/html/tutorial/usingVolatile.html
     */
    f = __sync_add_and_fetch(&dedupNum, 1);
    return f;
}

/** note_dedup_attrs -- fill d2pv_datum struct for dedup element.
 * @d2pv[out]: element to be filled
 * @key[in]: hashkey of the dedup element
 * @iodedupID[in]: ID to be assigned to the dedup element
 */
void note_dedup_attrs(d2pv_datum *d2pv,
                unsigned char *key, __u32 iodedupID, __u32 ioblkID)
{
    /* If HASHMAGIC is disabled, MAGIC_SIZE = 0, so wont get copied anyway */
	memcpy(d2pv->dhashkey, key, HASHLEN  + MAGIC_SIZE);

    d2pv->iodedupID = iodedupID;
    d2pv->ioblkID = ioblkID;	//only to maintain counts of self-hits/misses
								//updated each time to reflect the obj->ioblkID
								//of this content object in ARC cache.
}

void note_d2p_tuple(D2P_tuple_t *d2pp, __u32 ioblkID)
{
    d2pp->ioblkID = ioblkID;
}

void add_d2p_tuple_to_map(D2P_tuple_t *d2pt, d2pv_datum *d2pv)
{
    slist_add(&d2pt->head, &d2pv->d2pmaps);
}

void remove_d2p_tuple_from_map(D2P_tuple_t *d2p, d2pv_datum *d2pv)
{
	D2P_tuple_t *f = d2p;
    slist_del(&d2p->head, &d2pv->d2pmaps);
	free(f);
}

void setDedupMap(dedup_id_t iodedupID, d2pv_datum *d2pv)
{
	vector32_set(dedupmap_by_did, iodedupID, (void*)d2pv);
}

/* Retrieve dedup mapping for specified iodedupID */
struct dedupmap_t* getDedupMap(dedup_id_t iodedupID)
{
    /* Using the iodedupID, need to retrieve the dedupmap. For this,
     * we use the dedupmap_by_did vector.
     */
    return (dedupmap_t*) vector32_get(dedupmap_by_did, iodedupID);
}

int get_fulldedup(dedupmap_t* d2pv, unsigned char **buf)
{
	struct D2P_tuple_t *deduped_d2p;
	struct preq_spec preq_local;
	__u32 fetch_ioblk;
#ifdef DEBUG_SS
	assert(*buf == NULL);
	assert(d2pv != NULL);
#endif

	/* This function has not yet been updated to read from simdisk file */
	assert(!disksimflag);

	/* Retrieve the d2p mapping corresponding to the deduplicated 
	 * mapping. This is because while fetching any chunk's content,
	 * we want to repeatedly fetch it from the same (physical) block
	 * s.t. its popularity will increase and probability of its 
	 * presence in cache will increase, hence improving fetch times.
	 */
	deduped_d2p = get_deduped_d2p(d2pv);
	if (!deduped_d2p)
		RET_ERR("No d2p tuple marked as dedup_fetch:1\n");

	fetch_ioblk = deduped_d2p->ioblkID;
    directcreate_preq_spec(fetch_ioblk, BLKSIZE, 1, NULL,
                    0, BLKSIZE-1, &preq_local);

	/* Fetch the pblk (with start & end offsets) data into buf */
	if (fetchdata_pblk(&preq_local))
			RET_ERR("error in fetchdata_pblk\n");

	/* Copy those many bytes from buf into outbuf and free buf */
	*buf = calloc(BLKSIZE, sizeof(unsigned char));
    memcpy(*buf, preq_local.content, BLKSIZE);
	free(preq_local.content);

	return 0;
}

#if 0
/** Processing a dedup boundary.
 *
 * Dedup dedup detection is to be done in confided module 
 * The iodedupID is present in the mapping, and in the virt-to-dedup mapping, we 
 * should point to chunks by iodedupID.
 *
 * @param c 
 * @param leftover_len
 * @param newBoundaryBlkNum
 * @param initflag
 * @return Return chunk num (iodedupID) so it can be added to vblk chunk list
 */
dedup_id_t processDedup(
//                struct dedup_t *f, /* dedup blk to be processed != NULL */
				unsigned char *buf,
				__u16 len,	
                __u32 newBoundaryBlkNum, /* blk in which dedup chunk found */
                __u16 volID,        /* blk belongs to which volume */
                int initflag)  /* online phase or scanning phase */
{
    d2pv_datum *d2pv = NULL;
    struct D2P_tuple_t *d2p;
	dedupmap_t *dedupd2pv;

    unsigned char key[HASHLEN + MAGIC_SIZE];

    assert(f != NULL);
    /* Hash(+magic) the dedup chunk */
    getHashKey(buf, len, key);
#if defined(IOMAPPING_TEST) || defined(IODEDUPING_TEST)
    //WHERE;
    char bufhuman[HASHLEN_STR];
    MD5Human(key, bufhuman);
#endif

    /* Note the C2F tuple for this dedup chunk */
    d2p = (D2P_tuple_t*) malloc (sizeof(D2P_tuple_t));
    note_d2p_tuple(d2p, volID, newBoundaryBlkNum);

    /* trying to identify whether found dedup-chunk is dedup or not  */
    dedupd2pv = (d2pv_datum*) hashtab_search(deduptab.table, key);
    if (!dedupd2pv) /* not found in deduptab => new dedup-chunk */
    {
        d2pv = (d2pv_datum*) malloc (sizeof(d2pv_datum));
		INIT_LIST_HEAD(&d2pv->d2pmaps);
        note_dedup_attrs(d2pv, key, getNextDedupNum(initflag));
		d2p->dedupfetch = 1;
        add_d2p_tuple_to_map(d2p, d2pv);
        hashtab_insert(deduptab.table, d2pv->fhashkey, d2pv); //add to hashtab
#if defined(IOMAPPING_TEST) || defined(IODEDUPING_TEST)
		assert(memcmp(key, d2pv->fhashkey, HASHLEN + MAGIC_SIZE) == 0);
		fprintf(fhashptr, "iodedupID = %llu ", d2pv->iodedupID);
#endif
    }
    else    /* New d2p mapping for existing dedup */
    {
		d2p->dedupfetch = 0;
        add_d2p_tuple_to_map(d2p, dedupd2pv);
#if defined(IOMAPPING_TEST) || defined(IODEDUPING_TEST)
		fprintf(fhashptr, "dedupiodedupID = %llu ", dedupd2pv->iodedupID);
#endif
    }

#ifdef IOMAPPING_TEST
	int num_d2p;
	dedupmap_t *cp;
	D2P_tuple_t *ct;

	if (dedupd2pv)
		cp = dedupd2pv;
	else
		cp = d2pv;

    fprintf(fhashptr,
            "%s %u %x%x%x%x startblk=%llu\n",
            bufhuman, len,
        buf[len-4], buf[len-3], buf[len-2],
        buf[len-1],
        cp->iodedupID);

	fprintf(stdout, "%s", bufhuman);
	fprintf(stdout, " %llu", cp->iodedupID);
	//fprintf(stdout, " %d", cp->ddirty);

	num_d2p = slist_len(&cp->d2pmaps);
	fprintf(stdout, " num_d2p=%d ", num_d2p);

	if (num_d2p > 0)
	{
		struct slist_head *p;
		fprintf(stdout, "[ ");
		__slist_for_each(p, &cp->d2pmaps)
		{
    		ct = slist_entry(p, struct D2P_tuple_t, head);
		    fprintf(stdout, "(%d)", ct->dedupfetch);
		    fprintf(stdout, " %u", ct->volID);
		    fprintf(stdout, " %llu:", ct->ioblkID);
		}
		fprintf(stdout, "]");
	}
	fprintf(stdout, "\n");
#endif

    if (dedupd2pv)
        return dedupd2pv->iodedupID;
    else
    {
        setDedupMap(d2pv->iodedupID, d2pv);
        return d2pv->iodedupID;
    }
}
#endif
