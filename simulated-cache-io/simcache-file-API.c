/* Used for both disk simulation and for 
 * cache simulation for NOT collectformat, i.e. preadwritedump traces.
 */

#include <assert.h>
#include <errno.h>
#include "debug.h"
#include "uhashtab.h"
#include "blkidtab.h"
#include "blkidtab-API.h"
#include "simdisk-API.h"
#include "content-simfile.h"
#include "replay-defines.h"

#include "simcache-file-API.h"

extern struct blkidtab blkidtab;

void cachefile_fetch(unsigned char *simcontent, char *skey, unsigned int len)
{
	blkid_datum *dedupitem=NULL;
	__u32 simdisk_blkid_dedup;

#if defined(SIMREPLAY_DEBUG_SS)
	if (strstr(skey, "1050479") || strstr(skey, "1095667"))
		printf("In %s, searchingggggggggggg skey = %s\n", 
				__FUNCTION__, skey);
#endif
	assert(simcontent != NULL);

	/* check whether block already encountered */
	dedupitem = (blkid_datum*)hashtab_search(blkidtab.table,
			            (unsigned char*)skey);
    if (dedupitem)	/* seen block */
    {
		simdisk_blkid_dedup = *((__u32*)dedupitem->data);

		/* read block from simulated disk (also cache file) */
		if (len == BLKSIZE)
			read_simdisk(simdisk_blkid_dedup, simcontent);
		else if (len == MD5HASHLEN_STR-1)
		{
			assert(0); 	//not expected for now
			assert(!(DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
				&& !(DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY));
			get_simcontent(simdisk_blkid_dedup, simcontent, 0);
		}
		else
			assert(0);	//not expected for now
	}
	else		/* new block */
	{
		VOID_ERR("cachefile_fetch failed: entry ABSENT in blkidtab\n");
	}
}
