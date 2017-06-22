#ifdef SIMULATED_DISK

#ifndef _SIMDISK_API_H_
#define _SIMDISK_API_H_


struct simdisk_blkidtab_t{
	__u32 disksim_blkid;
};

int disk_write_trap(__u16 volID, __u32 blockID, unsigned char *md5string,
					__u32 len);
int disk_read_trap(__u16 volID, __u32 blockID, unsigned char *simcontent,
					__u32 len);
int disk_read_create(__u16 volID, __u32 blockID, unsigned char *md5string,
					__u32 len);
int disk_read_checkconsistency(__u16 volID, __u32 blockID, 
			unsigned char *md5string, __u32 len);


#endif /* _SIMDISK_API_H_ */
#endif /* SIMULATED_DISK */
