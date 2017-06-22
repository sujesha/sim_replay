/*
 * A hash table (hashtab) maintains associations between
 * key values and datum values.  The type of the key values
 * and the type of the datum values is arbitrary.  The
 * functions for hash computation and key comparison are
 * provided by the creator of the table.
 * Adapted to userspace
 *
 * Author : Stephen Smalley, <sds@epoch.ncsc.mil>
 * security/selinux/ss/hashtab.h of linux-2.6.35.13
 */
#ifndef _UHASHTAB_H_
#define _UHASHTAB_H_

#include <asm/types.h>

#define HASHTAB_MAX_NODES       0xffffffff

struct hashtab_node {
        void *key;
        void *datum;
        struct hashtab_node *next;
};

struct hashtab {
        struct hashtab_node **htable;   /* hash table */
        __u32 size;                       /* number of slots in hash table */
        __u32 nel;                        /* number of elements in hash table */
        __u32 (*hash_value)(struct hashtab *h, const void *key);
                                        /* hash function */
        int (*keycmp)(struct hashtab *h, const void *key1, const void *key2);
                                        /* key comparison function */
		void (*rementry)(void *datum);
};

struct hashtab_info {
        __u32 slots_used;
        __u32 max_chain_len;
};

/*
 * Creates a new hash table with the specified characteristics.
 *
 * Returns NULL if insufficent space is available or
 * the new hash table otherwise.
 */
struct hashtab *hashtab_create(__u32 (*hash_value)(struct hashtab *h, 
	const void *key),
	int (*keycmp)(struct hashtab *h, const void *key1, const void *key2),
                               __u32 size,
	void (*rementry)(void *datum));

/*
 * Inserts the specified (key, datum) pair into the specified hash table.
 *
 * Returns -ENOMEM on memory allocation error,
 * -EEXIST if there is already an entry with the same key,
 * -EINVAL for general errors or
  0 otherwise.
 */
int hashtab_insert(struct hashtab *h, unsigned char *k, void *d);

/** 
 * Searched by specified key and removes it from the hash table, so that it
 * can be freed up later.
 *
 * Returns -ENOENT if the entry is not present.
 * 			-EINVAL for general errors or 0 otherwise.
 */
int hashtab_remove(struct hashtab *h, unsigned char *key);

/*
 * Searches for the entry with the specified key in the hash table.
 *
 * Returns NULL if no entry has the specified key or
 * the datum of the entry otherwise.
 */
void *hashtab_search(struct hashtab *h, unsigned char *k);

/*
 * Destroys the specified hash table.
 */
void hashtab_destroy(struct hashtab *h);

/*
 * Applies the specified apply function to (key,datum,args)
 * for each entry in the specified hash table.
 *
 * The order in which the function is applied to the entries
 * is dependent upon the internal structure of the hash table.
 *
 * If apply returns a non-zero status, then hashtab_map will cease
 * iterating through the hash table and will propagate the error
 * return to its caller.
 */
int hashtab_map(struct hashtab *h,
                int (*apply)(void *k, void *d, void *args),
                void *args);

/* Fill info with some hash table statistics */
void hashtab_stat(struct hashtab *h, struct hashtab_info *info);

#endif  /* _UHASHTAB_H */
