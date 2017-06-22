#ifndef _FIXING_H_
#define _FIXING_H_

#include <asm/types.h>
#include "f2pv-map.h"



int resumeFixing(unsigned char *buf, __u16 len, __u16 volID, 
		__u32 blockID, int initflag, int lastblk_flag, int updateleader, int);

int perfWriteFixing(unsigned char *buf, int len, __u16 volID, __u32 blockID);

#endif /* _FIXING_H_ */
