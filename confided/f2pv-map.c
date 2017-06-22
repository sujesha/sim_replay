#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include "fixing.h"
#include "fixedtab.h"
#include "f2pv-map.h"
#include "debug.h"
#include "fserveio.h"
#include "serveio-utils.h"
#include "utils.h"
#include "vector32.h"
#include "pdd_config.h"
#include "unused.h"
#include "content-simfile.h"
#include "v2p-map.h"
#include "replay-defines.h"
#include "simdisk-API.h"

#if defined(CONMAPPING_TEST) || defined(CONCHUNKING_TEST)
	extern FILE * fhashptr;
#endif

int fixedmap_alive = 0;

/* ID 0 indicates zero block/chunk and 
 * ID 1 is DUMMY_ID
 */
fixed_id_t fixedNum = 2;

Node * currReusableFixedIDUList = NULL;
//pthread_mutex_t currReusableF_mutex;
extern struct fixedtab fixedtab;
extern int disksimflag;	/* read req have content */

/* This is vector indexed by fixedID, pointing to fixed-chunks 
 * in the hashtable structure */
vector32 * fixedmap_by_fid = NULL;

/* create_fixedmap_mapping_space: Use this to initialize fixedmap 2D vector
 * before starting off con_scan_and_process or pdd_freplay threads
 */
void create_fixedmap_mapping_space(void)
{
#ifdef PDDREPLAY_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	if (fixedmap_alive == 1)
	{
		fprintf(stdout, "fixedmap already non-NULL\n");
		return;
	}
	fixedmap_alive = 1;
	
	/* Initialize f2pv mapping space */
	fixedmap_by_fid = calloc(1, sizeof(vector32));
	if (fixedmap_by_fid == NULL)
		VOID_ERR("couldnt malloc fixedmap_by_fid\n");
	vector32_init(fixedmap_by_fid);
	vector32_resize(fixedmap_by_fid, 20000000);

	/* Initialize fixedtab */
	if (fixedtab_init(&fixedtab, BLKTAB_SIZE))
	{
		VOID_ERR("fixedtab_init failed\n");
		fixedtab_exit(&fixedtab);
	}
	return;
}

void free_fixedmap(void)
{
    __u32 i;        /* For iterating over fixedID */
    fixedmap_t *ptri;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	if (fixedmap_alive == 0)
	{
		fprintf(stdout, "fixedmap is not alive, exit\n");
		return;
	}
	fixedmap_alive = 0;

    for (i = 0; i < vector32_size(fixedmap_by_fid); i++)
    {
        if ((ptri = (fixedmap_t *) vector32_get(fixedmap_by_fid,i)) != NULL)
		{
            //free(ptri);     /* Freeing fixedmap node done in fixedtab_exit */
			int j;
            struct slist_head *p;
            struct F2V_tuple_t *f2v;
            int len = slist_len(&ptri->f2vmaps);
            for (j=0; j<len;j++)
            {
                p = slist_first(&ptri->f2vmaps);
                f2v = slist_entry(p, struct F2V_tuple_t, head);
                slist_del(&f2v->head, &ptri->f2vmaps);
				free(f2v);
            }
#ifdef DEBUG_SS
            assert(slist_empty(&ptri->f2vmaps));
#endif
		}
    }
	free(fixedmap_by_fid->data);
	free(fixedmap_by_fid);

	/* Free fixedtab */
	fixedtab_exit(&fixedtab);
	fprintf(stdout, "done now\n");
}	

/* notzero_fixed: Check whether the fixed specified by f2pv fixedmap
 *      is a zero fixed i.e. mapped by a zero vblk.
 * @f2pv[in]: specified fixedmap
 * @return : TRUE(1) or FALSE (0)
 */
int notzero_fixed(fixedmap_t *f2pv)
{
    /* If the fixed is a zero fixed, it will have fixedID = 0 */
    if (0 == f2pv->fixedID)
        return 1;
    else
        return 0;
}

/* In scanning phase, fixedIDs only keep increasing. 
 * In the online (IO) phase, need to use currReusablefixedIDList[] as well. 
 * However, the global fixedNum variable still needs atomic
 * increment and fetch, since it is use in the online (IO) phase as well.
 */
//FIXME: Would the fixedNum counter rollover?
fixed_id_t getNextFixedNum(int initflag)
{
	fixed_id_t f;
	//UNUSED(initflag);

#if 1
    fixed_id_t *fNum, val;

	/* We want to re-use IDs in real system, but not here in simulation */
    if (initflag == NOINIT_STAGE)   /* online phase */
    {
        //pthread_mutex_lock(&currReusableF_mutex);
        if (ulistLen(currReusableFixedIDUList))
        {
            fNum = (fixed_id_t*)popUList(&currReusableFixedIDUList);
            val = *fNum;
			free(fNum);		//alloc in popUList
			return val;
        }
        //pthread_mutex_unlock(&currReusableF_mutex);
    }
#endif

    /* If you are here, one of the following is true:-
     * 1. empty currReusableFixedIDUList in online phase, or
     * 2. scanning phase
     *
     * Use this to stay atomic
     * uint64_t atomic_inc_64_nv(volatile uint64_t *target);
     * http://www.cognitus.net/html/tutorial/usingVolatile.html
     */
    f = __sync_add_and_fetch(&fixedNum, 1);
    return f;
}

void note_fixed_attrs(f2pv_datum *f2pv,
                unsigned char *key, __u32 fixedID)
{
    /* If HASHMAGIC is disabled, MAGIC_SIZE = 0, so wont get copied anyway */
    memcpy(f2pv->fhashkey, key, HASHLEN);

    f2pv->fixedID = fixedID;
}

void note_f2v_tuple(F2V_tuple_t *f2vp, __u16 volID, __u32 blockID)
{
#ifdef SIMREPLAY_DEBUG_SS_DONE
    fprintf(stdout, "In %s, volID=%u, blockID=%u\n", __FUNCTION__, 
			volID, blockID);
#endif
    f2vp->volID = volID;
    f2vp->blockID = blockID;
}

void add_f2v_tuple_to_map(F2V_tuple_t *f2vt, f2pv_datum *f2pv)
{
    slist_add(&f2vt->head, &f2pv->f2vmaps);
}

void remove_f2v_tuple_from_map(F2V_tuple_t *f2v, f2pv_datum *f2pv)
{
	F2V_tuple_t *f = f2v;
    slist_del(&f2v->head, &f2pv->f2vmaps);
	free(f);
}

void setFixedMap(fixed_id_t fixedID, f2pv_datum *f2pv)
{
//	fprintf(stdout, "%s: invoking vector32_set for fixedID=%u\n",
//			__FUNCTION__, fixedID);
	vector32_set(fixedmap_by_fid, fixedID, (void*)f2pv);
}

/* Retrieve fixed mapping for specified fixedID */
struct fixedmap_t* getFixedMap(fixed_id_t fixedID)
{
    /* Using the fixedID, need to retrieve the fixedmap. For this,
     * we use the fixedmap_by_fid vector.
     */
    return (fixedmap_t*) vector32_get(fixedmap_by_fid, fixedID);
}

int get_fullfixed(fixedmap_t* f2pv, unsigned char **buf, __u8 *content)
{
	struct F2V_tuple_t *deduped_f2v;
	struct preq_spec preq_local;
	__u32 fetch_vblk;
#ifdef DEBUG_SS
	assert(*buf == NULL);
	assert(f2pv != NULL);
#endif
	if (disksimflag)
		assert(content != NULL);

	/* Retrieve the c2v mapping corresponding to the deduplicated 
	 * mapping. This is because while fetching any chunk's content,
	 * we want to repeatedly fetch it from the same (physical) block
	 * s.t. its popularity will increase and probability of its 
	 * presence in cache will increase, hence improving fetch times.
	 */
	deduped_f2v = get_deduped_f2v(f2pv);
	if (!deduped_f2v)
		RET_ERR("No f2v tuple marked as dedup_fetch:1\n");

	fetch_vblk = deduped_f2v->blockID;
#ifdef SIMREPLAY_DEBUG_SS
	if (fetch_vblk == 10 || fetch_vblk == 33414267 ||
			fetch_vblk == 34600770 || fetch_vblk == 10100928)
		fprintf(stdout, "%s:here fetch_vblk=%u, fixedID=%u\n", __FUNCTION__, 
			fetch_vblk, f2pv->fixedID);
#endif

	if (disksimflag)
	{
		__u8 *simcontent = NULL;
		if (runtimemap)
		{
			if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
			{
				simcontent = malloc(BLKSIZE);
#ifdef SIMREPLAY_DEBUG_SS
				if (f2pv->fixedID == 3750409)
					printf("%s: fixedID=%u, fetch_vblk = %u\n", 
							__FUNCTION__, f2pv->fixedID, fetch_vblk);
#endif
				disk_read_trap(deduped_f2v->volID, fetch_vblk, simcontent, 
					BLKSIZE);
			}
			else
			{
				simcontent = malloc(MD5HASHLEN_STR-1);
				disk_read_trap(deduped_f2v->volID, fetch_vblk, simcontent, 
					MD5HASHLEN_STR-1);
			}
		}
#if 0
		/* For both collectformat & not, using simdisk_trap & then
		 * get_simcontent() internally for collectformat. If want to get
		 * BLKSIZE content from get_simcontent, send generate flag = 1, else 0
		 */
		else
		{
			__u32 ioblk;
			if (getVirttoPhysMap(deduped_f2v->volID, fetch_vblk, &ioblk))
				VOID_ERR("getVirttoPhysMap error'ed\n");
			get_simcontent(ioblk, simcontent, 1);
		}
#endif
	    create_preq_spec(deduped_f2v->volID, fetch_vblk, 
					BLKSIZE, 1, simcontent,
                    0, BLKSIZE-1, &preq_local);
		free(simcontent);
	}		
	else
	{
	    create_preq_spec(deduped_f2v->volID, fetch_vblk, 
					BLKSIZE, 1, content,
                    0, BLKSIZE-1, &preq_local);

		/* Fetch the pblk (with start & end offsets) data into buf */
		if (fetchdata_pblk(&preq_local))
			RET_ERR("error in fetchdata_pblk\n");
	}

	/* Copy those many bytes from buf into outbuf and free local buf */
	if (collectformat)
	{
		*buf = calloc(MD5HASHLEN_STR-1, sizeof(unsigned char));
    	memcpy(*buf, preq_local.content, MD5HASHLEN_STR-1);
		free(preq_local.content);
	}
	else
	{	
		*buf = calloc(BLKSIZE, sizeof(unsigned char));
    	memcpy(*buf, preq_local.content, BLKSIZE);
		free(preq_local.content);
	}

	if ((content!=NULL && !collectformat && memcmp(content, *buf, BLKSIZE)!=0)||
 (content!=NULL && collectformat && memcmp(content, *buf, MD5HASHLEN_STR-1)!=0))
	{
		RET_ERR("%s: content mismatch fetch_vblk=%u, fixedID=%u\n", 
			__FUNCTION__, fetch_vblk, f2pv->fixedID);
		//get_deduped_f2v(f2pv);
	}
	return 0;
}

#if 0
/** Processing a fixed boundary.
 *
 * Fixed dedup detection is to be done in confided module 
 * The fixedID is present in the mapping, and in the virt-to-fixed mapping, we 
 * should point to chunks by fixedID.
 *
 * @param c 
 * @param leftover_len
 * @param newBoundaryBlkNum
 * @param initflag
 * @return Return chunk num (fixedID) so it can be added to vblk chunk list
 */
fixed_id_t processFixed(
//                struct fixed_t *f, /* fixed blk to be processed != NULL */
				unsigned char *buf,
				__u16 len,	
                __u32 newBoundaryBlkNum, /* blk in which fixed chunk found */
                __u16 volID,        /* blk belongs to which volume */
                int initflag)  /* online phase or scanning phase */
{
    f2pv_datum *f2pv = NULL;
    struct F2V_tuple_t *f2v;
	fixedmap_t *dedupf2pv;

    unsigned char key[HASHLEN + MAGIC_SIZE];

    assert(f != NULL);
    /* Hash(+magic) the fixed chunk */
    getHashKey(buf, len, key);
#if defined(CONMAPPING_TEST) || defined(CONCHUNKING_TEST)
    //WHERE;
    char bufhuman[HASHLEN_STR];
    MD5Human(key, bufhuman);
#endif

    /* Note the C2F tuple for this fixed chunk */
    f2v = (F2V_tuple_t*) malloc (sizeof(F2V_tuple_t));
    note_f2v_tuple(f2v, volID, newBoundaryBlkNum);

    /* trying to identify whether found fixed-chunk is dedup or not  */
    dedupf2pv = (f2pv_datum*) hashtab_search(fixedtab.table, key);
    if (!dedupf2pv) /* not found in fixedtab => new fixed-chunk */
    {
        f2pv = (f2pv_datum*) malloc (sizeof(f2pv_datum));
		INIT_LIST_HEAD(&f2pv->f2vmaps);
        note_fixed_attrs(f2pv, key, getNextFixedNum(initflag));
		f2v->dedupfetch = 1;
        add_f2v_tuple_to_map(f2v, f2pv);
        hashtab_insert(fixedtab.table, f2pv->fhashkey, f2pv); //add to hashtab
#if defined(CONMAPPING_TEST) || defined(CONCHUNKING_TEST)
		assert(memcmp(key, f2pv->fhashkey, HASHLEN + MAGIC_SIZE) == 0);
		fprintf(fhashptr, "fixedID = %llu ", f2pv->fixedID);
#endif
    }
    else    /* New f2v mapping for existing fixed */
    {
		f2v->dedupfetch = 0;
        add_f2v_tuple_to_map(f2v, dedupf2pv);
#if defined(CONMAPPING_TEST) || defined(CONCHUNKING_TEST)
		fprintf(fhashptr, "dedupfixedID = %llu ", dedupf2pv->fixedID);
#endif
    }

#ifdef CONMAPPING_TEST
	int num_f2v;
	fixedmap_t *cp;
	F2V_tuple_t *ct;

	if (dedupf2pv)
		cp = dedupf2pv;
	else
		cp = f2pv;

    fprintf(fhashptr,
            "%s %u %x%x%x%x startblk=%llu\n",
            bufhuman, len,
        buf[len-4], buf[len-3], buf[len-2],
        buf[len-1],
        cp->fixedID);

	fprintf(stdout, "%s", bufhuman);
	fprintf(stdout, " %llu", cp->fixedID);
//	fprintf(stdout, " %d", cp->fdirty);

	num_f2v = slist_len(&cp->f2vmaps);
	fprintf(stdout, " num_f2v=%d ", num_f2v);

	if (num_f2v > 0)
	{
		struct slist_head *p;
		fprintf(stdout, "[ ");
		__slist_for_each(p, &cp->f2vmaps)
		{
    		ct = slist_entry(p, struct F2V_tuple_t, head);
		    fprintf(stdout, "(%d)", ct->dedupfetch);
		    fprintf(stdout, " %u", ct->volID);
		    fprintf(stdout, " %llu:", ct->blockID);
		}
		fprintf(stdout, "]");
	}
	fprintf(stdout, "\n");
#endif

    if (dedupf2pv)
        return dedupf2pv->fixedID;
    else
    {
        setFixedMap(f2pv->fixedID, f2pv);
        return f2pv->fixedID;
    }
}
#endif

