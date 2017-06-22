/* PROVIDED module needs to scan the hard-disk of test machine, but in the
 * virtual address space (i.e. in order of vblks).
 * The scanning can be done one VM after another, or one logical volume
 * after another.
 */

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <assert.h>
#include <asm/types.h>
#include "utils.h"
#include "scandisk.h"
#include "sync-disk-interface.h"
#include "pdd_config.h"
#include "debug.h"
#include "debugg.h"
#include "v2p-map.h"
#include "chunking.h"
#include "rabin.h"
#include "vmfile-handling.h"
#include "content-gen.h"
#include "unused.h"
#include "vmfile-handling.h"
#include "replay-defines.h"
#include "content-simfile.h"
#include "v2p-map.h"

int scanharddiskp = 0;		/* Scanning in parallel with replay */
int scanharddisks = 0;		/* Scanning sequentially before replay */
int initmapfromfile = 0;
pthread_t map_thread;
pthread_t scan_thread;	/* Thread for scandisk_routine */
int scanning = 0;	//TODO: use this

extern struct ifile_info *iip;
extern char *V2PmapFile;		/* input V2P mapping file */
extern FILE *writemapp;
extern const char zeroarray[65537];
extern char *idevnm;
extern volatile int signal_done;    // Boolean: Signal'ed, need to quit
extern FILE * fhashptr;
extern int collectformat;	/* MD5 hash present instead of BLKSIZE content */

int write_input_v2p_mapping(void);

/* scan_VM_image: scanning virtual blocks of one VM at a time.
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
int scan_VM_image(__u16 volID, int dp)
{
    int rc = 0;
    __u32 start_blk, end_blk, iter;
    int bytes_read;
	unsigned char *buf = NULL;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    if (get_start_end_blk(volID, &start_blk, &end_blk))
    {
        RET_ERR("get_start_end_blk() unsuccessful. Exiting\n");
    }

#if defined(SCAN_DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS) || defined(PDD_REPLAY_DEBUG_SS)
    fprintf(stdout, "start_blk = %u, end_blk = %u.\n", 
			start_blk, end_blk);
#endif

	buf = alloc_mem(BLKSIZE);
	if (buf == NULL)
	{
		RET_ERR("No memory to allocate buffer for reading blocks\n");
	}

	for (iter = start_blk; iter <= end_blk; iter++)
    {
        if ((iter & 0x1FF) == 0)
        {
			// This will be true every 1K blks (~4MB of data), 
			// assuming BLKSIZE = 4K 
#if defined(SCAN_DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
	        fprintf(stdout,"checking scan status at blk (iter) = %llu\n", 
				(long long unsigned) iter);
#endif			
            if (signal_done == 1)
			{
				free(buf);
                RET_ERR("signal_done = 1, so break out.\n");
			}
			usleep(1000);
        }

        /* Read the next block */
        bytes_read = pro_read_block(dp, volID, iter, buf);

        if (memcmp(buf, zeroarray, bytes_read) == 0)
        {
#ifdef PDD_BENCHMARK_STATS
	    	fprintf(fhashptr,
	            "%s 4096 0000 blk= %u %u\n",
    	        "zeroblkhash", volID, iter);
			if (iter == end_blk)
	    		fprintf(fhashptr, "ULTIMATE %u %u\n", volID, iter);
#endif			
			rc = resumeChunking(buf, 0, volID, iter, INIT_STAGE, 
							ZEROBLK_FLAG, NULL, 2);
			if (rc)
			{
				free(buf);
				RET_ERR("resumeChunking failed for Zero-block.\n");
			}
#if defined(SCAN_DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
            fprintf(stdout, "Zero-block.\n");
#endif
            continue;
        }
        else
        {
#if defined(SCAN_DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
            fprintf(stdout, "Non-zero block, buf = %s\n", buf);
#endif
			if (iter == end_blk)
			{
#ifdef PDD_BENCHMARK_STATS
	    		fprintf(fhashptr, "ULTIMATE %u %u\n", volID, iter);
#endif			
				rc = resumeChunking(buf, BLKSIZE, volID, iter, INIT_STAGE, 
								ULTIMATE_LASTBLK, NULL, 2);
			}
			else if (iter == start_blk)
			{
#ifdef PDD_BENCHMARK_STATS
	    		fprintf(fhashptr, "FIRST %u %u\n", volID, iter);
#endif			
				rc = resumeChunking(buf, BLKSIZE, volID, iter, INIT_STAGE, 
								SCAN_FIRSTBLK, NULL, 2);
			}
			else
				rc = resumeChunking(buf, BLKSIZE, volID, iter, INIT_STAGE, 
								GOODBLK_FLAG, NULL, 2);
			if (rc)
			{
				free(buf);
				RET_ERR("resumeChunking failed for scanning.\n");
			}
        }
    }
	free(buf);
    return 0;
}

/* pro_scan_trace_and_process: scanning of the test trace
 */
int pro_scan_trace_and_process(void)
{
	__u16 volidx;
	__u32 blkidx[10] = {0};
	__u32 iter = 0;
	__u32 ioblk;
    int rc = 0;
	int len;
	struct vmreq_spec spec;
	struct vmreq_spec next_spec;
	//savemem unsigned char buf[BLKSIZE];
	unsigned char *buf = malloc(BLKSIZE);
	spec.content = NULL;
	assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY);

    /* Should be called before resumeChunking() */
    initChunking(/*rf_win_dataprocess*/);	

#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	len = MD5HASHLEN_STR - 1;
	rc = next_io(&spec, iip);
    while (!next_io(&next_spec, iip))
	{
    	if (spec.content)
		{
			memset(buf, 0, BLKSIZE);
			generate_BLK_content(buf, spec.content, len, BLKSIZE);
			volidx = (__u16) get_volID(spec.vmname);

#if defined(SIMREPLAY_DEBUG_SS_DONE)
		    fprintf(stdout, "vmname=%s\n", spec.vmname);
			fprintf(stdout, "blk=%s\n", buf);
#endif

			/* Needed only for PROVIDED */
			if (getVirttoPhysMap(volidx, blkidx[volidx], &ioblk))
				VOID_ERR("getVirttoPhys error'ed\n");
			if (append_simcontent(ioblk, spec.content))
			{
				RET_ERR("append_simcontent failed\n");
			}

			if (iter == 0)
			{
				rc = resumeChunking(buf, BLKSIZE, volidx, blkidx[volidx], 
					INIT_STAGE,	SCAN_FIRSTBLK, NULL, 2);			
			}
			else
			{
				rc = resumeChunking(buf, BLKSIZE, volidx, blkidx[volidx], 
					INIT_STAGE,	GOODBLK_FLAG, NULL, 2);			
			}
			if (rc)
			{
				RET_ERR("resumeChunking failed for scanning\n");
			}

			if ((iter & 0x1FF) == 0)
			{
				// This will be true every 1K bits (~4MB of data),
				// assuming BLKSIZE = 4KB
				if (signal_done == 1)
				{
					RET_ERR("signal_done = 1, so break out.\n");
				}
//				usleep(1000);
			}
	        if ((iter & 0x1FFF) == 0)
    	    {
        	    fprintf(stdout, ".");   //to show progress after 131072 entries
    	        fflush(stdout);
	        }
			free(spec.content);
			spec.content = NULL;
			iter++;
			blkidx[volidx]++;

			spec = next_spec;
			spec.content = malloc(MD5HASHLEN_STR-1);
			memcpy(spec.content, next_spec.content, MD5HASHLEN_STR-1);
			free(next_spec.content);
			next_spec.content = NULL;
		}
		else
		{
			RET_ERR("why trace has no content for PROVIDED trace scan\n");
		}
	}

	if (spec.content)
	{
			memset(buf, 0, BLKSIZE);
			generate_BLK_content(buf, spec.content, len, BLKSIZE);
			volidx = (__u16) get_volID(spec.vmname);

#if defined(SIMREPLAY_DEBUG_SS_DONE)
		    fprintf(stdout, "vmname=%s\n", spec.vmname);
			fprintf(stdout, "blk=%s\n", buf);
#endif
			/* Needed only for PROVIDED */
			if (getVirttoPhysMap(volidx, blkidx[volidx], &ioblk))
				VOID_ERR("getVirttoPhys error'ed\n");
			if (append_simcontent(ioblk, spec.content))
			{
				RET_ERR("append_simcontent failed\n");
			}

			rc = resumeChunking(buf, BLKSIZE, volidx, blkidx[volidx], 
					INIT_STAGE,	ULTIMATE_LASTBLK, NULL, 2);			
			if (rc)
			{
				RET_ERR("resumeChunking failed for scanning\n");
			}

			free(spec.content);
			spec.content = NULL;
			if (updateVirttoPhysMap(volidx, blkidx[volidx]))
			{
				VOID_ERR("updateVirttoPhysMap failed (%u, %u)\n", volidx, 
						blkidx[volidx]);
			}

			if (V2PmapFile != NULL && 
				DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY)
			{
				/* If we are here, we did not read up any V2P map, but
				 * need to write V2P map to writemapp FILE
				 */
			    writemapp = fopen(V2PmapFile, "w");
			    if (writemapp == NULL)
					fatal(NULL, ERR_SYSCALL,
						"Failed to open file %s for V2P write\n", V2PmapFile);
		
		        if (write_input_v2p_mapping())
		             fatal(NULL, ERR_USERCALL, "err write_input_v2p_mapping\n");
		    }
			iter++;
			blkidx[volidx]++;
	}
	free(buf);	//savemem
	return 0;
}

/* return 0 for success */
//int pro_scan_and_process(int volID/*,struct block_device *bdev*/)
int pro_scan_and_process(char *devname)
{
	__u16 volidx;
    char path[MAXPATHLEN];
	int dp;
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);

	/* Open the physical device to be scanned for retrieving content */
    sprintf(path, "/dev/%s", devname);
    dp = open_sync_device((char*)path);

    /* Should be called before resumeChunking() */
    initChunking(/*rf_win_dataprocess*/);	

	for (volidx = 0; volidx < capVolumes(); volidx++)
	{
		if (volIDexists(volidx))
		{
			if (scan_VM_image(volidx, dp))
			{
				close_sync_device(dp);
				exitChunking();
				RET_ERR("scan_VM_image error'ed\n");
			}
		}
#ifdef SCAN_DEBUG_SS
		else
		{
			close_sync_device(dp);
			exitChunking();
			RET_ERR("There is no volID %u\n", volidx);
		}
#endif
	}

	exitChunking();
	close_sync_device(dp);
	return 0;
}

/* pro_scandisk_routine : scanning of the test hard-disk
 * This function can be called within a thread using pthread
 */
void* pro_scandisk_routine(void *arg)
{
    int rc = 0;
	UNUSED (arg);
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    /* scanning = 1 here to indicate scanning in progress */
    if ((rc = __sync_add_and_fetch(&scanning, 1)) > 1)
	{
		VOID_ERR("scanning may be already in progress? exiting\n");
		pthread_exit(NULL);
	}

	if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY)
	{
		rc = pro_scan_trace_and_process();
	}
	else
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
	    rc = pro_scan_and_process(idevnm);
	}
    if (rc)
    {
		VOID_ERR("pro_scan.*() returned error\n");
			pthread_exit(NULL);
	}

    /* Scanning done. Set scanning = 0 and wake-up other threads 
     * waiting for scanning to complete.
     */
    if (signal_done != 1 && !rc)
        fprintf(stdout, "Scanning complete\n");
    if (__sync_add_and_fetch(&scanning, 0) == 1)
    {
		rc = __sync_sub_and_fetch(&scanning, 1);
		if (rc < 0)
		{
			VOID_ERR("scanning is already not operational\n");
			pthread_exit(NULL);
		}
		/* Do not need scanning_waitq, we can just use pthread_join later. */
    }
    fprintf(stdout, "Exiting scandisk_routine()\n");

	//FIXME: do we need to keep hanging around here so that the 
	// 		pthread_join is ready to collect??

	pthread_exit(NULL);
}
