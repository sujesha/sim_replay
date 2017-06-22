
#ifndef _C2PV_MAP_H_
#define _C2PV_MAP_H_

#include <asm/types.h>
#include "defs.h"
#include "slist.h"				/* LIST_HEAD */
#include "rabin.h"
#include "pdd_config.h"
#include "ulist.h"
#include "md5.h"
//#include "chunking.h"

typedef __u32 chunk_id_t;


/* Chunk max size is 64K and to represent 65536 needs 17 bits. In order to
 * fit this information into 16 bits, we choose to represent the value
 * lastoffsetminus1 i.e. sizes 1 to 65536 become lastoffsets 0 to 65535
 * and lastoffsetminus1 -1 to 65534 and -1 is represented as 65535
 */
typedef __u16 chunk_lastoffsetminus1_t;
typedef chunk_lastoffsetminus1_t chunk_size_t;


/* --------begin chunk-to-virt (C2V) mapping information--------------*/
/* One tuple corresponds to one C2V map. 
 * For a deduplicated chunk, there are multiple tuples 
 */
typedef struct C2V_tuple_t{
    struct slist_head head;  /* To add in list c2vmaps */

    unsigned char dedupfetch:1; /* Signifies this dedup tuple to be fetched */
    //unsigned char dedupfetch; /* Signifies this dedup tuple to be fetched */
    __u16 volID;
    __u32 start_vblk_id;
    __u16 start_offset_into_vblk;           /* vblks are sequential */
}C2V_tuple_t;

#if 0
/* One tuple corresponds to one C2P map. 
 * For a deduplicated chunk, there are multiple tuples 
 */
typedef struct C2P_tuple_t{
    struct slist_head head;  /* To add in list c2pmaps */

    unsigned char dedupfetch:1; /* Signifies this dedup tuple should be fetched */
    __u32 start_pblk_id;
    __u16 start_offset_into_pblk;
    Node *pblkIDUList;          /* Because pblks may not always be sequential */
}C2P_tuple_t;
#endif

typedef struct chunkmap_t{
    chunk_id_t chunkID;
//    struct slist_head head;  /* to allow these c2pv to form part of c2pvCList */

    unsigned char chashkey[HASHLEN + MAGIC_SIZE];
    chunk_size_t clen;  /* useful for both chunk-to-virt & chunk-to-phys */
//    unsigned char cdirty;		//add to V2C instead...?
    //unsigned char cforced;
    unsigned char cforced:1;

    struct slist_head c2vmaps;

	//FIXME: can we discard c2pmaps? and only do c2v, v2p instead?
#if 0
    LIST_HEAD(c2pmaps);         
#endif								

}chunkmap_t;
typedef chunkmap_t c2pv_datum;

/* prototypes */
#ifndef NONSPANNING_PROVIDE
int updateBegChunkMap(struct chunkmap_t **seqnextp, struct chunk_t **cdp,
        int lastblk_flag, __u16 blklen,
		chunk_id_t *chunkIDp, __u32 *endblkID, __u16 volID, __u32 blockID);
#endif
struct chunkmap_t* getChunkMap(chunk_id_t chunkID);
int get_fullchunk(chunkmap_t* c2pv, 
				struct chunk_t **chunkp, __u32 *endblkID);
chunk_id_t processChunk(
                struct chunk_t *c, /* chunk to be processed != NULL */
                __u16 leftover_len, /* length of leftover data <= BLKSIZE */
                __u32 newBoundaryBlkNum, /* blk in which chunk found */
                __u16 volID,        /* blk belongs to which volume */
                int initflag,   /* online phase or scanning phase */
                __u16 len,        /* len of block (special significance in I/O) */
                int lastblk_flag, /* for resetting static vars */
				int rw_flag);
void create_chunkmap_mapping_space(void);
void free_chunkmap(void);
void add_c2v_tuple_to_map(C2V_tuple_t *c2vt, c2pv_datum *c2pv);
void remove_c2v_tuple_from_map(C2V_tuple_t *c2v, c2pv_datum *c2pv);
void note_chunk_attrs(c2pv_datum *c2pv, struct chunk_t *chunk,
                unsigned char *key, chunk_id_t chunkID);
chunk_id_t getNextChunkNum(int initflag);
void setChunkMap(chunk_id_t chunkID, c2pv_datum *c2pv);

#endif /* _C2PV_MAP_H_ */
