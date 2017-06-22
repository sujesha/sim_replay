
#ifndef _SECTORCACHE_H_
#define _SECTORCACHE_H_

#include "serveio-utils.h"

void sectorcache_init(void);
int find_in_sectorcache(struct preq_spec **preql, int numpkts);
int overwrite_in_sectorcache(struct preq_spec **preql, int numpkts);


#endif /* _SECTORCACHE_H_ */
