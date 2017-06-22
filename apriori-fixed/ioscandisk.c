/* PROVIDED module needs to scan the hard-disk of test machine, but in the
 * virtual address space (i.e. in order of vblks).
 * The scanning can be done one VM after another, or one logical volume
 * after another.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <asm/types.h>
#include <unistd.h>
#include <sys/param.h>
#include "utils.h"
#include "ioscandisk.h"
#include "sync-disk-interface.h"
#include "pdd_config.h"
#include "debug.h"
#include "p2d-map.h"
#include "v2p-map.h"
#include "iodeduping.h"
#include "unused.h"

int ioscanning = 0;
__u32 iozeros = 0;

extern const char zeroarray[65537];
extern char *idevnm;
extern volatile int signal_done;    // Boolean: Signal'ed, need to quit

/* ioscan_VM_image: scanning virtual blocks of one VM at a time.
 * The input device name is the device/partition name on which
 * this VM's image lies. We can assume that the volume partition is 
 * a logical device and the actual hard-disk is the physical device.
 * If so, we could have an additional mapping of device name
 * from virtual to physical. However, in our case, we consider
 * that both are one and the same, hence the input being sent here
 * is the physical device itself.
 * Note that, the startblk and endblk is according to the virtual
 * machine image, i.e. they correspond to virtual block numbers,
 * and a lookup of V2P map should be done to determine which actual
 * physical block is to be requested for using async I/O
 */
int ioscan_VM_image(__u16 volID, char *devname)
{
    char path[MAXPATHLEN];
	int dp;
    int rc = 0;
    __u32 start_blk, end_blk, iter;
   	__u32 ioblk;
    int bytes_read;
	unsigned char *buf = NULL;
#if defined(CONFIXING_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	/* Open the physical device to be scanned for retrieving content */
    sprintf(path, "/dev/%s", devname);
    dp = open_sync_device((char*)path);

    if (get_start_end_blk(volID, &start_blk, &end_blk))
    {
        RET_ERR("get_start_end_blk() unsuccessful. Exiting\n");
    }

#if defined(SCAN_DEBUG_SS) || defined(CONFIXING_DEBUG_SSS) || defined(PDD_REPLAY_DEBUG_SS)
    fprintf(stdout, "start_blk = %u, end_blk = %u.\n", 
			start_blk, end_blk);
#endif

    /* Should be called before resumeDeduping() */
    //initFixing(/*rf_win_dataprocess*/);		//FIXME not needed?

	buf = alloc_mem(BLKSIZE);
	if (buf == NULL)
	{
		close_sync_device(dp);
		RET_ERR("No memory to allocate buffer for reading blocks\n");
	}

	for (iter = start_blk; iter <= end_blk; iter++)
    {
        if ((iter & 0x1FF) == 0)
        {
			// This will be true every 1K blks (~4MB of data), 
			// assuming BLKSIZE = 4K 
#if defined(SCAN_DEBUG_SS) || defined(CONFIXING_DEBUG_SS)
	        fprintf(stdout,"checking scan status at blk (iter) = %llu\n", 
				(long long unsigned) iter);
#endif			
            if (signal_done == 1)
            {
				close_sync_device(dp);
                RET_ERR("signal_done = 1, so break out.\n");
            }
			usleep(1000);
        }

        /* Read the next block */
        bytes_read = pro_read_block(dp, volID, iter, buf);
#ifdef PDD_REPLAY_DEBUG_SS_DONE
		printf("bytes_read = %u\n", bytes_read);
#endif

    	if (getVirttoPhysMap(volID, iter, &ioblk))
        	VOID_ERR("getVirttoPhysMap error'ed\n");

        if (memcmp(buf, zeroarray, bytes_read) == 0)
        {
#ifdef PDD_REPLAY_DEBUG_SS_DONE
			printf("zeroblk %u\n", iter);
#endif
			iozeros++;
			rc = resumeDeduping(NULL, 0, ioblk, INIT_STAGE,
                        ZEROBLK_FLAG, 2);
			if (rc)
			{
				RET_ERR("resumeDeduping failed for Zero-block.\n");
			}
#if defined(SCAN_DEBUG_SS) || defined(CONFIXING_DEBUG_SS)
            fprintf(stdout, "Zero-block.\n");
#endif
            continue;
        }
        else
        {
#if defined(SCAN_DEBUG_SS) || defined(CONFIXING_DEBUG_SS)
            fprintf(stdout, "Non-zero block, buf = %s\n", buf);
#endif
			rc = resumeDeduping(buf, BLKSIZE, ioblk, INIT_STAGE, 
								GOODBLK_FLAG, 2);
			if (rc)
			{
				RET_ERR("resumeDeduping failed for scanning.\n");
			}
        }
    }
	close_sync_device(dp);
    return 0;
}

/* return 0 for success */
int io_scan_and_process(void)
{
	__u16 volidx;
#if defined(CONFIXING_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	for (volidx = 0; volidx < capVolumes(); volidx++)
	{
		if (volIDexists(volidx))
		{
			if (ioscan_VM_image(volidx, idevnm))
				RET_ERR("scan_VM_image error'ed\n");
		}
#ifdef SCAN_DEBUG_SS
		else
			RET_ERR("There is no volID %u\n", volidx);
#endif
	}
	return 0;
}

/* io_scandisk_routine : scanning of the test hard-disk
 * This function can be called within a thread using pthread
 */
void* io_scandisk_routine(void *arg)
{
    int rc = 0;
	UNUSED (arg);
#if defined(CONFIXING_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    /* ioscanning = 1 here to indicate scanning in progress */
    if ((rc = __sync_add_and_fetch(&ioscanning, 1)) > 1)
	{
		VOID_ERR("scanning may be already in progress? exiting\n");
		pthread_exit(NULL);
	}

    rc = io_scan_and_process();
    if (rc)
    {
        VOID_ERR("io_scan_and_process() returned error\n");
		pthread_exit(NULL);
    }

    /* Scanning done. Set scanning = 0 and wake-up other threads 
     * waiting for scanning to complete.
     */
    if (signal_done != 1 && !rc)
        fprintf(stdout, "Scanning complete\n");
    if (__sync_add_and_fetch(&ioscanning, 0) == 1)
    {
		rc = __sync_sub_and_fetch(&ioscanning, 1);
		if (rc < 0)
		{
			VOID_ERR("scanning is already not operational\n");
			pthread_exit(NULL);
		}
		/* Do not need scanning_waitq, we can just use pthread_join later. */
    }
    fprintf(stdout, "Exiting io_scandisk_routine()\n");

	pthread_exit(NULL);
}
