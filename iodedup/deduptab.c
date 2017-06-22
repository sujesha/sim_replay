/*
 * In ioreplay, dedup hash-table used for IODEDUP deduplication logic 
 *
 * Author : Sujesha Sudevalayam
 * Credit: Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include "deduptab.h"
#include "debug.h"
#include "pdd_config.h"
#include "md5.h"
#include "utils.h"
#include "unused.h"

struct deduptab deduptab;
static int deduptab_alive;


/* --------uses hashtab to create hashtable of dedups-----------------*/

/* The key being input here can be the MD5 hash char array + magicSample */
__u32 deduphash(struct hashtab *h, const void *key)
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
static int dedupcmp(struct hashtab *h, const void *key1, const void *key2)
{
        const char *keyp1, *keyp2;

		UNUSED(h);
        keyp1 = (char *)key1;
        keyp2 = (char *)key2;
        return memcmp(keyp1, keyp2, HASHLEN+MAGIC_SIZE);
}

int deduptab_init(struct deduptab *c, unsigned int s)
{
		unsigned int size = s;
		unsigned int prim;
		if (deduptab_alive)
		{
			fprintf(stderr, "deduptab already exists. dont init again\n");
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
	    printf("Creating deduptab of %u buckets.\n", size);
        c->table = hashtab_create(deduphash, dedupcmp, size, NULL);
        if (!c->table)
		{
			RET_ERR("error in hashtab_create for deduptab_init\n");
		}
#ifdef DEBUG_SS
        fprintf(stdout, "Successful deduptab_init().\n");
#endif
        c->nprim = 0;
		deduptab_alive = 1;
        return 0;
}

void deduptab_exit(struct deduptab *l)
{
    /* The check of flag deduptab_alive is done to ensure that repeated
     * calls to hashtab_destroy() do not happen. In fact, the flag is
     * set to 0 immediately before the actual call to hashtab_destroy()
     * because once invoked, it might take substantial time to finish
     * (since the hashtable could be potentially huge), and in the 
     * mean time, we do not want to entertain any more invocations.
     */
    if (l->table != NULL && deduptab_alive)
    {
        deduptab_alive = 0;
        hashtab_destroy(l->table);
    }
#ifdef DEBUG_SS
    printk(KERN_DEBUG "Successful deduptab_exit().\n");
#endif
}

