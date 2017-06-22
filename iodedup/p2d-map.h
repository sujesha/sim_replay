
#ifndef _P2D_MAP_H_
#define _P2D_MAP_H_

#include <asm/types.h>
#include "slist.h"
#include "defs.h"
#include "pdd_config.h"
#include "ulist.h"
#include "md5.h"
#include "iodeduping.h"
#include "vector32.h"


/* --------begin phy-to-dedup (P2D) mapping information--------------*/

typedef struct P2D_t{
    struct slist_head head;  /* To add in list pointed by p2dlistp */

    //unsigned char bhashkey[HASHLEN + MAGIC_SIZE]; /* magic appended to hash */
//    __u32 ioblkID;      // redundant?

    /* phy-to-dedup */
	__u32 iodedupID;
    unsigned char pdirty:1;

}P2D_t;
typedef P2D_t p2d_datum;

void create_p2d_mapping_space(void);
void free_p2dmaps(void);
void p2dmaps_set(vector32 *p2dmaps,__u32 x, void *e);
void *p2dmaps_get(vector32 *p2dmaps, __u32 x);
int notzeroIO_vblk(p2d_datum *p2d);
int note_p2d_map(p2d_datum *p2d, __u32 iodedupID,
		int lastblk_flag);
int dedup_dirty(p2d_datum *p2d);
int getVirttoDedupMap(__u32 ioblkID, __u16 count, 
				struct slist_head *p2dlistp);
int updateBlockio(__u32 ioblkID,
                int lastblk_flag, __u32 iodedupID);
int processBlockio(__u32 ioblkID,
				int lastblk_flag, __u32 iodedupID);

#endif /* _P2D_MAP_H_ */
