#ifndef _PRUNTIME_H_
#define _PRUNTIME_H_


int perfReadWriteChunking(struct preq_spec *preq, __u16 volID, __u32 blkID);
int p_mapupdate(struct preq_spec **preql, struct vm_pkt *blkReq, int numpkts);

#endif /* _PRUNTIME_H_ */
