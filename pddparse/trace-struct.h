#ifndef _TRACE_STRUCT_H_
#define _TRACE_STRUCT_H_

#include <asm/types.h>

#define ENDIAN_MAGIC  0x65617400 /* same magic as blktrace_api.h */
#define ENDIAN_VERSION    (0x07)

#define CHECK_MAGIC(t)      ((magic & 0xffffff00) == ENDIAN_MAGIC)

#define FILE_EOF (-900)

struct trace_event_element
{
	__u32 magic;
    __u32 elt_len;  /* length of data in next trace element */
};

struct record_info
{
    __u32 elt_len;  /* length of data in next trace element - for temp */
	char *event;
    unsigned long long ptime;
    char *hostname;
    unsigned int blockID;
	unsigned int nbytes;
    char *digstr;
    int contentflag;
    char processname[30];
    int pid;
    int major;
    int minor;
    char *dataorkey;
};

#endif
