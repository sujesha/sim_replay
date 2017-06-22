#include <string.h>
#include <asm/types.h>
#include <assert.h>
#include <stdio.h>
#include "uthash.h"
#include "pdd_config.h"
#include "md5.h"
#include "debugg.h"
#include "debug.h"
#include "contentdedup-API.h"
#include "utils.h"
#include "unused.h"

extern __u64 bcache_hits;
extern __u64 bcache_hits_w;
extern __u64 bcache_misses;
extern __u64 bcache_misses_w;
extern int preplayflag;
extern int freplayflag;
extern int warmupflag;

// this is an example of how to do a LRU cache in C using uthash
// http://uthash.sourceforge.net/
// by Jehiah Czebotar 2011 - jehiah@gmail.com
// this code is in the public domain http://unlicense.org/

//#define MAX_CACHE_COUNT 100000
__u32 MAX_CACHE_COUNT = 0;	/* Value is set via sectorcache_init */

struct CacheEntry {
    char *key;
    char *value;
    UT_hash_handle hh;
};
struct CacheEntry *cache = NULL;

char* find_in_cache(char *key)
{
    struct CacheEntry *entry;
    HASH_FIND_STR(cache, key, entry);
    if (entry) {
	    // remove it (so subsequent add will throw it on front of the list)
	    HASH_DELETE(hh, cache, entry);
    	HASH_ADD_KEYPTR(hh, cache, entry->key, strlen(entry->key), entry);
        return entry->value;
    }
    return NULL;
}

/* Proceeding to add content to cache, if the key (here blockID) already
 * exists in cache, then retrieve it, update cache hits, and delete it. Add
 * new content with the key value.
 */
void add_to_cache(char *key, char *value, int len, int updatehits,
					char *newleaderkey, int *bcachefoundout)
{
    struct CacheEntry *entry, *tmp_entry;
    struct CacheEntry *findentry=NULL;	//, *findnewleaderentry=NULL;
#ifdef CONTENTDEDUP
	//savemem unsigned char dig[HASHLEN + MAGIC_SIZE];
	unsigned char *dig = malloc(HASHLEN);
#endif

	UNUSED(newleaderkey);

    entry = malloc(sizeof(struct CacheEntry));
	if (!entry)
	{
		VOID_ERR("malloc failed in add_to_cache\n");
	}
    entry->key = strdup(key);
	entry->value = malloc(len);
	memcpy(entry->value, value, len);

	/* Find whether key/blockID already exists in sectorcache */
    HASH_FIND_STR(cache, key, findentry);
    if (findentry)
    {
        //If entry exists, remove it so that it can be newly added next.
        HASH_DELETE(hh, cache, findentry);
#ifdef CONTENTDEDUP
		if (getHashKey((unsigned char*)findentry->value, len, dig))
            VOID_ERR("getHashKey() returned error\n");
		if (cache_delete_trap(dig))
		{
			VOID_ERR("cache_delete_trap err'ed\n");
		}
#endif
    }

    /* Dont put "else-if" between this and above! */
	if (findentry && !updatehits)	/* read cache hit */
	{
		//If entry exists, remove it so that it can be newly added next.
		//moved above!!!!! HASH_DELETE(hh, cache, findentry);

        free(findentry->key);
        free(findentry->value);
        free(findentry);
	}
	else if (findentry && updatehits)	/* write cache hit */
	{
		//If entry exists, remove it so that it can be newly added next.
		//moved above!!!!! HASH_DELETE(hh, cache, findentry);

		if (!warmupflag) {	//dont count stats in warmup (begin)
			bcache_hits++;
			bcache_hits_w++;
			*bcachefoundout = 1;	//to avoid double-counting in content-cache
		}
        free(findentry->key);
        free(findentry->value);
        free(findentry);
	}
	else if (updatehits) /* Entry doesnt exist, but this is write path */
	{
		if (!warmupflag) {	//dont count stats in warmup (begin)
			bcache_misses++;	//write dont count for miss-classification//
			bcache_misses_w++;
			*bcachefoundout = 0;	//to avoid double-counting in content-cache
		}
	}

	/* Add the new entry */
    HASH_ADD_KEYPTR(hh, cache, entry->key, strlen(entry->key), entry);
#ifdef CONTENTDEDUP
	if (getHashKey((unsigned char*)entry->value, len, dig))
    	VOID_ERR("getHashKey() returned error\n");
	if (cache_insert_trap(dig))
	{
		VOID_ERR("cache_insert_trap err'ed\n");
	}
#endif
    
    // prune the cache to MAX_CACHE_COUNT
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	printf("%s: HASH_COUNT(cache) = %u\n", __FUNCTION__, HASH_COUNT(cache));
#endif
    while (HASH_COUNT(cache) > (unsigned int)MAX_CACHE_COUNT) 
	{
		if (MAX_CACHE_COUNT <= 0)
			fatal(NULL, ERR_USERCALL, "Initialize MAX_CACHE_COUNT by " \
				"sectorcache_init()\n");

        HASH_ITER(hh, cache, entry, tmp_entry) {
            // prune the first entry (loop is based on insertion order so this deletes the oldest item)

#if defined(SIMREPLAY_DEBUG_SS_DONE)
			if (strstr(entry->key, "1050479") || strstr(entry->key, "1095667"))
				printf("%s: Evicting %s for %s\n",__FUNCTION__,entry->key,key);
#endif

            HASH_DELETE(hh, cache, entry);
#ifdef CONTENTDEDUP
			if (getHashKey((unsigned char*)entry->value, len, dig))
		    	VOID_ERR("getHashKey() returned error\n");
			if (cache_delete_trap(dig))
			{
				VOID_ERR("cache_delete_trap err'ed\n");
			}
#endif
            free(entry->key);
            free(entry->value);
            free(entry);
            break;
        }
    }    
#ifdef CONTENTDEDUP
	free(dig);	//savemem
#endif
}

void print_lrucache_stat()
{
    struct CacheEntry *entry, *tmp_entry;
	printf("BCACHE state: [ ");
	HASH_ITER(hh, cache, entry, tmp_entry) {
		printf("%s ", entry->key);
	}
	printf("]\n");
}

void free_lru_cache()
{
    struct CacheEntry *entry, *tmp_entry;
	HASH_ITER(hh, cache, entry, tmp_entry) {
    // prune the first entry (loop is based on insertion order so this deletes the oldest item)
    	HASH_DELETE(hh, cache, entry);
        free(entry->key);
        free(entry->value);
        free(entry);
	}
}

#if 0
int main()
{
	char *value;
	add_to_cache("1", "sujesha");
	add_to_cache("2", "susha");
	add_to_cache("3", "usha");
	value = find_in_cache("2");
	if (value)
		printf("found value = %s\n", value);
	else
		printf("value not found for key 2\n");
	value = find_in_cache("4");
	if (value)
		printf("found value = %s\n", value);
	else
		printf("value not found for key 4\n");

}
#endif
