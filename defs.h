#ifndef _DEFS_H_
#define _DEFS_H_

#include <stdlib.h>
#include <stdint.h>
#include <asm/types.h>

enum {
	NOINIT_STAGE = 20,
	INIT_STAGE,
	INIT_IMMATERIAL,
	NOINIT_MAPMISS,		//map create upon read request
	NOINIT_MAPHIT,		//map update upon read request
	NOINIT_MAPDIRTY,	//map update upon read request
};

/*-----------------------------------------------------------------------*/
/* type definition */
/*-----------------------------------------------------------------------*/

typedef uint8_t  u_char;
//conflicting typedef uint64_t u_long;
//conflicting typedef uint64_t ulong;
typedef uint32_t u_int;

typedef uint8_t  byte;
typedef byte     u_int8;
typedef uint16_t u_int16;
typedef uint32_t u_int32;
typedef uint64_t u_int64;

typedef uint64_t u64int;
typedef uint32_t u32int;
typedef uint8_t  uchar;
typedef uint16_t u16int;

typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;

/*-----------------------------------------------------------------------*/
/* useful macros */
/*-----------------------------------------------------------------------*/
#ifndef NELEM
#define NELEM(x) (sizeof(x)/sizeof(x[0]))
#endif

#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef MIN
#define MIN(x, y) ((x) < (y) ? (x) : (y))
#endif

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef O_LARGEFILE
#define O_LARGEFILE  0100000
#endif

#define unlikely(cond) (cond)
#define likely(cond) (cond)

#endif
