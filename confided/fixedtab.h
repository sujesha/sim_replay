/*
 * A fixed table (symtab) maintains associations between fixed hashes and 
 * fixed mapping information, for fixed deduplication and mapping information
 * to be written out to provided traces. 
 * The type of the datum values is arbitrary. Represents fixed
 * The fixed table type is implemented using the hash table type (hashtab).
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _FIXEDTAB_H_
#define _FIXEDTAB_H_

#include <asm/types.h>
#include "uhashtab.h"

struct fixedtab{
	struct hashtab *table;  /* hash table (keyed on a string) */
    __u32 nprim;              /* number of primary names in table */
};

int fixedtab_init(struct fixedtab *c, unsigned int size);
void fixedtab_exit(struct fixedtab *c);

/* fdatum is same as f2pv_datum, defined in f2pv-map.h
 * So no need to duplicate the definition here.
 * Note that both fixedtab.h and f2pv-map.h are included within f2pv-map.c
 * So, we should not define anything here that requires f2pv-map.h to be
 * included here. IMPORTANT.
 * For example, "typedef f2pv_datum fdatum"
 */

#endif  /* _FIXEDTAB_H_ */
