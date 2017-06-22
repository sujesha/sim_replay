/*
 * blkidtab maintains information regarding blkID and its content
 * for fixing consistency problem in traces.
 *
 * Author : Sujesha Sudevalayam
 * Credit: Stephen Smalley, <sds@epoch.ncsc.mil>
 */



#include <string.h>
#include <errno.h>
#include <asm/types.h>
#include "blkidtab.h"
#include "debug.h"
#include "pdd_config.h"
#include "md5.h"
#include "utils.h"
#include "unused.h"
#include "blkidtab-API.h"

struct blkidtab blkidtab;
static int blkidtab_alive;


/* --------uses hashtab to create hashtable of blkids-----------------*/

__u32 blkidhash(struct hashtab *h, const void *key)
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
        size = strlen(keyp);
        //size = HASHLEN;
        for (p = keyp; (unsigned int)(p - keyp) < size; p++)
                val = (val << 4 | (val >> (8*sizeof(unsigned int)-4))) ^ (*p);
        return val & (h->size - 1);
}

/* The key being input here can be the MD5 hash char array + magicSample */
static int blkidcmp(struct hashtab *h, const void *key1, const void *key2)
{
        const char *keyp1, *keyp2;

		UNUSED(h);
        keyp1 = (char *)key1;
        keyp2 = (char *)key2;
		if (strlen(keyp1) != strlen(keyp2))
//					&& 0 == memcmp(keyp1, keyp2, strlen(keyp1)))
		{
//			RET_ERR("%s: key lengths dont match %s & %s, so keys dont match\n",
//				__FUNCTION__, keyp1, keyp2);
			return 1;	//non-zero value to indicate "dont match"
		}
        return memcmp(keyp1, keyp2, strlen(keyp1));
}

void blkidrem(void *datum)
{
	blkid_datum* item = NULL;
	item = (blkid_datum*)datum;

	/* Freeing or removing the entities within datum */
	free(item->data);		//malloc in simdisk_trap()
	free(item->blkidkey);	//strdup in simdisk_trap()
}

int blkidtab_init(struct blkidtab *c, unsigned int s)
{
		unsigned int size = s;
		unsigned int prim;
		if (blkidtab_alive)
		{
			fprintf(stderr, "blkidtab already exists. dont init again\n");
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
	    printf("Creating blkidtab of %u buckets.\n", size);
        c->table = hashtab_create(blkidhash, blkidcmp, size, blkidrem);
        if (!c->table)
		{
			RET_ERR("error in hashtab_create for blkidtab_init\n");
		}
#ifdef DEBUG_SS
        fprintf(stdout, "Successful blkidtab_init().\n");
#endif
		blkidtab_alive = 1;
        return 0;
}

void blkidtab_exit(struct blkidtab *l)
{
    /* The check of flag blkidtab_alive is done to ensure that repeated
     * calls to hashtab_destroy() do not happen. In fact, the flag is
     * set to 0 immediately before the actual call to hashtab_destroy()
     * because once invoked, it might take substantial time to finish
     * (since the hashtable could be potentially huge), and in the 
     * mean time, we do not want to entertain any more invocations.
     */
    if (l->table != NULL && blkidtab_alive)
    {
        blkidtab_alive = 0;
        hashtab_destroy(l->table);
    }
#ifdef DEBUG_SS
    printk(KERN_DEBUG "Successful blkidtab_exit().\n");
#endif
}


