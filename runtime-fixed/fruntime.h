#ifndef _FRUNTIME_H_
#define _FRUNTIME_H_

#include "serveio-utils.h"
#include "vmbunching_structs.h"

int perfReadWriteFixing(struct preq_spec *preq, __u16 volID, __u32 blkID);
int f_mapupdate(struct preq_spec **preql, struct vm_pkt *blkReq, int numpkts);

#endif /* _FRUNTIME_H_ */
