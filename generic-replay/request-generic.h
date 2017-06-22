#ifndef _REQUEST_GENERIC_H_
#define _REQUEST_GENERIC_H_

int vblkIDMatch(__u16 newvolID,__u32 newblkID, __u16 volID, __u32 vBlkID);
__u32 getVirtBlkID(struct vm_pkt *blkReq);
inline __u16 getVolNum(struct vm_pkt *blkReq);
inline __u32 getNumBlocks(struct vm_pkt *blkReq);
inline u32int getNBytes(struct vm_pkt *blkReq);
int retrieveBuf(unsigned char **buf, struct vm_pkt *blkReq);








#endif /* _REQUEST_GENERIC_H_ */
