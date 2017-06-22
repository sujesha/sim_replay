#ifndef _V2P_MAP_H_
#define _V2P_MAP_H_

#include <asm/types.h>
#include "vmbunching_structs.h"

/* This is the expected maximum number of VMs. If more, increase this number */
#define VOLTAB_SIZE 200

struct vm_info {
	char vmname[HOSTNAME_LEN];
	__u16 volID;		/* Every VM image is in a single volume */
	__u32 vBlkID_cap;	/* Num of blks (BLKSIZE) in a single volume */
	__u32 pBlkID_base;	/* Phys base/start address for this volume */
};


/******************* PROTOYPES ********************/
__u16 capVolumes(void);
int volIDexists(__u16 volidx);

int read_input_v2p_mapping(void);
int updateVirttoPhysMap(__u16 volID, __u32 vBlkID);
int getVirttoPhysMap(__u16 volID, __u32 vBlkID, __u32 *pBlkID);
void getFirstvBlk(__u16 volID, __u32 *start);
int getLastvBlk(__u16 volID, __u32 *end);
int get_volID(char *vmname);
void free_v2pmaps(void);
void create_v2pmaps(void);
int _pro_read_block(int dp, __u32 start, unsigned char *buf);
int pro_read_block(int dp, __u16 volID, __u32 vBlkID, 
				unsigned char *buf);
int readnexttoken(char **ptr, char *sep, char **rest, char **token);

#endif /* _V2P_MAP_H_ */
