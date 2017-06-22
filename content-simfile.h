#ifndef _CONTENT_SIMFILE_H_
#define _CONTENT_SIMFILE_H_

void simdiskfn_init(char *type);
int get_simcontent(__u32 ioblk, __u8 *simcontent, int generate);
int append_simcontent(__u32 ioblk, __u8 *buf);
void simdiskfn_exit();
int read_simdisk(__u32 ioblk, __u8 *simcontent);
int write_simdisk(__u32 ioblk, __u8 *buf);

#endif /* _CONTENT_SIMFILE_H_ */
