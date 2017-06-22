
#ifndef _D2PV_MAP_H_
#define _D2PV_MAP_H_

#include <asm/types.h>
#include "defs.h"
#include "slist.h"				/* LIST_HEAD */
#include "pdd_config.h"
#include "ulist.h"
#include "mbuffer.h"
#include "md5.h"
//#include "chunking.h"

typedef __u32 dedup_id_t;


/* --------begin dedup-to-phys(D2P) mapping information--------------*/
/* One tuple corresponds to one D2P map. 
 * For a deduplicated chunk, there are multiple tuples 
 */
typedef struct D2P_tuple_t{
    struct slist_head head;  /* To add in list d2pmaps */

    __u32 ioblkID;
}D2P_tuple_t;

typedef struct dedupmap_t{
	__u32 ioblkID; 			/* physical block ID for host I/O, only to count */
							/* updated each time to reflect the obj->ioblkID
							 * of this content object in ARC cache.
							 */
    dedup_id_t iodedupID;	/* deduplication ID */
//    struct slist_head head;  /* to allow these d2pv to form part of d2pvCList */

    unsigned char dhashkey[HASHLEN + MAGIC_SIZE];	//hex or non-hex 

    struct slist_head d2pmaps;
}dedupmap_t;
typedef dedupmap_t d2pv_datum;

struct dedup_t
{
  mbuffer_t uncompressed_data;
};

/* prototypes */
void note_d2p_tuple(D2P_tuple_t *d2pp, __u32 ioblkID);
struct dedupmap_t* getDedupMap(dedup_id_t dedupID);
void remove_d2p_tuple_from_map(D2P_tuple_t *d2p, d2pv_datum *d2pv);
#if 0
dedup_id_t processDedup(
                struct dedup_t *f, /* dedup blk to be processed != NULL */
                __u32 newBoundaryBlkNum, /* blk in which dedup chunk found */
                __u16 volID,        /* blk belongs to which volume */
                int initflag);  /* online phase or scanning phase */
#endif
void create_dedupmap_mapping_space(void);
void free_dedupmap(void);
void add_d2p_tuple_to_map(D2P_tuple_t *d2pt, d2pv_datum *d2pv);
void note_dedup_attrs(d2pv_datum *d2pv,
                unsigned char *key, __u32 dedupID, __u32 ioblkID);
dedup_id_t getNextDedupNum(int initflag);
void setDedupMap(dedup_id_t dedupID, d2pv_datum *d2pv);
int get_fulldedup(dedupmap_t* d2pv, unsigned char **buf);

#endif /* _D2PV_MAP_H_ */
