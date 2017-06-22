
///* This file needs to be included only if CONTENTDEDUP is #defined. */
//#ifdef CONTENTDEDUP
//#endif /* CONTENTDEDUP */

/* This file needs to be included only if CONTENTDEDUP is #defined. */
#ifdef CONTENTDEDUP


#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include "contentdeduptab.h"
#include "contentdedup-API.h"
#include "debug.h"
#include "utils.h"
#include "pdd_config.h"
#include "unused.h"

#define CONTENTTAB_SIZE BLKTAB_SIZE

int contentdeduptab_alive = 0;	/* contentdedup space been created or not */
FILE * itemptr = NULL;

extern struct contentdeduptab contentdeduptab;
extern __u32 uniq_items;
extern __u32 total_items;

void itemcountfn_init(char *type)
{
    char *progname = "replay";
    char itemcountfn[356];
    fprintf(stdout, "In %s\n", __FUNCTION__);

    strcpy(itemcountfn, "contentdedupcnt_output_");
    strcat(itemcountfn, progname);
    strcat(itemcountfn, "_4K_");
    strcat(itemcountfn, type);
    strcat(itemcountfn, ".txt");
    itemptr = fopen(itemcountfn, "w");  /* output file, appended by diskname,  
time and date. Used to output hashes  */
    if (itemptr == NULL)
        printf("Output file open failed: %s\n", itemcountfn);
    else
        printf("Opened output file.\n");
}


/** create_contentdeduptab_space: Use this to initialize
 * 		This space will get freed up in the end, in free_contentdeduptab().
 */
void create_contentdeduptab_space(char *type)
{
	/* First, check and set the status flag for contentdedup space creation */
	if (contentdeduptab_alive == 1)
	{
		fprintf(stdout, "contentdeduptab already non-NULL\n");
		return;
	}
	contentdeduptab_alive = 1;
    fprintf(stdout, "In %s\n", __FUNCTION__);
	
	/* Initialize contentdeduptab */
	if (contentdeduptab_init(&contentdeduptab, CONTENTTAB_SIZE))
	{
		VOID_ERR("contentdeduptab_init failed\n");
		contentdeduptab_exit(&contentdeduptab);
	}
	itemcountfn_init(type);
	return;
}

/** free_contentdeduptab -- free up the contentdedup mapping space, earlier created in
 * 					create_contentdeduptab_mapping_space().
 */
void free_contentdeduptab(void)
{
	/* First, reset status flag for contentdedup mapping space creation 
	 * This is so that the same space is not freed twice. 
	 */
	if (contentdeduptab_alive == 0)
	{
		fprintf(stdout, "contentdeduptab is not alive, exit\n");
		return;
	}
	contentdeduptab_alive = 0;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	/* Free contentdeduptab hashtable */
	contentdeduptab_exit(&contentdeduptab);	
	fprintf(stdout, "done now\n");
}	

/* To be invoked only when block is inserted into block-cache, 
 * i.e. not cache-hit
 */
int cache_insert_trap(unsigned char *key)
{
	int ret;
	content_datum *item = NULL, *dedupitem = NULL;
//    fprintf(itemptr, "%s\n", __FUNCTION__);

	dedupitem = (content_datum*)hashtab_search(contentdeduptab.table, key);
	if (dedupitem)
	{
		dedupitem->numcopies++;
		total_items++;
	}
	else
	{
		item = (content_datum*) calloc(1, sizeof(content_datum));
		memcpy(item->hashkey, key, HASHLEN);
		item->numcopies = 1;
		total_items++;
		uniq_items++;
		ret = hashtab_insert(contentdeduptab.table, item->hashkey, item);
		if (ret == -EEXIST)
		{
			RET_ERR("block already exists in contentdeduptab\n");
		}
		else if (ret == -ENOMEM)
		{
			RET_ERR("out of memory for contentdeduptab\n");
		}
	}

	return 0;
}

/* To be invoked when block is deleted from block-cache or content-cache */
int cache_delete_trap(unsigned char *key)
{
	content_datum * dedupitem = NULL;
 //   fprintf(itemptr, "%s\n", __FUNCTION__);

	dedupitem = (content_datum*)hashtab_search(contentdeduptab.table, key);
	if (!dedupitem)
	{
		RET_ERR("content not existing in contentdeduptab\n");
	}

	if (dedupitem->numcopies == 1)
	{
		if (hashtab_remove(contentdeduptab.table, dedupitem->hashkey))
		{
			RET_ERR("hashtab_remove failed for contentdeduptab\n");
		}
		free(dedupitem);
		uniq_items--;
		total_items--;
	}
	else
	{
		dedupitem->numcopies--;
		total_items--;
	}

	return 0;
}

void print_total_uniq_items(void)
{
	static __u32 i = 0;
//	if (i==0)
//		fprintf(itemptr, "total unique\n");

//	fprintf(itemptr, "iter=%u %u %u\n", i, total_items, uniq_items);
	fprintf(itemptr, "%u %u\n", total_items, uniq_items);
	i++;
}

#endif /* CONTENTDEDUP */
