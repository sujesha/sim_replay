
#ifndef _V2F_MAP_H_
#define _V2F_MAP_H_

#include <asm/types.h>
#include "slist.h"
#include "defs.h"
#include "pdd_config.h"
#include "ulist.h"
#include "md5.h"
#include "fixing.h"
#include "vector16.h"


/* --------begin virt-to-fixed (V2F) mapping information--------------*/

typedef struct V2F_t{
    struct slist_head head;  /* To add in list pointed by v2flistp */

    //unsigned char bhashkey[HASHLEN + MAGIC_SIZE]; /* magic appended to hash */
//    __u16 volID;        // redundant?
//    __u32 blockID;      // redundant?

    /* virt-to-fixed */
	__u32 fixedID;
	unsigned char fdirty:1;

}V2F_t;
typedef V2F_t v2f_datum;

int notzeroF_vblk(v2f_datum *v2f);
int getVirttoFixedMap(__u16 volID, __u32 vBlkID, __u16 count, struct slist_head*);
int processBlockf(__u32 blockID, __u16 volID,
				int lastblk_flag, __u32 fixedID); //, unsigned char key[]);
int updateBlockf(__u32 blockID, __u16 volID,
                int lastblk_flag, __u32 fixedID); //, unsigned char key[]);
void free_v2fmaps(void);
void v2fmaps_resize(vector16 *v2fmaps, __u16 x, __u32 size);
void create_v2f_mapping_space(void);
void v2fmaps_set(vector16 *v2fmaps, __u16 x, __u32 y, void *e);
void *v2fmaps_get(vector16 *v2fmaps, __u16 x, __u32 y);
int fixed_dirty(v2f_datum* v2f);

#endif /* _V2F_MAP_H_ */
