#ifndef _SERVEIO_H_
#define _SERVEIO_H_

#include <asm/types.h>
//#include "per-input-file.h"
#include "c2pv-map.h"
#include "serveio-utils.h"
#include "rabin.h"
#include "vmbunching_structs.h"
#include "v2c-map.h"
#include "chunking.h"

#ifndef NONSPANNING_PROVIDE
int form_prechunk(struct chunk_t **prechunkp,
			   		v2c_datum *prev_v2c, chunkmap_t *last_cpv);
int form_postchunk(struct chunk_t **postchunkp, v2c_datum *v2c, 
					v2c_datum *first_v2c,
					chunkmap_t *last_c2pv, chunkmap_t *first_c2pv, 
					__u32 *postchunk_endblkID);
void createPrePostchunkBufs(struct chunk_t **prechunkp, 
							struct chunk_t **postchunkp, 
			__u16 volID, __u32 blockID, __u32 numB, chunkmap_t **seqnextp);
#endif

void unmark_old_dedupfetch(chunkmap_t *c2pv);
int provideReadRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq);
int provideWriteRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preq, int *nreq);
C2V_tuple_t* get_nondeduped_c2v(chunkmap_t *c2pv, __u16 volID, 
				__u32 vBlkID);
int resetMappings(v2c_datum *v2c, __u16 volID, __u32 vBlkID);
int recyclechunkID(chunk_id_t chunkID, int nozero_flag,
						__u16 volID, __u32 blockID);
C2V_tuple_t * get_deduped_c2v(chunkmap_t *c2pv);
int fetchdata_pblk(struct preq_spec *preql);
__u32 getVirtBlkID(struct vm_pkt *blkReq);
inline __u16 getVolNum(struct vm_pkt *blkReq);
void* p_mapupdate_sub(void *arg);

#endif /* _SERVEIO_H_ */
