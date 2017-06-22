#ifndef _COMMOM_H_
#define _COMMON_H_

#include <stdio.h>
#include <asm/types.h>
#include <string.h>
#include <sys/param.h>


#ifndef CONFIG_X86_64
#define PRI_SECT    "%llu"
#else
#define PRI_SECT    "%lu"
#endif

#define BLKSIZE 4096

#endif 	/* _COMMON_H_ */
