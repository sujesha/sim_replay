/*
 * Implementation of hashtable for looking up volID based on vmname of vm_pkt
 *
 * Author : Sujesha Sudevalayam
 * Credit: Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#include <linux/string.h>
#include <linux/errno.h>
#include "voltab.h"
#include "utils.h"
#include "md5.h"
#include "debug.h"
#include "unused.h"

struct voltab voltab;
static int voltab_alive = 0;


/* --------uses hashtab to create hashtable of volume info---------------*/

/* The key being input here is the vmname from struct vm_pkt */
static __u32 volhash(struct hashtab *h, const void *key)
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
        for (p = keyp; (unsigned int)(p - keyp) < size; p++)
                val = (val << 4 | (val >> (8*sizeof(unsigned int)-4))) ^ (*p);
        return val & (h->size - 1);
}

/* The key being input here is the vmname from struct vm_pkt */
static int volcmp(struct hashtab *h, const void *key1, const void *key2)
{
        const char *keyp1, *keyp2;

		UNUSED(h);
        keyp1 = (char *)key1;
        keyp2 = (char *)key2;
        return strcmp(keyp1, keyp2);
}

int voltab_init(struct voltab *c, unsigned int s)
{
		unsigned int size = s;
		unsigned int prim;
#ifdef RECREATE_DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
		if (voltab_alive)
		{
			fprintf(stderr, "voltab already exists, dont init again\n");
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
	    printf("Creating voltab of %u buckets.\n", size);

        c->table = hashtab_create(volhash, volcmp, size, NULL);
        if (!c->table)
                return -1;
        c->nprim = 0;
		voltab_alive = 1;
        return 0;
}

void voltab_exit(struct voltab *c)
{
	/* The check of flag voltab_alive is done to ensure that repeated
     * calls to hashtab_destroy() do not happen. In fact, the flag is
     * set to 0 immediately before the actual call to hashtab_destroy()
     * because once invoked, it might take substantial time to finish
     * (since the hashtable could be potentially huge), and in the 
     * mean time, we do not want to entertain any more invocations.
     */
	if (c->table != NULL && voltab_alive)
	{
		voltab_alive = 0;
		hashtab_destroy(c->table);
	}
#ifdef DEBUG_SS
    fprintf(stdout, "Successful voltab_exit().\n");
#endif
}


