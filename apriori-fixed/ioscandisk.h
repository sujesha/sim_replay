#ifndef _IOSCANDISK_H_
#define _IOSCANDISK_H_

int ioscan_VM_image(__u16 volID, char *devname);
int io_scan_and_process(void);
void* io_scandisk_routine(void *arg);

#endif /* _IOSCANDISK_H_ */
