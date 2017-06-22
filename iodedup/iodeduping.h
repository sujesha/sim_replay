#ifndef _IODEDUPING_H_
#define _IODEDUPING_H_

#include <asm/types.h>
#include "d2pv-map.h"

int resumeDeduping(unsigned char *buf, __u16 len,
		__u32 blockID, int initflag, int lastblk_flag, int rw_flag);

//int perfWriteFixing(unsigned char *buf, int len, __u16 volID, __u32 blockID);

#endif /* _IODEDUPING_H_ */
