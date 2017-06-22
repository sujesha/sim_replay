#ifndef _SCANDISK_H_
#define _SCANDISK_H_

int _do_bread(int dp, __u32 start, __u16 bytes, unsigned char *buf);
int scan_VM_image(__u16 volID, int dp);
int pro_scan_and_process(char *devname);
void* pro_scandisk_routine(void *arg);

#endif /* _SCANDISK_H_ */
