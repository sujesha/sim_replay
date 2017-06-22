/*
 * A chunk table (symtab) maintains associations between chunk hashes and 
 * chunk mapping information, for chunk deduplication and mapping information
 * to be written out to provided traces. 
 * The type of the datum values is arbitrary. Represents chunk
 * The chunk table type is implemented using the hash table type (hashtab).
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _CHUNKTAB_H_
#define _CHUNKTAB_H_

#include <asm/types.h>
#include "uhashtab.h"

struct chunktab {
        struct hashtab *table;  /* hash table (keyed on a string) */
        __u32 nprim;              /* number of primary names in table */
};

int chunktab_init(struct chunktab *c, unsigned int size);
void chunktab_exit(struct chunktab *c);

/* cdatum is same as c2pv_datum, defined in c2pv-map.h
 * So no need to duplicate the definition here.
 * Note that both chunktab.h and c2pv-map.h are included within c2pv-map.c
 * So, we should not define anything here that requires c2pv-map.h to be
 * included here. IMPORTANT.
 * For example, "typedef c2pv_datum cdatum"
 */

#endif  /* _CHUNKTAB_H_ */
