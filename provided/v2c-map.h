
#ifndef _V2C_MAP_H_
#define _V2C_MAP_H_

#include <asm/types.h>
#include "slist.h"
#include "defs.h"
#include "pdd_config.h"
#include "ulist.h"
#include "md5.h"
#include "chunking.h"
#include "vector16.h"


/* --------begin virt-to-chunk (V2C) mapping information--------------*/

typedef struct V2C_t{
    struct slist_head head;  /* To add in list pointed by v2clistp */

    //unsigned char bhashkey[HASHLEN + MAGIC_SIZE]; /* magic appended to hash */
//    __u16 volID;        // redundant?
//    __u32 blockID;      // redundant?

    /* virt-to-chunk */
    chunk_size_t start_offset_into_chunk;
    chunk_size_t end_offset_into_chunk;
	unsigned char cdirty:1;

    /* chunkIDUList[] contains list of all chunk IDs for this block, from start to end */
    Node *chunkIDUList;
}V2C_t;
typedef V2C_t v2c_datum;

#ifndef NONSPANNING_PROVIDE
int fixPrevBlock(int initflag, chunk_id_t chunkID, __u32 currblockID,              __u16 volID, int chunklist_stat, int foundchunk, int before_flag);
void addChunkIDtoPrev2c(chunk_id_t chunkID, __u16 volID, __u32 bID);
#endif

int notzero_vblk(v2c_datum *v2c);
int getVirttoChunkMap(__u16 volID, __u32 vBlkID, __u16 count,struct slist_head*);
int processBlock(unsigned char *buf, __u32 blockID, __u16 volID,
                int len_tillnow, int coincide_flag, 
				int *endcoincide_stat, int lastblk_flag);
int updateBlock(unsigned char *buf, __u32 blockID, __u16 volID,
                int len_tillnow, int lastblk_flag, int coincide_flag, 
				int *coinciding_stat, Node **chunkIDUListp);
void free_v2cmaps(void);
void create_v2c_mapping_space(void);
void v2cmaps_set(vector16 *v2cmaps, __u16 x, __u32 y, void *e);
void *v2cmaps_get(vector16 *v2cmaps, __u16 x, __u32 y);
int cvblk_dirty(v2c_datum* v2c);

#endif /* _V2C_MAP_H_ */
