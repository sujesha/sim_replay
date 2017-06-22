/*
 * A voltab maintains associations between VM name and volID.
 * The type of the datum values is char/string. Represents VM name.
 * The voltab type is implemented using the hash table type (hashtab).
 *
 * Author : Sujesha Sudevalayam
 * Credit : Stephen Smalley, <sds@epoch.ncsc.mil>
 */
#ifndef _VOLTAB_H_
#define _VOLTAB_H_

#include <asm/types.h>
#include "uhashtab.h"
#include "vmbunching_structs.h"

struct voltab {
        struct hashtab *table;  /* hash table (keyed on a string) */
        __u32 nprim;              /* number of primary names in table */
};

struct vol_datum{
		char vmname[HOSTNAME_LEN];
		__u16 volID;
};

int voltab_init(struct voltab *c, unsigned int size);
void voltab_exit(struct voltab *c);


#endif  /* _VOLTAB_H_ */
