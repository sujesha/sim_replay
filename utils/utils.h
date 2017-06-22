
#ifndef _UTILS_H_
#define _UTILS_H_

#include <asm/types.h>
#include "pdd_config.h"
#include "debug.h"

#define KEY_ZERO       (0 << 0)  // 000000
#define KEY_ONE       (1 << 0)  // 000001

#ifndef gen_malloc
#define gen_malloc(name, type, num) \
		name = (type *) calloc(num, sizeof(type));	\
		if (name == NULL)	\
			EXIT_TRACE("calloc failed in gen_malloc\n");
#endif

#ifndef gen_realloc
#define gen_realloc(name, type, num) \
		name = realloc(name, (num) * sizeof(type));	\
		if (name == NULL)	\
			EXIT_TRACE("realloc failed in gen_realloc\n");
#endif

/* ZEROBLK_FLAG or GOODBLK_FLAG to be indicated only in scanning phase */
enum {
	ZEROBLK_FLAG = 11,
	GOODBLK_FLAG,
	ULTIMATE_LASTBLK,	/* There is no block beyond this one */
	DONTCARE_LASTBLK,	/* for runtime-mapping, this doesnt matter?! */
	DONTCARE_VOLID = 5000,
};

//#define dec_blkoffset(val) ((val)==0) ? (BLKSIZE-1):((val)-1)

/* prototypes */
unsigned char* alloc_mem(__u16 len);
void copycontent(__u8 **d, __u8 *s, unsigned int nbytes);
void free_mem(unsigned char* mem);
__u16 inc_blkoffset(__u16 val);
__u16 dec_blkoffset(__u16 val);
void markMagicSample(unsigned char *buf, int len, unsigned char *mag);
int getHashKey(unsigned char *buf, __u32 len, unsigned char key[]);
int read_data(int fd, void *buffer, int bytes);
int isPrime(unsigned int num);

#endif /* _UTILS_H_ */

