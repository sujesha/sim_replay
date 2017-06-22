#include <asm/types.h>
#include <assert.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include "blkidtab.h"
#include "blkidtab-API.h"
#include "debug.h"
#include "utils.h"
#include "pdd_config.h"
#include "defs.h"

#define BLKIDTAB_SIZE BLKTAB_SIZE

int blkidtab_alive = 0;	/* blkid space been created or not */

extern struct blkidtab blkidtab;
extern __u32 simdisk_blkid;

/** create_blkidtab_space: Use this to initialize
 * 		This space will get freed up in the end, in free_blkidtab().
 */
void create_blkidtab_space(void)
{
	/* First, check and set the status flag for blkid space creation */
	if (blkidtab_alive == 1)
	{
		fprintf(stdout, "blkidtab already non-NULL\n");
		return;
	}
	blkidtab_alive = 1;
    fprintf(stdout, "In %s\n", __FUNCTION__);
	
	/* Initialize blkidtab */
	if (blkidtab_init(&blkidtab, BLKIDTAB_SIZE))
	{
		VOID_ERR("blkidtab_init failed\n");
		blkidtab_exit(&blkidtab);
	}
	return;
}

/** free_blkidtab -- free up the blkid mapping space, earlier created in
 * 					create_blkidtab_mapping_space().
 */
void free_blkidtab(void)
{
	__u32 i;

	/* First, reset status flag for blkid mapping space creation 
	 * This is so that the same space is not freed twice. 
	 */
	if (blkidtab_alive == 0)
	{
		fprintf(stdout, "blkidtab is not alive, exit\n");
		return;
	}
	blkidtab_alive = 0;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	for (i = 0; i < simdisk_blkid; i++)
	{
		
	}

	/* Free blkidtab hashtable */
	blkidtab_exit(&blkidtab);	
	fprintf(stdout, "done now\n");
}	

int construct_key_hostname_blkid(char *vmname, __u32 blkID, char *skey)
{
	char blkidstr[20];
	assert(skey != NULL);

	sprintf(blkidstr, "%u", blkID);

	strcpy(skey, vmname);
	strcat(skey, ":");
	strcat(skey, blkidstr);

	return 0;
}

/* Takes volID, blkID and builds a string key that can be used as 
 * key into hashtable blkidtab
 */
int construct_key_volid_blkid(__u16 volID, __u32 blkID, char *skey)
{
	char volidstr[10];
	char blkidstr[20];

	assert(skey != NULL);

	sprintf(volidstr, "%u", volID);
	sprintf(blkidstr, "%u", blkID);

	strcpy(skey, volidstr);
	strcat(skey, ":");
	strcat(skey, blkidstr);

#ifdef SIMREPLAY_DEBUG_SS_DONE
	if (blkID == 558350)
		printf("In %s, skey = %s\n", __FUNCTION__, skey);
#endif

	return 0;
}

