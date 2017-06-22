#ifndef _DEBUGG_H_
#define _DEBUGG_H_

#include <stdarg.h>

#define ERR_ARGS            1
#define ERR_SYSCALL         2
static inline void fatal(const char *errstring, const int exitval,
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

#endif /* _DEBUGG_H_ */

