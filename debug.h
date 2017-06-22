#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>
#include <stdlib.h>

#define LOG_ERR (-999)

#define ACCESSTIME_PRINT(fmt, msg...) {									\
	if (!warmupflag) fprintf(ftimeptr, fmt, ##msg);						\
}

#define TRACE(fmt, msg...) {                                               \
       fprintf(stderr, "[%s] " fmt, __FUNCTION__, ##msg);                  \
       }                               \

#define EXIT_TRACE(fmt, msg...) {                                          \
	TRACE("Error at file %s, line %d, func %s: " fmt,  __FILE__, __LINE__, __FUNCTION__, ##msg) \
       exit(-1);                                                           \
}

#ifndef VOID_ERR
#define VOID_ERR(fmt, errmsg...) { \
	TRACE("Error at file %s, line %d, func %s: " fmt,  __FILE__, __LINE__, __FUNCTION__, ##errmsg) \
}
#endif

#ifndef WHERE
#define WHERE TRACE("In file %s, line %d, func %s\n",  __FILE__, __LINE__, __FUNCTION__)
#endif

/** This one posts the error message on stderr and 
 * immediately returns with error value LOG_ERR 
 */
#define RET_ERR(fmt, errmsg...) {														\
	TRACE("Error at file %s, line %d, func %s: " fmt,  __FILE__, __LINE__, __FUNCTION__, ##errmsg); \
	return(LOG_ERR);													\
}

#endif //_DEBUG_H_
