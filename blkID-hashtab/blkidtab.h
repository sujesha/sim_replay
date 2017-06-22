/*
 * blkidtab maintains information regarding blkID and its content
 * for fixing consistency problem in traces.
 *
 * The type of the datum values is arbitrary. Represents blkID
 * The blkidtab type is implemented using the hash table type (hashtab).
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

#ifndef _BLKIDTAB_H_
#define _BLKIDTAB_H_

#include <asm/types.h>
#include "uhashtab.h"

struct blkidtab{
	struct hashtab *table;  /* hash table (keyed on a string -- blkid) */
};

int blkidtab_init(struct blkidtab *c, unsigned int size);
void blkidtab_exit(struct blkidtab *c);

#endif  /* _BLKIDTAB_H_ */
