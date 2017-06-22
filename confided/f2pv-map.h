
#ifndef _F2PV_MAP_H_
#define _F2PV_MAP_H_

#include <asm/types.h>
#include "defs.h"
#include "slist.h"				/* LIST_HEAD */
#include "pdd_config.h"
#include "ulist.h"
#include "mbuffer.h"
#include "md5.h"
//#include "chunking.h"

typedef __u32 fixed_id_t;


/* --------begin chunk-to-virt (F2V) mapping information--------------*/
/* One tuple corresponds to one F2V map. 
 * For a deduplicated chunk, there are multiple tuples 
 */
typedef struct F2V_tuple_t{
    struct slist_head head;  /* To add in list f2vmaps */

    unsigned char dedupfetch:1; /* Signifies this dedup tuple to be fetched */
    //unsigned char dedupfetch; /* Signifies this dedup tuple to be fetched */
    __u16 volID;
    __u32 blockID;
}F2V_tuple_t;

typedef struct fixedmap_t{
    fixed_id_t fixedID;
//    struct slist_head head;  /* to allow these f2pv to form part of f2pvCList */

    unsigned char fhashkey[HASHLEN + MAGIC_SIZE];
    //unsigned char fdirty;
    //unsigned char fdirty:1;

    struct slist_head f2vmaps;

	//FIXME: can we discard c2pmaps? and only do f2v, v2p instead?
#if 0
    LIST_HEAD(c2pmaps);         
#endif								

}fixedmap_t;
typedef fixedmap_t f2pv_datum;

struct fixed_t
{
  mbuffer_t uncompressed_data;
};

/* prototypes */
void note_f2v_tuple(F2V_tuple_t *f2vp, __u16 volID, __u32 blockID);
struct fixedmap_t* getFixedMap(fixed_id_t fixedID);
void remove_f2v_tuple_from_map(F2V_tuple_t *f2v, f2pv_datum *f2pv);
#if 0
fixed_id_t processFixed(
                struct fixed_t *f, /* fixed blk to be processed != NULL */
                __u32 newBoundaryBlkNum, /* blk in which fixed chunk found */
                __u16 volID,        /* blk belongs to which volume */
                int initflag);  /* online phase or scanning phase */
#endif
void create_fixedmap_mapping_space(void);
void free_fixedmap(void);
void add_f2v_tuple_to_map(F2V_tuple_t *f2vt, f2pv_datum *f2pv);
void note_fixed_attrs(f2pv_datum *f2pv,
                unsigned char *key, __u32 fixedID);
fixed_id_t getNextFixedNum(int initflag);
void setFixedMap(fixed_id_t fixedID, f2pv_datum *f2pv);
int get_fullfixed(fixedmap_t* f2pv, unsigned char **buf, __u8 *content);

#endif /* _F2PV_MAP_H_ */
