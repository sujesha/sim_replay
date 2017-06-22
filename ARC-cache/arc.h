
#ifndef __ARC_H__
#define __ARC_H__

#include <memory.h>
#include <stddef.h>
#include <asm/types.h>

/**********************************************************************
 * Simple double-linked list, inspired by the implementation used in the
 * linux kernel.
 */
struct __arc_list {
    struct __arc_list *prev, *next;
};

#define __arc_list_entry(ptr, type, field) \
    ((type*) (((char*)ptr) - offsetof(type,field)))

#define __arc_list_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define __arc_list_each_prev(pos, head) \
    for (pos = (head)->prev; pos != (head); pos = pos->prev)

static inline void
__arc_list_init( struct __arc_list * head )
{
    head->next = head->prev = head;
}

/* Added this function to correct the implementation of __arc_destroy(),
 * which was other resulting in segmentation fault due to dangling next
 * pointers, after the object had already been freed!
 */
static inline struct __arc_list*
__arc_list_first( struct __arc_list * head )
{
	if (head != head->next)
		return head->next;
	else
		return NULL;
}

static inline void
__arc_list_insert(struct __arc_list *list, struct __arc_list *prev, struct __arc_list *next)
{
    next->prev = list;
    list->next = next;
    list->prev = prev;
    prev->next = list;
}

static inline void
__arc_list_splice(struct __arc_list *prev, struct __arc_list *next)
{
    next->prev = prev;
    prev->next = next;
}


static inline void
__arc_list_remove(struct __arc_list *head)
{
    __arc_list_splice(head->prev, head->next);
    head->next = head->prev = NULL;
}

static inline void
__arc_list_prepend(struct __arc_list *head, struct __arc_list *list)
{
    __arc_list_insert(head, list, list->next);
}


/**********************************************************************
 * All objects are inserted into a hashtable to make the lookup fast.
 */
struct __arc_hash {
    unsigned long size;
    struct __arc_list *bucket;
};


/**********************************************************************
 * The arc state represents one of the m{r,f}u{g,} lists
 */
struct __arc_state {
    unsigned long size;
    struct __arc_list head;
};

/* This structure represents an object that is stored in the cache. Consider
 * this structure private, don't access the fields directly. When creating
 * a new object, use the __arc_object_init() function to initialize it. */
struct __arc_object {
    struct __arc_state *state;
    struct __arc_list head, hash;
    unsigned long size;
};

struct __arc_ops {
    /* Return a hash value for the given key. */
    __u32 (*hash) (const void *key);

    /* Compare the object with the key. */
    int (*cmp) (struct __arc_object *obj, const void *key);	//__cop_compare

    /* Create a new object. The size of the new object must be know at
     * this time. Use the __arc_object_init() function to initialize
     * the __arc_object structure. */
#if 1
    struct __arc_object *(*create) (const void *key, const void *content, 
					unsigned int len, __u32 ioblk);	//__cop_create
#else
    struct __arc_object *(*create) (const void *key, const void *content, 
									unsigned int len);
#endif
    
    /* Fetch the data associated with the object. */
    int (*fetch) (struct __arc_object *obj, const void *content,
			__u32 ioblk); //__cop_fetch
    
    /* This function is called when the cache is full and we need to evict
     * objects from the cache. Free all data associated with the object. */
    void (*evict) (struct __arc_object *obj);	//__cop_evict
    
    /* This function is called when the object is completely removed from
     * the cache directory. Free the object data and the object itself. */
    void (*destroy) (struct __arc_object *obj);
};

/* The actual cache. */
struct __arc {
    struct __arc_ops *ops;
    struct __arc_hash hash;
    
    __u32 c, p;
    struct __arc_state mrug, mru, mfu, mfug;
};

/* Functions to create and destroy the cache. */
struct __arc *__arc_create(struct __arc_ops *ops, __u32 c);
void __arc_destroy(struct __arc *cache);

/* Initialize a new object. To be used from the alloc() op function. */
void __arc_object_init(struct __arc_object *obj, unsigned long size);

/* Lookup an object in the cache. The cache automatically allocates and
 * fetches the object if it does not exists yet. */
struct __arc_object *__arc_lookup(struct __arc *cache, 
			const void *key, unsigned int len);

#if 1
struct __arc_object *__arc_add(struct __arc *cache, const void *dhashkey,
            const void *content, unsigned int len, __u32 ioblk, int updatehits);
#else
struct __arc_object *__arc_add(struct __arc *cache, const void *dhashkey,
            const void *content, unsigned int len);
#endif

#endif /* __ARC_H__ */

