#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <error.h>
#include <sys/types.h>
#include <linux/types.h>
#include "debugg.h"

inline void fatal(const char *errstring, const int exitval,
             const char *fmt, ...)
{
    va_list ap;

    if (errstring)
        perror(errstring);

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    exit(exitval);
    /*NOTREACHED*/
}

/**
 * min - Return minimum of two integers
 */
inline int min(int a, int b)
{
    return a < b ? a : b;
}

/**
 * minl - Return minimum of two longs
 */
inline long minl(long a, long b)
{
    return a < b ? a : b;
}

inline long long unsigned du64_to_sec(__u64 du64)
{
    return (long long unsigned)du64 / (1000 * 1000 * 1000);
}

inline long long unsigned du64_to_nsec(__u64 du64)
{
    return llabs((long long)du64) % (1000 * 1000 * 1000);
}




