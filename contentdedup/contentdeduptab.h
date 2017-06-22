/*
 * A memdedup table (contentdeduptab) maintains information regarding memory 
 * deduplication for the sake of simulation results.
 * This file needs to be included only if CONTENTDEDUP is #defined.
 *
 * The type of the datum values is arbitrary. Represents memdedup
 * The memdedup table type is implemented using the hash table type (hashtab).
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */

#ifdef CONTENTDEDUP

#ifndef _CONTENTDEDUPTAB_H_
#define _CONTENTDEDUPTAB_H_

#include <asm/types.h>
#include "uhashtab.h"

struct contentdeduptab{
	struct hashtab *table;  /* hash table (keyed on a string) */
};

int contentdeduptab_init(struct contentdeduptab *c, unsigned int size);
void contentdeduptab_exit(struct contentdeduptab *c);

#endif  /* _CONTENTDEDUPTAB_H_ */

#endif /* CONTENTDEDUP */
