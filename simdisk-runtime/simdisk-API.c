#ifdef SIMULATED_DISK

/* This file contains functions to perform disk simulation for read
 * and write requests.
 * disk_write_trap() writes block to simulated disk
 * disk_read_checkconsistency() checks that if read block already on disk, it
 * 				should be consistent.
 * disk_read_fixconsistency() : if found inconsistent, writes to sim disk.
 * disk_read_trap() assumes block has been previously written, so just reads.
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
#include "utils.h"

__u32 simdisk_blkid = 0;

extern struct blkidtab blkidtab;

int simdisk_trap(__u16 volID, __u32 blockID, unsigned char *simcontent,
		unsigned char *md5string, int rw, __u32 len, int consistencycheck)
{
	int ret;
	char skey[256];
	blkid_datum *item=NULL, *dedupitem=NULL;
	__u32 simdisk_blkid_dedup;

	if (consistencycheck == 1)
		assert(rw == 1);

	if (rw)	//read
		assert(simcontent != NULL && md5string == NULL);
	else	//write
		assert(simcontent == NULL && md5string != NULL);

	/* construct key into blkid hash-table */
	construct_key_volid_blkid(volID, blockID, skey);

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
	{
    	//savemem unsigned char debugkey[HASHLEN+1];
		unsigned char *debugkey = malloc(HASHLEN+1);
		if (rw && consistencycheck)
		{
		   	getHashKey(simcontent, BLKSIZE, debugkey);
			debugkey[HASHLEN]='\0';
			printf("In %s, searching (rw=%u) skey = %s, debugkey=%s\n", 
				__FUNCTION__, rw, skey, debugkey);
		}
		else if (!rw)
		{
		   	getHashKey(md5string, BLKSIZE, debugkey);
			debugkey[HASHLEN]='\0';
			printf("In %s, searching (rw=%u) skey = %s, debugkey=%s\n", 
				__FUNCTION__, rw, skey, debugkey);
		}
		free(debugkey);	//savemem
	}
#endif

	/* check whether block already encountered */
	dedupitem = (blkid_datum*)hashtab_search(blkidtab.table,
			            (unsigned char*)skey);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
		printf("In %s, hashtab_search done\n", __FUNCTION__);
#endif
    if (dedupitem)	/* seen block */
    {
		simdisk_blkid_dedup = *((__u32*)dedupitem->data);

		/* If consistencycheck requested, and block ID found in hashtab,
		 * read-up the block from simdisk and memcmp with incoming data!
		 */
		if (consistencycheck)
		{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
			if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
				printf("consistency check for duplicate: %u ", blockID);
#endif
			//savemem unsigned char tempbuf[BLKSIZE];
			unsigned char *tempbuf = malloc(BLKSIZE);
			if (len == BLKSIZE)
			{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
				if (blockID == 10 || blockID == 33414267 ||
					blockID == 34600770 || blockID == 10100928)
					printf(", simdisk_blkid_dedup=%u ", simdisk_blkid_dedup);
#endif

				if (read_simdisk(simdisk_blkid_dedup, tempbuf))
					RET_ERR("read_simdisk failed\n");
    			
#if defined(SIMREPLAY_DEBUG_SS_DONE)
				if (blockID == 10 || blockID == 33414267 ||
					blockID == 34600770 || blockID == 10100928)
				{
					//savemem unsigned char debugkey[HASHLEN+1];
					unsigned char *debugkey = malloc(HASHLEN+1);
		   			getHashKey(tempbuf, BLKSIZE, debugkey);
					debugkey[HASHLEN]='\0';
					printf(", retrieveddebugkey=%s ", debugkey);
					free(debugkey);	//savemem
				}
#endif
			}
			else if (len == MD5HASHLEN_STR-1)
			{
				assert(!(DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
					&& !(DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY));
				get_simcontent(simdisk_blkid_dedup, tempbuf, 0);//no gen!!
			}
			else
				assert(0);	//not expected for now
			
			if (memcmp(simcontent, tempbuf, len) == 0)
			{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
				if (blockID == 10 || blockID == 33414267 ||
					blockID == 34600770 || blockID == 10100928)
						printf("succeeded\n");
#endif
				free(tempbuf);	//savemem
				return 1;	//success in consistencycheck
			}
			else
			{
#if defined(SIMREPLAY_DEBUG_SS)
//				if (blockID == 10 || blockID == 33414267 ||
//					blockID == 34600770 || blockID == 10100928)
						//printf("ccf ");
						//printf("c");
#endif
				/* Additionally, copy consistent data into simcontent! */
				memcpy(simcontent, tempbuf, len);
				free(tempbuf);	//savemem
				return 0;	//failure in consistencycheck, but fixed
			}
		}

		/* If we are here, consistencycheck was not requested and block ID
		 * was found in hash-tab.
		 */
		if (rw)
		{
			/* in case of read request, read block from simulated disk */
			if (len == BLKSIZE)
			{
				if (blockID == 10 || blockID == 33414267 ||
					blockID == 34600770 || blockID == 10100928)
						printf("%s: simdisk_blkid_dedup=%u ", 
							__FUNCTION__, simdisk_blkid_dedup);

				if (read_simdisk(simdisk_blkid_dedup, simcontent))
					RET_ERR("read_simdisk failed here\n");
			}
			else if (len == MD5HASHLEN_STR-1)
			{
				assert(!(DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
					&& !(DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY));
				get_simcontent(simdisk_blkid_dedup, simcontent, 0);
			}
			else
				assert(0);	//not expected for now
#if defined(SIMREPLAY_DEBUG_SS)
			if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
			{
    			//savemem unsigned char debugkey[HASHLEN+1];
				unsigned char *debugkey = malloc(HASHLEN + 1);
		   		getHashKey(simcontent, BLKSIZE, debugkey);
				debugkey[HASHLEN]='\0';
				printf("%s: read simcontent (rw=%u) skey = %s, debugkey=%s\n", 
					__FUNCTION__, rw, skey, debugkey);
				free(debugkey);	//savemem
			}
#endif
		}
		else
		{
			/* in case of write request, write block to simulated disk */
			if (len == BLKSIZE)
			{
				if (blockID == 10 || blockID == 33414267 ||
					blockID == 34600770 || blockID == 10100928)
						printf("write simdisk_blkid_dedup=%u\n", 
								simdisk_blkid_dedup);

				if (write_simdisk(simdisk_blkid_dedup, md5string))
				{
					RET_ERR("write_simdisk failed for existing block\n");
				}
			}
			else if (len == MD5HASHLEN_STR-1)
			{
				assert(!(DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
					&& !(DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY));
				if (append_simcontent(simdisk_blkid_dedup, md5string))
				{
					RET_ERR("append_simcontent failed for existing block\n");
				}
			}
			else
				assert(0);
		}
	}
	else		/* new block */
	{
		unsigned char *bufptr = NULL;
		/* If we are here, consistencycheck was not requested, and
		 * block ID not found in hash-tab for read request
		 */
		if (rw && !consistencycheck)
		{
			RET_ERR("read block (%u) would have been created already\n",
					blockID);
		}

		/* If we are here, (!rw || consistencycheck) is true.
		 * If consistencycheck requested, and block ID not found in hashtab,
		 * no issue of consistency! Create it now.
		 */
		if (rw)
			bufptr = simcontent;
		else
			bufptr = md5string;

#if defined(SIMREPLAY_DEBUG_SS)
		if (consistencycheck && 
			(blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928))
			printf("requested consistency check 1st time: %u\n", blockID);
#endif
		if (len == BLKSIZE)
		{
			if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
					printf("overwrite (read/write) simdisk_blkid=%u\n", 
						simdisk_blkid);

			if (write_simdisk(simdisk_blkid, bufptr))
			{
				RET_ERR("write_simdisk failed for new block\n");
			}
		}
		else if (len == MD5HASHLEN_STR-1)
		{
			assert(!(DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
				&& !(DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY));
			if (append_simcontent(simdisk_blkid, bufptr))
			{
				RET_ERR("append_simcontent failed for new block\n");
			}
		}
		else
			assert(0);

#if defined(SIMREPLAY_DEBUG_SS)
		if (!rw && (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928))
			printf("write request for 1st time: %u\n", blockID);
#endif
		/* also add to blkid hash-table */
		item = (blkid_datum*) calloc(1, sizeof(blkid_datum));
		item->data = (__u32*)malloc(sizeof(__u32));		//free in blkidrem()

		*(__u32*)(item->data) = simdisk_blkid;
        item->blkidkey = strdup(skey);					//free in blkidrem()

#if defined(SIMREPLAY_DEBUG_SS_DONE)
		if (blockID == 10 || blockID == 33414267 ||
			blockID == 34600770 || blockID == 10100928)
			printf("In %s, inserting skey = %s, item->blkidkey=%s\n", 
				__FUNCTION__, skey, (unsigned char*)item->blkidkey);
#endif
		ret = hashtab_insert(blkidtab.table, (unsigned char*)item->blkidkey,
				item);
		if (ret == -EEXIST)
		{
			RET_ERR("block already exists in blkidtab\n");
		}
		else if (ret == -ENOMEM)
		{
			RET_ERR("out of memory for blkidtab\n");
		}

		/* increment for next block in file */
		simdisk_blkid++;

		if (consistencycheck)
			return 1;
	}

	return 0;
}

/* To be invoked when block is written to disk */
int disk_write_trap(__u16 volID, __u32 blockID, unsigned char *md5string,
					__u32 len)
{
#if defined(SIMREPLAY_DEBUG_SS)
	if (blockID == 10 || blockID == 33414267 ||
			blockID == 34600770 || blockID == 10100928)
		printf("%s: %u\n", __FUNCTION__, blockID);
#endif
	return simdisk_trap(volID, blockID, NULL, md5string, 0, len, 0);
}

/* To be invoked when block is read from disk.
 * Returns block of size BLKSIZE corresponding to (volID, blockID)
 */
int disk_read_trap(__u16 volID, __u32 blockID, unsigned char *simcontent,
					__u32 len)
{
#if defined(SIMREPLAY_DEBUG_SS)
	if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
		printf("%s: %u\n", __FUNCTION__, blockID);
#endif
	return simdisk_trap(volID, blockID, simcontent, NULL, 1, len, 0);
}

int disk_read_checkconsistency(__u16 volID, __u32 blockID, 
			unsigned char *simcontent, __u32 len)
{
#if defined(SIMREPLAY_DEBUG_SS)
	if (blockID == 10 || blockID == 33414267 ||
				blockID == 34600770 || blockID == 10100928)
		printf("%s: %u, simcont = %s\n", __FUNCTION__, blockID, simcontent);
#endif
	return simdisk_trap(volID, blockID, simcontent, NULL, 1, len, 1);
}

#endif	/* SIMULATED_DISK */
