
#include <arc.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include "contentcache.h"

#define MAX(a, b) ( (a) > (b) ? (a) : (b) )
#define MIN(a, b) ( (a) < (b) ? (a) : (b) )

unsigned char ccache_already_had_flag = 0; //only for counting self-hits/miss
__u32 ccache_already_had_obj_ioblkID = 0;

extern int warmupflag;
extern __u64 ccache_hits;
extern __u64 ccache_hits_w;
extern __u64 ccache_misses;
extern __u64 ccache_misses_w;
extern __u32 CCACHE_HASHTAB_COUNT;

/* A simple hashtable with fixed bucket count. */
static void __arc_hash_init(struct __arc *cache)
{
	unsigned long i;
//	cache->hash.size = 3079;	//should this be dependent on CCACHE_SIZE?
	cache->hash.size = CCACHE_HASHTAB_COUNT;
	cache->hash.bucket = malloc(cache->hash.size * sizeof(struct __arc_list));
	for (i = 0; i < cache->hash.size; ++i) {
		__arc_list_init(&cache->hash.bucket[i]);
	}
}

static void __arc_hash_insert(struct __arc *cache, const void *key, 
		struct __arc_object *obj)
{
	unsigned long hash = cache->ops->hash(key) & (cache->hash.size - 1);
	__arc_list_prepend(&obj->hash, &cache->hash.bucket[hash]);
}

static struct __arc_object *__arc_hash_lookup(struct __arc *cache, 
		const void *key, unsigned int datalen)
{
	struct __arc_list *iter;
	__u32 hash = cache->ops->hash(key) & (cache->hash.size - 1);

	__arc_list_each(iter, &cache->hash.bucket[hash]) {
		struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, hash);
		if (cache->ops->cmp(obj, key) == 0 && datalen==obj->size)
			return obj;
	}

	return NULL;
}

static void __arc_hash_fini(struct __arc *cache)
{
	free(cache->hash.bucket);
}

/* Initialize a new object with this function. */
void __arc_object_init(struct __arc_object *obj, unsigned long size)
{
	obj->state = NULL;
	obj->size = size;

	__arc_list_init(&obj->head);
	__arc_list_init(&obj->hash);
}

/* Forward-declaration needed in __arc_move(). */
static void __arc_balance(struct __arc *cache, unsigned long size);

/* Move the object to the given state. If the state transition requires,
* fetch, evict or destroy the object. */
static struct __arc_object *__arc_move(struct __arc *cache, struct __arc_object *obj, struct __arc_state *state, const void *content, __u32 ioblk)
{
	assert(obj != NULL);
    if (obj->state) {
        obj->state->size -= obj->size;
        __arc_list_remove(&obj->head);
    }

    if (state == NULL) {
        /* The object is being removed from the cache, destroy it. */
        __arc_list_remove(&obj->hash);
        cache->ops->destroy(obj);

		return NULL;
    } else {
        if (state == &cache->mrug || state == &cache->mfug) {
            /* The object is being moved to one of the ghost lists, evict
             * the object from the cache. */
            cache->ops->evict(obj);
        } else if (obj->state != &cache->mru && obj->state != &cache->mfu) {
            /* The object is being moved from one of the ghost lists into
             * the MRU or MFU list, fetch the object into the cache. */
            __arc_balance(cache, obj->size);
            if (cache->ops->fetch(obj, content, ioblk)) {
                /* If the fetch fails, put the object back to the list
                 * it was in before. */
                obj->state->size += obj->size;
                __arc_list_prepend(&obj->head, &obj->state->head);
                
                return NULL;
            }
        }

        __arc_list_prepend(&obj->head, &state->head);

        obj->state = state;
        obj->state->size += obj->size;
    }
    
    return obj;
}

/* Return the LRU element from the given state. */
static struct __arc_object *__arc_state_lru(struct __arc_state *state)
{
    struct __arc_list *head = state->head.prev;
    return __arc_list_entry(head, struct __arc_object, head);
}

/* Balance the lists so that we can fit an object with the given size into
 * the cache. */
static void __arc_balance(struct __arc *cache, unsigned long size)
{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "__arc_balance\n");
#endif
    /* First move objects from MRU/MFU to their respective ghost lists. */
    while (cache->mru.size + cache->mfu.size + size > cache->c) {        
        if (cache->mru.size > cache->p) {
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "MRU size > cache in __arc_balance\n");
#endif
            struct __arc_object *obj = __arc_state_lru(&cache->mru);
            __arc_move(cache, obj, &cache->mrug, NULL, 0);
        } else if (cache->mfu.size > 0) {
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "MFU size > 0 in __arc_balance\n");
#endif
            struct __arc_object *obj = __arc_state_lru(&cache->mfu);
            __arc_move(cache, obj, &cache->mfug, NULL, 0);
        } else {
            break;
        }
    }
    
    /* Then start removing objects from the ghost lists. */
    while (cache->mrug.size + cache->mfug.size > cache->c) {        
        if (cache->mfug.size > cache->p) {
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "MFU ghost size > cache in __arc_balance\n");
#endif
            struct __arc_object *obj = __arc_state_lru(&cache->mfug);
            __arc_move(cache, obj, NULL, NULL, 0);
        } else if (cache->mrug.size > 0) {
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "MRU size > 0 in __arc_balance\n");
#endif
            struct __arc_object *obj = __arc_state_lru(&cache->mrug);
            __arc_move(cache, obj, NULL, NULL, 0);
        } else {
            break;
        }
    }
}


/* Create a new cache. */
struct __arc *__arc_create(struct __arc_ops *ops, __u32 c)
{
    struct __arc *cache = malloc(sizeof(struct __arc));
    memset(cache, 0, sizeof(struct __arc));

    cache->ops = ops;
    
    __arc_hash_init(cache);

    cache->c = c;
    cache->p = c >> 1;

    __arc_list_init(&cache->mrug.head);
    __arc_list_init(&cache->mru.head);
    __arc_list_init(&cache->mfu.head);
    __arc_list_init(&cache->mfug.head);
fprintf(stdout, "creating ARC cache of size %u\n", c);

    return cache;
}

#if 0
/* Destroy the given cache. Free all objects which remain in the cache. */
void __arc_destroy(struct __arc *cache)
{
    struct __arc_list *iter;
    
    __arc_list_each(iter, &cache->mrug.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
		__arc_move(cache, obj, NULL, NULL);
    }
    __arc_list_each(iter, &cache->mru.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
		__arc_move(cache, obj, NULL, NULL);
    }
    __arc_list_each(iter, &cache->mfu.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
		__arc_move(cache, obj, NULL, NULL);
    }
    __arc_list_each(iter, &cache->mfug.head) {
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, head);
		__arc_move(cache, obj, NULL, NULL);
    }

    __arc_hash_fini(cache);
    
    free(cache);
}
#else
/* Above implementation results in segmentation fault. Correct implementation
 * here.
 * Destroy the given cache. Free all objects which remain in the cache.
 */
void __arc_destroy(struct __arc *cache)
{
	struct __arc_list *iter;

	while ((iter = __arc_list_first(&cache->mrug.head)) != NULL)
	{
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, 
				head);
		__arc_move(cache, obj, NULL, NULL, 0);
	}
	while ((iter = __arc_list_first(&cache->mfug.head)) != NULL)
	{
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, 
				head);
		__arc_move(cache, obj, NULL, NULL, 0);
	}
	while ((iter = __arc_list_first(&cache->mru.head)) != NULL)
	{
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, 
				head);
		__arc_move(cache, obj, NULL, NULL, 0);
	}
	while ((iter = __arc_list_first(&cache->mfu.head)) != NULL)
	{
        struct __arc_object *obj = __arc_list_entry(iter, struct __arc_object, 
				head);
		__arc_move(cache, obj, NULL, NULL, 0);
	}
}
#endif


struct __arc_object *__arc_lookup(struct __arc *cache, 
			const void *dhashkey, unsigned int len)
{
    struct __arc_object *obj = __arc_hash_lookup(cache, dhashkey, len);

	if (obj)
	{
		if (obj->state == &cache->mru || obj->state == &cache->mfu) {
            /* Object is already in the cache, move it to the head of the
			 * MFU list. */
            return __arc_move(cache, obj, &cache->mfu, NULL, 0);
        }
		else
		{
			/* Object is in ghost lists, not in cache */
			return NULL;
		}
	}
	else
		return NULL;
}

/* Adding an object to ARC cache, if it is already in cache, its state will be
 * updated, else it will be newly created and added.
 * Copy of __arc_lookup()
 */
struct __arc_object *__arc_add(struct __arc *cache, const void *dhashkey,
	const void *content, unsigned int datalen, __u32 ioblk, int updatehits)
{
    struct __arc_object *obj = __arc_hash_lookup(cache, dhashkey, datalen);

    if (obj) 
	{
        if (obj->state == &cache->mru || obj->state == &cache->mfu) 
		{
			ccache_already_had_flag = 1;
			if (ccache_already_had_flag)
			{
				struct object *mobj = __arc_list_entry(obj, struct object,
					  entry);
				ccache_already_had_obj_ioblkID = mobj->ioblkID;
			}
			if (!warmupflag) {	//dont count stats in warmup (begin)
				if (updatehits)
				{
					ccache_hits++;
					ccache_hits_w++;
				}
			}
            /* Object is already in the cache, move it to the head of the
             * MFU list. */
            return __arc_move(cache, obj, &cache->mfu, NULL, 0);
        } 
		else if (obj->state == &cache->mrug) 
		{
            cache->p = MIN(cache->c, cache->p + MAX(cache->mfug.size / cache->mrug.size, 1));
			if (!warmupflag) {	//dont count stats in warmup (begin)
				if (updatehits)	//write request, so dont count as capacity_miss
				{
					ccache_misses++;
					ccache_misses_w++;
				}
			}
            return __arc_move(cache, obj, &cache->mfu, content, ioblk);
        } 
		else if (obj->state == &cache->mfug) 
		{
            cache->p = (__u32)MAX(0, (int)(cache->p - MAX(cache->mrug.size / cache->mfug.size, 1)));
			if (!warmupflag) {	//dont count stats in warmup (begin)
				if (updatehits)	//write request, so dont count as capacity_miss
				{
					ccache_misses++;
					ccache_misses_w++;
				}
			}
            return __arc_move(cache, obj, &cache->mfu, content, ioblk);
        } 
		else 
		{
            assert(0);
        }
    }
	else 
	{
		if (!warmupflag) {	//dont count stats in warmup (begin)
			if (updatehits)	//write request, so dont count as capacity_misses//
			{
				ccache_misses++;
				ccache_misses_w++;
			}
		}

#if 1
        obj = cache->ops->create(dhashkey, content, datalen, ioblk);
#else
        obj = cache->ops->create(dhashkey, content, datalen);
#endif
        if (!obj)
		{
			fprintf(stdout, "cache->ops->create failed\n");
            return NULL;
		}

		/* New objects are always moved to the MRU list. */
        __arc_hash_insert(cache, dhashkey, obj);
        return __arc_move(cache, obj, &cache->mru, NULL, 0);
    }
    __arc_hash_fini(cache);
    
    free(cache);
}

