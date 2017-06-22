#ifndef _BLKIDTAB_API_H_
#define _BLKIDTAB_API_H_

#include <asm/types.h>
#include "defs.h"
#include "slist.h"				/* LIST_HEAD */
#include "pdd_config.h"
#include "ulist.h"
#include "md5.h"

struct blkidtab_t{
    char *blkidkey;
	void *data;		//generic data type
};
typedef struct blkidtab_t blkid_datum;


void create_blkidtab_space(void);
void free_blkidtab(void);
int construct_key_volid_blkid(__u16 volID, __u32 blkID, char *skey);
int construct_key_hostname_blkid(char *vmname, __u32 blkID, char *skey);



#endif /* _BLKIDTAB_API_H_ */
