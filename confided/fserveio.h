#ifndef _FSERVEIO_H_
#define _FSERVEIO_H_

#include <asm/types.h>
//#include "per-input-file.h"
#include "f2pv-map.h"
#include "serveio-utils.h"
#include "vmbunching_structs.h"
#include "v2f-map.h"


void unmark_old_dedupfetchF(fixedmap_t *f2pv);
void* f_mapupdate_sub(void *arg);
int confideReadRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq);
int confideWriteRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preq, int *nreq);
F2V_tuple_t* get_nondeduped_f2v(fixedmap_t *f2pv, __u16 volID, 
				__u32 vBlkID);
int resetMappingsF(v2f_datum *v2f, __u16 volID, __u32 vBlkID,
		__u16 *leader_volIDp, __u32 *leader_blkIDp);
int recyclefixedID(fixed_id_t fixedID, int nozero_flag,
						__u16 volID, __u32 blockID,
						__u16 *leader_volIDp, __u32 *leader_blkIDp);
F2V_tuple_t * get_deduped_f2v(fixedmap_t *f2pv);
int fetchdata_pblk(struct preq_spec *preql);
__u32 getVirtBlkID(struct vm_pkt *blkReq);
inline __u16 getVolNum(struct vm_pkt *blkReq);
#endif /* _FSERVEIO_H_ */
