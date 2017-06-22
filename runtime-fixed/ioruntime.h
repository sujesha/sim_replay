#ifndef _IORUNTIME_H_
#define _IORUNTIME_H_

#include "serveio-utils.h"


int perfReadWriteDeduping(struct preq_spec *preq);
int io_mapupdate(struct preq_spec **preql, int numpkts);
int ioread_contentcacheupdate(struct preq_spec **preql, int numpkts);

#endif /* _IORUNTIME_H_ */
