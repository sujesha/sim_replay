#ifndef _IOSERVEIO_H_
#define _IOSERVEIO_H_

#include <asm/types.h>
//#include "per-input-file.h"
#include "d2pv-map.h"
#include "serveio-utils.h"
#include "vmbunching_structs.h"
#include "p2d-map.h"

void* io_mapupdate_sub(void *arg);
int iodedupReadRequest(struct preq_spec *preq);
int iodedupWriteRequest(struct preq_spec *preq);
D2P_tuple_t* get_deduped_d2p(dedupmap_t *d2pv);
D2P_tuple_t* get_nondeduped_d2p(dedupmap_t *d2pv, __u32 ioblkID);
int resetMappingsIO(p2d_datum *p2d, __u32 ioblkID);
int recycleiodedupID(dedup_id_t iodedupID, int nozero_flag,
						__u16 volID, __u32 blockID);
int mappingTrimScanIO(struct preq_spec **preql, int numBlocks);
#endif /* _IOSERVEIO_H_ */
