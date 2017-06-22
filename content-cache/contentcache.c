#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <assert.h>
#include "debugg.h"
#include "md5.h"
#include "utils.h"
#include "arc.h"
#include "pdd_config.h"
#include "contentcache.h"
#include "debug.h"
#include "unused.h"
#include "replay-defines.h"
#include "contentdedup-API.h"

/* Globals */
int CCACHEsize_MB = 100;	/* Default content-cache size is 100MB - Mar 4 */
__u32 MAX_CCACHE_SIZE = 0;		/* Value is set via contentcache_init */
__u32 CCACHE_HASHTAB_COUNT = 0;	/* Value is set via contentcache_init */
struct __arc *ccache = NULL;
__u64 ccache_hits = 0;
__u64 ccache_hits_r = 0;
__u64 ccache_hits_w = 0;
__u64 ccache_misses = 0;
__u64 ccache_misses_r = 0;
__u64 ccache_misses_w = 0;
__u64 cmap_hits = 0;
__u64 cmap_misses = 0;
__u64 cmap_dirties = 0;
__u64 ccache_dedup_hits = 0;
__u64 ccache_nondedup_hits = 0;
__u64 ccache_dedup_misses = 0;
__u64 ccache_nondedup_misses = 0;
__u64 cmap_self_is_leader = 0;
__u64 cmap_self_is_not_leader = 0;

extern int warmupflag;
extern int disksimflag;
extern int collectformat;
extern const char zeroarray[65537];
extern const char zerohash[HASHLEN_STR-1];

#if 0
/* This is the object we're managing. It has a name (md5/sha1)
 * and some data. This data will be loaded when ARC instruct
 * us to do so. */
struct object {
	unsigned char dhashkey[HASHLEN];
    struct __arc_object entry;

#if 1
	__u32 ioblkID;	//to count dedup hits in content-cache only
#endif
    void *content;
};
#endif

unsigned char objname(struct __arc_object *entry)
{
    struct object *obj = __arc_list_entry(entry, struct object, entry);
    return obj->dhashkey[0];
}

/**
* Here are the operations implemented
*/
#if 0
static __u32 __cop_hash(const void *key)
{
	/* If key is content, len=BLKSIZE, else if key=MD5, len=HASHLEN */
	const unsigned char *p, *keyp;   
    unsigned int size;
    unsigned int val;

    if (!key)
    {
        RET_ERR("NULL key in __cop_hash().\n");
    }

    val = 0;
    keyp = (unsigned char *)key;
    //size = strlen(keyp);
    size = HASHLEN;
    for (p = keyp; (unsigned int)(p - keyp) < size; p++)
            val = (val << 4 | (val >> (8*sizeof(unsigned int)-4))) ^ (*p);
    return val;		//& (h->size - 1)
}
#else
static __u32 __cop_hash(const void *key)
{
    const unsigned char *dhashkey = key;
    return dhashkey[0];
}
#endif


static int __cop_compare(struct __arc_object *e, const void *key)
{
	struct object *obj = __arc_list_entry(e, struct object, entry);

#if defined(SIMREPLAY_DEBUG_SS_DONE_DONE)
	assert(key != NULL);
	assert(obj != NULL);
	assert(obj->dhashkey != NULL);
#endif

    return memcmp(obj->dhashkey, key, HASHLEN);
}

#if 1
static struct __arc_object *__cop_create(const void *dhashkey, 
				const void *content, unsigned int datalen, __u32 ioblk)
#else
static struct __arc_object *__cop_create(const void *dhashkey, 
				const void *content, unsigned int datalen)
#endif
{
	/* Create object datastructure */
    struct object *obj = malloc(sizeof(struct object));
	if (obj == NULL)
	{
		fprintf(stdout, "malloc failed for obj in __cop_create()\n");
		return NULL;
	}
#if defined(SIMREPLAY_DEBUG_SS_DONE_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	/* Allocate memory to hold content and copy content over */
	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
	{
		/* Null char not to be copied as content */
		obj->content = malloc(MD5HASHLEN_STR-1);
	    memcpy(obj->content, content, MD5HASHLEN_STR-1);
	}
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
		obj->content = malloc(datalen);
	    memcpy(obj->content, content, datalen);
	}
#if 1
	obj->ioblkID = ioblk;
#endif

    memcpy(obj->dhashkey, dhashkey, HASHLEN);
#ifdef CONTENTDEDUP
	if (cache_insert_trap(obj->dhashkey))
	{
		VOID_ERR("cache_insert_trap err'ed\n");
	}
#endif	//CONTENTDEDUP

	/* Init the object datastructure with size of the content */
    __arc_object_init(&obj->entry, datalen);
    return &obj->entry;
}

static int __cop_fetch(struct __arc_object *e, const void *content,
		__u32 ioblkID)
{
    struct object *obj = __arc_list_entry(e, struct object, entry);
#if 0
	if (obj->content != NULL)
	    return 0;	//0 for success here 
	else
	{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "In %s: dhashkey=%s\n", __FUNCTION__, (char*)obj->dhashkey);
#endif
		return -1;	//-1 for fetch failure
	}
#endif
	if (obj->content == NULL)
	{
#ifdef CONTENTDEDUP
		if (cache_insert_trap(obj->dhashkey))
		{
			RET_ERR("cache_insert_trap err'ed\n");
		}
#endif	//CONTENTDEDUP
		assert(content != NULL);
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
		{
			obj->content = malloc(MD5HASHLEN_STR-1);
			memcpy(obj->content, content, MD5HASHLEN_STR-1);
		}
		else
		{
			assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
			obj->content = malloc(BLKSIZE);
			memcpy(obj->content, content, BLKSIZE);
		}
		obj->ioblkID = ioblkID;
	}
	else
		assert(content == NULL);

	return 0;
}

static void __cop_evict(struct __arc_object *e)
{
    /* SSS: Eviction from cache involves removal of data, but the metadata
     * is still held for ARC purposes???
     */
    struct object *obj = __arc_list_entry(e, struct object, entry);
#if defined(SIMREPLAY_DEBUG_SS_DONE_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#ifdef CONTENTDEDUP
	if (cache_delete_trap(obj->dhashkey))
	{
		VOID_ERR("cache_delete_trap err'ed\n");
	}
#endif	//CONTENTDEDUP

    free(obj->content);
    obj->content = NULL;   /*SSS: so that it can be used in __cop_destroy */

//	UNUSED(obj);
}

static void __cop_destroy(struct __arc_object *e)
{
    struct object *obj = __arc_list_entry(e, struct object, entry);
#if defined(SIMREPLAY_DEBUG_SS_DONE_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    /* SSS: should free(obj) be preceeded with free(obj->content)? */
	if (obj->content != NULL)
	{
#ifdef CONTENTDEDUP
		if (cache_delete_trap(obj->dhashkey))
		{
			VOID_ERR("cache_delete_trap err'ed\n");
		}
#endif	//CONTENTDEDUP
		free(obj->content);
		obj->content = NULL;
	}
	free(obj);
	obj = NULL;
}

static struct __arc_ops ops = {
    .hash       = __cop_hash,
    .cmp        = __cop_compare,
    .create     = __cop_create,
    .fetch      = __cop_fetch,
    .evict      = __cop_evict,
    .destroy    = __cop_destroy
};

void contentcache_init()
{
	unsigned int prim;
#if defined(SIMREPLAY_DEBUG_SS_DONE_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	/* Calculate total content cache size */
	MAX_CCACHE_SIZE = CCACHEsize_MB * 1024 * 1024;

	/* Calculate bucket count for hash-table that represents content-cache */
	CCACHE_HASHTAB_COUNT = CCACHEsize_MB * 256;	//same as num of blocks
    if (!isPrime(CCACHE_HASHTAB_COUNT))
    {
        printf("CCACHE_HASHTAB_COUNT = %u, but we need prime\n", 
					CCACHE_HASHTAB_COUNT);
        for (prim=CCACHE_HASHTAB_COUNT; ; prim--)
            if (isPrime(prim))
                break;      //definitely will break at some point

        CCACHE_HASHTAB_COUNT = prim;
        printf("CCACHE_HASHTAB_COUNT = %u\n", CCACHE_HASHTAB_COUNT);
    }

	 /* "Datalen" is given as different values based on collectformat flag.
	 * Accordingly, the total content cache size needs to be set as well,
	 * for example, if hashes are used as content, total size should be small.
	 */
	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
		MAX_CCACHE_SIZE = MAX_CCACHE_SIZE / 128;
	fprintf(stdout, "creating content cache of size %u\n", MAX_CCACHE_SIZE);

	/* Initialize for calculated size */
	ccache = __arc_create(&ops, MAX_CCACHE_SIZE);
}

void contentcache_exit()
{
	__arc_destroy(ccache);
}

void contentcache_add(__u8* dhashkey, __u8* content, unsigned int datalen,
						__u32 ioblk, int updatehits)
{
	struct __arc_object *obj = NULL;
#if defined(SIMREPLAY_DEBUG_SS_DONE_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    /* Content in content-cache should be BLKSIZE long, but for sake
     * of simulation by traces, we may just use the human representation of
     * MD5 hashes as content themselves. Whichever the case may be, dhashkey
	 * is that content's hash (MD5/SHA1)
     */
	obj = __arc_add(ccache, dhashkey, content, datalen, ioblk, updatehits);
	if (obj == NULL)
		fatal(NULL, ERR_USERCALL, "__arc_add failed\n");
}

/* In reality, we might lookup content-based cache hashtable using hash function
 * on MD5/SHA1 of the given content. So, the lookup is based on MD5 and returned
 * is the content itself. 
 * Used only in read-path, so below counts of dedup-miss, dedup-hits, self-hits,
 * self-miss, etc. are only for read requests, and we reach here only for
 * metadata hit, i.e maphit_flag = 1
 */
__u8* contentcache_lookup(__u8* dhashkey, unsigned int len, __u32 ioblkID,
		__u32 d2pv_ioblkID /*only for counts of self-hits*/)
{
	struct __arc_object *e = NULL;
#if defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s, searching for key=%s\n", __FUNCTION__, dhashkey);
#endif

	e = __arc_lookup(ccache, dhashkey, len);
	if (warmupflag)
		assert(e == NULL);
	if (e == NULL)
	{
		return NULL;
	}
	
	struct object *obj = __arc_list_entry(e, struct object, entry);
	if (obj == NULL)
		fatal(NULL, ERR_USERCALL, "obj should not be empty in e!!\n");
//		return NULL;

	assert(obj->ioblkID == d2pv_ioblkID);
	if (!warmupflag) {
		if (ioblkID != obj->ioblkID)
			ccache_dedup_hits++;			//dedup-hit
		else
			ccache_nondedup_hits++;			//self-miss
	}

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	printf("Content cache lookup: buf=%s\n", (char*)obj->content);
	printf("Content cache lookup: md5="); puts((char*)obj->dhashkey);
#endif

	return (__u8*)(obj->content);		//could be blk-content or hex hash
}

/* Used in both read and write paths
 * Write path -- write to content-cache by ARC 
 * Read path --- write to content-cache while doing map update
 */
int overwrite_in_contentcache(struct preq_spec *preq, int updatehits) 
{    
    //savemem unsigned char key[HASHLEN];
	unsigned char *key = NULL;
#if defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s, content=%s and ", __FUNCTION__, preq->content);
#endif

#ifdef SIMREPLAY_DEBUG_SS
    assert(preq != NULL);
    assert(preq->bytes == BLKSIZE);
#endif

	/* If it is already accomplished, nothing to do here */
	if (preq->done == 1)
		return 0;

	/* update content-cache only after warm-up phase is over */
	if (warmupflag)
		return 0;

	 key = malloc(HASHLEN);	//malloc here so that above returns dont leak mem.

	/* ccache hits/misses are already counted for reads, so do it only for
	 * writes here => updatehits is a flag to request counting of hits/misses
	 * in contentcache via __arc_add().
	 * Also, for a write request, if already cache-hit in bcache, then
	 * dont count hit/miss in ccache, else results in double-counting!!
	 */
	assert((preq->rw && !updatehits) || (!preq->rw && updatehits) ||
			(!preq->rw && preq->bcachefound && !updatehits));

    if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
	{
		if (memcmp(preq->content, zeroarray, MD5HASHLEN_STR-1) == 0)
			memcpy(key, zerohash, MD5HASHLEN);		//hash is only 16 char long
		else if (getHashKey(preq->content, MD5HASHLEN_STR-1, key))
        	RET_ERR("getHashKey() returned error for MD5HASHLEN_STR\n");
	}
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
		if (memcmp(preq->content, zeroarray, BLKSIZE) == 0)
			memcpy(key, zerohash, HASHLEN);		//hash is only 16 char long
	    /* Hash(+magic) the block */
		else if (getHashKey(preq->content, BLKSIZE, key))
        	RET_ERR("getHashKey() returned error\n");
	}

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "key=%s\n", key);
#endif

	/* If disk is being simulated, the hex hash is already present in the traces
	 * and dhashkey is hash of hex hash.
	 * Similarly done in iodedupReadRequest().
	 * "Datalen" is given as different values based on collectformat flag.
	 * Accordingly, the total content cache size needs to be set as well,
	 * for example, if hashes are used as content, total size should be small.
     */
	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY)
	{
	    contentcache_add((__u8*)key, preq->content, MD5HASHLEN_STR-1, preq->ioblk, updatehits);
	}
	else
	{
		assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
	    contentcache_add((__u8*)key, preq->content, BLKSIZE, preq->ioblk, updatehits);
	}

	/* In read path, setting done isnt significant because this is return path.
	 * but in write path, setting done flag indicates that this request need not
	 * go to disk. However, since this is content-cache which is used only in 
	 * I/O dedup, write requests do need to reach disk.
	 */
	//preq->done = 1;	//Dont set done so that write requests reach disk
						//Also because we depend on this flag being not set
						//here, to do some ioblkID update to d2pv_datum
						//in io_mapupdate() for counting purposes.
	free(key);	//savemem
	return 0;
}

