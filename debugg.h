#ifndef _DEBUGG_H_
#define _DEBUGG_H_

#include <linux/types.h>

#define ERR_ARGS            1
#define ERR_SYSCALL         2
#define ERR_USERCALL		3
#define ERR_COMPILE			4

inline void fatal(const char *errstring, const int exitval,
             const char *fmt, ...);
inline long long unsigned du64_to_nsec(__u64 du64);
inline long long unsigned du64_to_sec(__u64 du64);
inline long minl(long a, long b);
inline int min(int a, int b);

#endif /* _DEBUGG_H_ */
