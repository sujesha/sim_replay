#ifndef _STD_SERVEIO_H_
#define _STD_SERVEIO_H_
#include "serveio-utils.h"

int standardReadRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq);
int standardWriteRequest(struct vm_pkt *blkReq, 
				struct preq_spec **preql, int *nreq);

#endif /* _STD_SERVEIO_H_ */
