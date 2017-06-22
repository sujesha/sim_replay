
/* This file needs to be included only if CONTENTDEDUP is #defined. */
#ifdef CONTENTDEDUP

#ifndef _CONTENTDEDUP_API_H_
#define _CONTENTDEDUP_API_H_

#include <asm/types.h>
#include "defs.h"
#include "slist.h"				/* LIST_HEAD */
#include "pdd_config.h"
#include "ulist.h"
#include "md5.h"


struct contentdeduptab_t{
	__u32 numcopies;
    unsigned char hashkey[HASHLEN + MAGIC_SIZE];
};
typedef struct contentdeduptab_t content_datum;


void itemcountfn_init(char *type);
void create_contentdeduptab_space(char *type);
void free_contentdeduptab(void);
int cache_insert_trap(unsigned char *key);
int cache_delete_trap(unsigned char *key);
void print_total_uniq_items(void);

#endif /* _CONTENTDEDUP_API_H_ */

#endif
