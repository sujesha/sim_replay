#ifndef _SERVEIO_UTILS_H_
#define _SERVEIO_UTILS_H_

#include <asm/types.h>

/* This structure is used to represent the outgoing read/write requests
 * for physical blocks, after mapping logic is applied --- prelay, sreplay, etc.
 */     
struct preq_spec{
    __u32 ioblk;			/* physical block ID for host I/O */
	__u32 newleader_ioblk;	/* valid only for write request, 6 feb 2014 */
    __u32 bytes;
    __u16 start;                 
    __u16 end;
    int rw;
    char *blkidkey;				/* used only if preadwritedump traces &
							  	 * cache being simulated via file access.
								 * FIXME: Used for collectformat traces also,
								 * for simulating revertPhystoVirtMap()
								 * for PROVIDED.
								 */
    __u8 *content;
	unsigned char done:1;
	unsigned char bcachefound:1;
};

int elongate_preql(struct preq_spec **preql, int *nreq);
int revertPhystoVirtMap(struct preq_spec *preq, __u16* volID, __u32* blockID);
void create_preq_spec(__u16 volID, __u32 blockID, __u32 bytes, int rw,
        __u8 *content, __u16 start, __u16 end, struct preq_spec *preq);
void directcreate_preq_spec(__u32 ioblkID, __u32 bytes, int rw,
        __u8 *content, __u16 start, __u16 end, struct preq_spec *preq);
void copy_preq_spec(struct preq_spec *preq, struct preq_spec *dest);

#endif /* _SERVEIO_UTILS_H_ */
