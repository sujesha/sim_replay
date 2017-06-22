#ifndef _SYNC_DISK_INTERFACE_H_
#define _SYNC_DISK_INTERFACE_H_

#include "serveio-utils.h"
#include "per-input-file.h"

int open_sync_device(char* path);
void close_sync_device(int dp);
int _do_read(struct preq_spec *preql);
int get_start_end_blk(__u16 volID, __u32 *start, __u32 *end);
int _do_bread(int dp, __u32 start, __u16 bytes, unsigned char *buf);
int _do_bwrite(int dp, __u32 start, __u16 bytes, unsigned char *buf);



#endif 	/* _SYNC_DISK_INTERFACE_H_ */
