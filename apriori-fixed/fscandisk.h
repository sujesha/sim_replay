#ifndef _FSCANDISK_H_
#define _FSCANDISK_H_

int fscan_VM_image(__u16 volID, char *devname);
int con_scan_and_process(void);
void* con_scandisk_routine(void *arg);

#endif /* _FSCANDISK_H_ */
