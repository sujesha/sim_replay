/*
 * A dedup table (symtab) maintains associations between dedup hashes and 
 * dedup mapping information, for IODEDUO deduplication and mapping information.
 * The type of the datum values is arbitrary. Represents deduplication metadata.
 * The dedup table type is implemented using the hash table type (hashtab).
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _DEDUPTAB_H_
#define _DEDUPTAB_H_

#include <asm/types.h>
#include "uhashtab.h"

struct deduptab{
	struct hashtab *table;  /* hash table (keyed on a string) */
    __u32 nprim;              /* number of primary names in table */
};

int deduptab_init(struct deduptab *c, unsigned int size);
void deduptab_exit(struct deduptab *c);

/* ddatum is same as d2pv_datum, defined in d2pv-map.h
 * So no need to duplicate the definition here.
 * Note that both deduptab.h and d2pv-map.h are included within d2pv-map.c
 * So, we should not define anything here that requires d2pv-map.h to be
 * included here. IMPORTANT.
 * For example, "typedef d2pv_datum ddatum"
 */

#endif  /* _DEDUPTAB_H_ */
