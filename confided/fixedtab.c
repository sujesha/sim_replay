/*
 * In preplay, fixed hash-table used for fixed dedup logic 
 * Implementation of the fixeds table type.
 *
 * Author : Sujesha Sudevalayam
 * Credit: Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include "fixedtab.h"
#include "debug.h"
#include "pdd_config.h"
#include "md5.h"
#include "utils.h"
#include "unused.h"

struct fixedtab fixedtab;
static int fixedtab_alive;


/* --------uses hashtab to create hashtable of fixeds-----------------*/

/* The key being input here can be the MD5 hash char array + magicSample */
__u32 fixedhash(struct hashtab *h, const void *key)
{
        const char *p, *keyp;
        unsigned int size;
        unsigned int val;

        if (!h || !key)
        {
            RET_ERR("NULL table or key in lblkhash().\n");
        }

        val = 0;
        keyp = (char *)key;
        //size = strlen(keyp);
        size = HASHLEN;
        for (p = keyp; (unsigned int)(p - keyp) < size; p++)
                val = (val << 4 | (val >> (8*sizeof(unsigned int)-4))) ^ (*p);
        return val & (h->size - 1);
}

/* The key being input here can be the MD5 hash char array + magicSample */
static int fixedcmp(struct hashtab *h, const void *key1, const void *key2)
{
        const char *keyp1, *keyp2;

		UNUSED(h);
        keyp1 = (char *)key1;
        keyp2 = (char *)key2;
        return memcmp(keyp1, keyp2, HASHLEN+MAGIC_SIZE);
}

int fixedtab_init(struct fixedtab *c, unsigned int s)
{
		unsigned int size = s;
		unsigned int prim;
		if (fixedtab_alive)
		{
			fprintf(stderr, "fixedtab already exists. dont init again\n");
			return 0;
		}
		if (!isPrime(size))
    	{
	        printf("size = %u, but we need it prime\n", size);
    	    for (prim=size; ; prim--)
        	    if (isPrime(prim))
                	break;      //definitely will break at some point

	        size = prim;
    	}
	    printf("Single bucket= %u bytes\n", (__u32)sizeof(struct hashtab_node));
	    printf("Creating fixedtab of %u buckets.\n", size);
        c->table = hashtab_create(fixedhash, fixedcmp, size, NULL);
        if (!c->table)
		{
			RET_ERR("error in hashtab_create for fixedtab_init\n");
		}
#ifdef DEBUG_SS
        fprintf(stdout, "Successful fixedtab_init().\n");
#endif
        c->nprim = 0;
		fixedtab_alive = 1;
        return 0;
}

void fixedtab_exit(struct fixedtab *l)
{
    /* The check of flag fixedtab_alive is done to ensure that repeated
     * calls to hashtab_destroy() do not happen. In fact, the flag is
     * set to 0 immediately before the actual call to hashtab_destroy()
     * because once invoked, it might take substantial time to finish
     * (since the hashtable could be potentially huge), and in the 
     * mean time, we do not want to entertain any more invocations.
     */
    if (l->table != NULL && fixedtab_alive)
    {
        fixedtab_alive = 0;
        hashtab_destroy(l->table);
    }
#ifdef DEBUG_SS
    printk(KERN_DEBUG "Successful lblktab_exit().\n");
#endif
}

