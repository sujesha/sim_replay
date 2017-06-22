#include <assert.h>
#include "c2pv-map.h"
#include "debugg.h"
#include "debug.h"
#include "serveio.h"
#include "fserveio.h"
#include "replay-plugins.h"
#include "scandisk.h"
#include "fscandisk.h"
#include "ioscandisk.h"
#include "v2p-map.h"
#include "mapdump.h"
#include "fmapdump.h"
#include "pdd_config.h"
#include "vmbunching_structs.h"
#include "serveio-utils.h"
#include "std_serveio.h"
#include "vector16.h"
#include "vector32.h"
#include "utils.h"
#include "v2f-map.h"
#include "pro_outputhashfn.h"
#include "pro_outputtimefn.h"
#include "unused.h"
#include "contentcache.h"
#include "d2pv-map.h"
#include "ioserveio.h"
#include "p2d-map.h"
#include "replay-defines.h"
#include "per-input-file.h"
#include "trace-struct.h"
#include "parse-generic.h"
#include "content-simfile.h"
#include "contentdedup-API.h"
#include "blkidtab-API.h"
#include <time.h>
#include "content-gen.h"

int preplayflag = 0;
int freplayflag = 0;
int sreplayflag = 1;	/* default */
int ioreplayflag = 0;

extern int scanharddiskp;		/* Scanning in parallel with replay */
extern int scanharddisks;		/* Scanning sequentially before replay */
extern int initmapfromfile;
extern int runtimemap;

extern int disksimflag;
extern int collectformat;
extern int mapdumpflag;
extern int write_enabled;       // Boolean: Enable writing
extern int read_enabled;       // Boolean: Enable reading
extern int warmupflag;

/* Extern'ed from v2p-map.c */
extern char *V2PmapFile;		/* input V2P mapping file */
extern char *default_V2PmapFile;	/* default V2P mapping file */
extern FILE *mapp;				/* FILE pointer for V2P map info */

extern char *idevnm;	// Input device name
extern pthread_t map_thread;
extern pthread_t scan_thread;	//scandisk.c
extern FILE* mapp;      		//v2p-map.c
extern int verbose;
extern vector16 * v2cmaps;				//v2c-map.c
extern vector16 * v2fmaps;				//v2f-map.c
extern vector32 * chunkmap_by_cid;		//c2pv-map.c
extern vector32 * fixedmap_by_fid;		//f2pv-map.c

extern __u64 disk_hits;
extern __u64 disk_hits_r;
extern __u64 disk_hits_w;

extern FILE * ftimeptr;
extern const char zeroarray[65537];

inline __u64 gettime(void);
int sync_disk_init(void);

/* Stats for sreplay */
#ifdef PRO_STATS
    extern unsigned long stotalreq;    /* Including read/write reqs */
    extern unsigned long stotalblk;    /* Including read/write blks */

    extern unsigned long stotalreadreq;    /* Read req received */
    extern unsigned long stotalwritereq;   /* Write req received */

    extern unsigned long stotalblkread;    /* Count of blks to-be-read */
    extern unsigned long stotalblkwrite;   /* Count of blks to-be-written */

#endif


/* Returns 1 for success */
int verify_replayflags()
{
#if defined(PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_TEST)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	if (!(preplayflag || sreplayflag || ioreplayflag || freplayflag))
	{
		printf("At least one of the replay flags should be 1\n");
		return 0;
	}

	if ((preplayflag && sreplayflag) || (sreplayflag && ioreplayflag)
		|| (ioreplayflag && preplayflag))
	{
		printf("More than 1 replay flag should not be 1 at the same time"
				" %d, %d, %d\n", preplayflag, sreplayflag, freplayflag);
		return 0;
	}

	if ((preplayflag && freplayflag) || (sreplayflag && freplayflag) 
		|| (ioreplayflag && freplayflag))
	{
		printf("More than 1 replay flag should not be 1 simultaneously\n");
		return 0;
	}

	return 1;
}

/* Returns 1 for success */
int verify_preplayflags()
{
#if defined(PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_TEST)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	if (!(scanharddiskp || scanharddisks || initmapfromfile || runtimemap))
	{
		printf("Atleast 1 of scanning/initmap/runtimemap flags should be 1\n");
		return 0;
	}

	if ((scanharddiskp && initmapfromfile) || (initmapfromfile && scanharddisks)
			|| (scanharddiskp && scanharddisks))
	{
		printf("More than 1 scan/init flag shouldnt be 1 at the same time\n");
		return 0;
	}

	if ((runtimemap && initmapfromfile) || (runtimemap && scanharddiskp)
			|| (runtimemap && scanharddisks))
	{
		printf("More than 1 scan/runtime flag shouldnt be 1 at same time\n");
		return 0;
	}

	return 1;
}

/**
 * replay_init - Initializations for replay
 */
void pdd_replay_init()
{
#if defined(PDDREPLAY_DEBUG_SS) || defined(RECLAIM_DEBUG_SS) || defined(REPLAYDIRECT_TEST)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#ifndef TESTVMREPLAY	/* will be true for regular replay */	
#ifdef SYNCIO
	/* If disk is not being simulated, ready the disk for sync access */
	if ((freplayflag || preplayflag) && !disksimflag)
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);	//assert(0);
		assert(0); 	//not expected for now
		if (sync_disk_init())
    	   	fatal(NULL, ERR_ARGS, "sync_disk_init failed for /dev/%s", idevnm);
	}
#else
	fatal(NULL, ERR_USERCALL, "async_disk_init may be needed, incomplete\n");
#endif
#endif

    /* Create V2P maps structure and voltab */
    create_v2pmaps();

	if (freplayflag)	/* Init for CONFIDED replay */
	{
		fprintf(stdout, "To perform CONFIDED replay\n");
		outputhashfn_init("freplay");
		outputtimefn_init("freplay");
#ifdef CONTENTDEDUP
		create_contentdeduptab_space("freplay");
#endif
		/* Create V2F maps structure for virt-to-fixed mapping*/
		create_v2f_mapping_space();

    	/* Create F2PV maps structure for fixed-to-virt mapping */
		create_fixedmap_mapping_space();
	}
	else if (preplayflag)	/* Init for PROVIDED replay */
	{
		fprintf(stdout, "To perform PROVIDED replay\n");
		outputhashfn_init("preplay");
		outputtimefn_init("preplay");
#ifdef CONTENTDEDUP
		create_contentdeduptab_space("preplay");
#endif

		/* Create V2C maps structure for virt-to-chunk mapping */
	    create_v2c_mapping_space();

    	/* Create C2PV maps structure for chunk-to-virt mapping */
	    create_chunkmap_mapping_space();
	}
	else if (sreplayflag)	/* Init for STANDARD or Vanilla replay */
	{
		fprintf(stdout, "To perform STANDARD replay\n");
		outputhashfn_init("sreplay");
		outputtimefn_init("sreplay");
#ifdef CONTENTDEDUP
		create_contentdeduptab_space("sreplay");
#endif
	}
	else if (ioreplayflag)	/* Init for IODEDUP replay */
	{
		fprintf(stdout, "To perform IODEDUP replay\n");
		outputhashfn_init("ioreplay");
		outputtimefn_init("ioreplay");
#ifdef CONTENTDEDUP
		create_contentdeduptab_space("ioreplay");
#endif

		/* Create P2D maps structure */
		create_p2d_mapping_space();

    	/* Create D2PV maps structure */
	    create_dedupmap_mapping_space();
	}

	/* initmapfromfile=1 indicates that dont want to do scanning again,
	 * instead want to read all mappings from the v2p, v2c and c2pv mappings' 
	 * previously-dumped files. 
	 * If scanning and not disksim, just read up the V2P map from V2PmapFile
	 */
	//if (!disksimflag && (scanharddiskp || scanharddisks))
	if (V2PmapFile)
	{
	    struct stat st;

    	if (stat(V2PmapFile, &st) < 0)
    	{
        	fatal(NULL, ERR_ARGS, "File %s does not exist\n", V2PmapFile);
    	}
	    /* Open the file for reading V2P mapping info */
    	mapp = fopen(V2PmapFile, "r");
    	if (mapp == NULL)
        	fatal(NULL, ERR_SYSCALL,
                        "Failed to open file %s for V2P map\n", V2PmapFile);

    	/* Read input V2P mapping from V2PmapFile */
	    if (read_input_v2p_mapping())
    	    EXIT_TRACE("error in read_input_v2p_mapping\n");
	}

	/* Disk is being simulated */
	if (disksimflag)
	{
		if (preplayflag)
			simdiskfn_init("preplay");
		else if (sreplayflag)
			simdiskfn_init("sreplay");
		else if (freplayflag)
			simdiskfn_init("freplay");
		else if (ioreplayflag)
			simdiskfn_init("ioreplay");

		if (runtimemap || sreplayflag) 
		{
			create_blkidtab_space();

			if (preplayflag)
			{
		    	/* Should be called before resumeChunking(), 
					so need for PROVIDED */
			    initChunking(/*rf_win_dataprocess*/);
			}
		}
	}

    if (scanharddiskp || scanharddisks)
        memset(&scan_thread, 0, sizeof(scan_thread));
    else if (initmapfromfile)
        memset(&map_thread, 0, sizeof(map_thread));

    if ((scanharddiskp || scanharddisks) && preplayflag)	/* scan */
    {
	    /* start off scanning thread for PROVIDED metadata */
    	if (pthread_create(&scan_thread, NULL, pro_scandisk_routine, NULL))
	    {
            fatal("pthread_create", ERR_SYSCALL,
    	        "pro_scandisk_routine thread create failed\n");
	    }
    }
	else if ((scanharddiskp || scanharddisks) && freplayflag)	/* scan */
    {
        /* start off scanning thread for CONFIDED metadata */
        if (pthread_create(&scan_thread, NULL, con_scandisk_routine, NULL))
        {
            fatal("pthread_create", ERR_SYSCALL,
                "con_scandisk_routine thread create failed\n");
        }
    }
	else if ((scanharddiskp || scanharddisks) && ioreplayflag)	/* scan */
    {
        /* start off scanning thread for IODEDUP metadata */
        if (pthread_create(&scan_thread, NULL, io_scandisk_routine, NULL))
        {
            fatal("pthread_create", ERR_SYSCALL,
                "io_scandisk_routine thread create failed\n");
        }
	}
    else if (initmapfromfile && preplayflag)	/* No scan */
    {
        /* start of mapping read-up thread for PROVIDED */
        if (pthread_create(&map_thread, NULL, mapreadup_routine, NULL))
        {
            fatal("pthread_create", ERR_SYSCALL,
                "mapreadup thread create failed\n");
        }
    }
    else if (initmapfromfile && freplayflag)	/* No scan */
    {
        /* start of mapping read-up thread for CONFIDED */
        if (pthread_create(&map_thread, NULL, mapreadup_routineF, NULL))
        {
            fatal("pthread_create", ERR_SYSCALL,
                "mapreadup thread create failed\n");
        }
    }
    else if (initmapfromfile && ioreplayflag)	/* No scan */
    {
		fatal(NULL, ERR_ARGS, "Initmap from file not yet ready for IODEDUP\n");
	}

    if (scanharddisks)
    {
        /* Wait for the scanning thread to exit since sequential is requested */
        if (pthread_join(scan_thread, NULL))     
        {
            fatal("pthread_join", ERR_SYSCALL, "pthread scan join failed\n");
        }

		/* After sequential scan is done, dump the mapping metadata to file */
        if (mapdumpflag && preplayflag)
        {
            if (mapdump_routine())	/* for PROVIDED */
                fatal("mapdump_routine", ERR_USERCALL, "mapdump_routine failed\n");
        }
		else if (mapdumpflag && freplayflag)
        {
            if (mapdump_routineF())	/* for CONFIDED */
                fatal("mapdump_routineF", ERR_USERCALL,"mapdump_routineF failed\n");
        }
		else if (mapdumpflag && ioreplayflag)
    	{
			fatal(NULL, ERR_ARGS, "Mapdump not yet ready for IODEDUP\n");
		}	
    }
    else if (initmapfromfile)
    {
        /* Wait for the mapping read-up thread to finish */
        if (pthread_join(map_thread, NULL))
        {
	        fatal("pthread_join", ERR_SYSCALL, "pthread map join failed\n");
        }
    }

	return;
}

/** freplay_map - Gets a vm_pkt as input, map it into io_pkt lst (iopl) after
 * 				confideReadRequest or confideWriteRequest as appropriate.
 *
 * @tip: Per-thread information
 * @iopl[out]: Output io_pkt list
 * @vmpkt[in]: The VMPkt to be mapped 
 */
int freplay_map(struct thr_info *tip, struct preq_spec **preql, 
				struct vm_pkt *vmpkt)
{
    __u32 rw;
    int numpkts = -1;
    int rc = 0; 
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif  
   
#ifdef DEBUG_SS
    assert(preql != NULL && *preql == NULL);
    assert(vmpkt != NULL && tip != NULL);
#endif

    rw = vmpkt->rw;

    if (verbose > 1)
    {
        fprintf(stdout, "vmpkt (%s, %10llu) + %10lu %c%c\n",
                        vmpkt->vmname,
                        (unsigned long long)vmpkt->block,
                        (unsigned long)vmpkt->nbytes / BLKSIZE,
                        rw ? 'R' : 'W',
                        (rw == 1 && vmpkt->rw == 0) ? '!' : ' ');
        fprintf(tip->vfp, "vmpkt (%s, %10llu) + %10lu %c%c\n",
                        vmpkt->vmname,
                        (unsigned long long)vmpkt->block,
                        (unsigned long)vmpkt->nbytes / BLKSIZE,
                        rw ? 'R' : 'W',
                        (rw == 1 && vmpkt->rw == 0) ? '!' : ' ');
    }

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "vmname=%s\n", vmpkt->vmname);
#endif

    if (!vmpkt->rw && write_enabled)
        rc = confideWriteRequest(vmpkt, preql, &numpkts);
    else if (vmpkt->rw && read_enabled)
        rc = confideReadRequest(vmpkt, preql, &numpkts);

    if (rc || ((read_enabled || write_enabled) && numpkts<0))
        fatal(NULL, ERR_USERCALL, "failed in CONFIDED read/write(%d), "
				"numpkts=%d, rc=%d\n", vmpkt->rw, numpkts, rc);

    return numpkts;
}

/* Ideally, this should be done in the VM block address space, i.e.
 * with block identified as <VM, block> but for the sake of simulation,
 * the request passed here (preql) is still in the host address space,
 * i.e., with block identified as <ioblk>. This is only a note 
 * regarding semantics, and does not impact correctness in any way.
 */
int preplay_stitch(struct preq_spec **preql, int num_vop, 
		unsigned char *simcontent)
{
	int vop_iter;
	struct preq_spec *preq;
	//savemem unsigned char stitchbuf[BLKSIZE];
	unsigned char *stitchbuf = malloc(BLKSIZE);
	unsigned int stitchbufpos = 0;

	if (disksimflag)
		assert(simcontent != NULL);

	for (vop_iter=0; vop_iter<num_vop; vop_iter++)
	{
		preq = *preql + vop_iter;
		if (stitchbufpos >= BLKSIZE)	/* out of space in block */
			RET_ERR("out of space in block\n");
		if (stitchbufpos + (preq->end- preq->start +1) > BLKSIZE)
			RET_ERR("vop_iter=%d will take out of space in block\n", vop_iter);
		if (collectformat)
		{
			unsigned char gencontent[BLKSIZE];
			unsigned char tempbuf[MD5HASHLEN_STR-1];
			memcpy(tempbuf, preq->content, MD5HASHLEN_STR-1);
			generate_BLK_content(gencontent, tempbuf, MD5HASHLEN_STR-1,BLKSIZE);
			memcpy(stitchbuf+stitchbufpos, gencontent+preq->start, 
				preq->end-preq->start+1);
		}
		else
		{
			memcpy(stitchbuf+stitchbufpos, preq->content+preq->start, 
				preq->end-preq->start+1);
		}
		stitchbufpos += (preq->end- preq->start +1);
	}

	if (stitchbufpos < BLKSIZE)
#ifdef NONSPANNING_PROVIDE
	{
		int rem = BLKSIZE - stitchbufpos;
		memcpy(stitchbuf+stitchbufpos, zeroarray, rem);
	}
#else
	{
		RET_ERR("block not fully formed\n");
	}
#endif

	if (collectformat)
	{
		unsigned char simcontent2[BLKSIZE];
		generate_BLK_content(simcontent2, simcontent, MD5HASHLEN_STR-1,BLKSIZE);
		assert(memcmp(stitchbuf, simcontent2, BLKSIZE)==0);
	}
	else
		assert(memcmp(stitchbuf, simcontent, BLKSIZE)==0);
	free(stitchbuf);
	return 0;
}

/** preplay_map - Gets a vm_pkt as input, map it into io_pkt lst (iopl) after
 * 				provideReadRequest or provideWriteRequest as appropriate.
 *
 * @tip: Per-thread information
 * @preql[out]: Output io_pkt list
 * @vmpkt[in]: The VMPkt to be mapped for read or write
 */
int preplay_map(struct thr_info *tip, struct preq_spec **preql, 
				struct vm_pkt *vmpkt)
{
	__u32 rw;
	int numpkts = -1;
	int rc = 0;
#if defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

#ifdef DEBUG_SS
	assert(preql != NULL && *preql == NULL);
	assert(vmpkt != NULL && tip != NULL);
#endif

	rw = vmpkt->rw;		/* rw=1 implies read else write */

	if (verbose > 1)
	{
		fprintf(stdout, "vmpkt (%s, %10llu) + %10lu %c\n",
						vmpkt->vmname,
						(unsigned long long)vmpkt->block,
						(unsigned long)vmpkt->nbytes / BLKSIZE,
						rw ? 'R' : 'W');
		fprintf(tip->vfp, "vmpkt (%s, %10llu) + %10lu %c\n",
						vmpkt->vmname,
						(unsigned long long)vmpkt->block,
						(unsigned long)vmpkt->nbytes / BLKSIZE,
						rw ? 'R' : 'W');
	}

	if (!vmpkt->rw && write_enabled)
		rc = provideWriteRequest(vmpkt, preql, &numpkts);	/* WRITE */
	else if (vmpkt->rw && read_enabled)
		rc = provideReadRequest(vmpkt, preql, &numpkts);	/* READ */

	if (rc || ((read_enabled || write_enabled) && numpkts<0))
		fatal(NULL, ERR_USERCALL, "failed in PROVIDED read/write(%d), "
				"numpkts=%d, rc=%d\n", vmpkt->rw, numpkts, rc);

	return numpkts;	/* Even single blk may split into multiple packets 
					 * or multiple blks can map to multiple packets */
}

/** sreplay_map - Gets a vm_pkt as input, map it into io_pkt lst (iopl) after
 * 					applying V2P map. This is default.			
 * 				 This mapping function is used for IODEDUP as well, but the
 *				statistics are to be counted only for STANDARD, so use sreplayflag.
 *
 * @tip: Per-thread information
 * @iopl[out]: Output io_pkt list
 * @vmpkt[in]: The VMPkt to be mapped 
 */
int sreplay_map(struct thr_info *tip, struct preq_spec **preql, 
				struct vm_pkt *vmpkt)
{
	int numpkts = 0;
	int rc = 0;
	UNUSED(tip);

#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

#ifdef DEBUG_SS
	assert(preql != NULL && *preql == NULL);
	assert(vmpkt != NULL && tip != NULL);
#endif 

	if (!vmpkt->rw && write_enabled)
	{
		rc = standardWriteRequest(vmpkt, preql, &numpkts);
#ifdef PRO_STATS
		if (sreplayflag)
		{
			stotalwritereq++;
			stotalblkwrite += numpkts;
		}
#endif			
	}
	else if (vmpkt->rw && read_enabled)
	{
		rc = standardReadRequest(vmpkt, preql, &numpkts);
#ifdef PRO_STATS
		if (sreplayflag)
		{
			stotalreadreq++;
			stotalblkread += numpkts;
		}
#endif			
	}
	else
	{
		RET_ERR("Neither read_enabled nor write_enabled?!\n");
	}

#ifdef PRO_STATS
	if (sreplayflag)
	{
		/* Above while loop is for a single vmbunch, containing #npkts requests */
		stotalreq++;

		/* #npkts requests are split into tot_pkts #blks */
		stotalblk += vmpkt->nbytes/BLKSIZE;
	}
#endif	

	if (rc)
		RET_ERR("failed in standard read/write(%d)\n", vmpkt->rw);

	return numpkts;
}

/** ioreplay_map - This is the basic functionality of IODEDUP read/write
 *
 * @tip: Per-thread information
 * @iopl[out]: Output io_pkt list
 * @vmpkt[in]: The VMPkt to be mapped 
 */
int ioreplay_map(struct preq_spec **preql, int vop_iter)
{
	int i=vop_iter;
    __u32 rw;
    int rc = 0;
	unsigned long long stime=0, etime=0;
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(vop_iter >= 0);
	assert(preql != NULL);
   	assert(*preql != NULL);
#endif 

	/* Handling single block (@vop_iter) at a time */

		/* If "done" flag is set, implies that request has been serviced, no
		 * need to do IODEDUP for it  */
		if ((*preql+(i))->done)
			return 0;

	    rw = (*preql+(i))->rw;
	    if (!rw && write_enabled)
		{
			stime = gettime();	/* START IODEDUP content-write time */
    	    rc = iodedupWriteRequest((*preql+(i)));
			etime = gettime();	/* END IODEDUP content-write time */
			ACCESSTIME_PRINT("ioded-content-write time: %llu %d\n",
					 etime - stime, vop_iter);
		}
	    else if (rw && read_enabled)
    	    rc = iodedupReadRequest((*preql+(i)));

	    if (rc)
    	    RET_ERR("failed in IODEDUP read/write(%d)\n", rw);

	return 0;
}

/* preql_map -- map from preql to iopl list those blocks to be fetched.
 * "done" flag in struct preq_spec indicates this block no longer needs to
 * be fetched from disk. This flag may be set in the cache simulation mode, where
 * (i) cache hit may have occurred for block read or 
 * (ii) write done already for block write (since assuming write-back cache). 
 * Only those struct preq_spec which dont have done==1, need to be mapped into
 * struct io_pkt for further processing, i.e. disk access. Note that, if 
 * cache simulation mode not requested, then the "done" flag in all blocks
 * would anyway be zero, and all disks would be copied for disk replay as usual.
 */
int preql_map(struct preq_spec **preql, struct io_pkt **iopl, int vop_iter,
				int disk_iter)
{
	int i=vop_iter, j=disk_iter;
	int num_disk = 0;
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(vop_iter >= 0);
	assert(preql != NULL);
   	assert(*preql != NULL);
	assert(iopl != NULL && *iopl == NULL);
#endif 

		/* If "done" flag is set, implies that request has been serviced, no
		 * need to go further to disk, so no need to copy it */
		if ((*preql+(i))->done)
			return 0;
		gen_realloc(*iopl, struct io_pkt, j+1);

		(*iopl+(j))->rw = (*preql+(i))->rw;
		(*iopl+(j))->ioblk = (*preql+(i))->ioblk;
		(*iopl+(j))->nbytes = (*preql+(i))->bytes;

		//if (collectformat && (!(*preql+(i))->rw || disksimflag))
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
		{
#ifdef DEBUG_SS
			assert((*preql+(i))->content != NULL);
#endif
			gen_malloc((*iopl+(j))->content, __u8, MD5HASHLEN_STR-1);
			memcpy((*iopl+(j))->content,(*preql+(i))->content, MD5HASHLEN_STR-1);
			//(*iopl+(j))->content[MD5HASHLEN_STR-1] = '\0';	no null char
		}
		//else if (!(*preql+(i))->rw || disksimflag)
		else if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY 
				|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
				|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
		{
			gen_malloc((*iopl+(j))->content, __u8, (*iopl+(j))->nbytes);
			memcpy((*iopl+(j))->content,(*preql+(i))->content, (*iopl+(j))->nbytes);
			//free done in main loop
		}
		else	/* read request and !disksimflag */
		{
			assert(0);	//not expected
			(*iopl+(j))->content = NULL;
		}
		j++;
		num_disk++;
		if (!warmupflag) {
			disk_hits++;
			if (!(*preql+(i))->rw)
				disk_hits_w++;
			else
				disk_hits_r++;
		}

//	free(*preql);	//Work over, free it
//	*preql = NULL;

	return num_disk;
}

/**             
 * next_io_tip - Retrieve next I/O trace from input stream in thread (tip)
 * @tip: Per-thread file information
 * @spec: IO specifier for trace
 *          
 * Returns TRUE if we recovered a bunch of IOs, else hit EOF
 */         
int next_io_tip(struct thr_info *tip, struct vmreq_spec *spec)
{           
    int ret = 0;
    struct record_info *r = malloc(sizeof(struct record_info));
    if (r == NULL)
        fprintf(stderr, "Unable to malloc for r\n");
                
    assert(r != NULL);
#ifdef VMBUNCH_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif      
        
    ret = next_record(tip->ifd, &r);
    if (ret == FILE_EOF)
    {
        fprintf(stdout, "reached end-of-file\n");
		free(r);	//was allocated in above malloc()
        return 0;
    }   
    else if (ret)
    {
		free(r);	//was allocated in above malloc()
        RET_ERR("next_record error in next_io_tip\n");
    }
   
	if (r->nbytes == 0)
	{
		free(r);
		RET_ERR("r->nbytes == 0, should we ignore and read next?\n");
	}
 
    spec->bytes = r->nbytes;
    spec->time = 0; //dont care r->ptime;
    strcpy(spec->vmname, r->hostname);
    /* Since piotrace reports sector_t whereas we want to deal with 
     * blockID, hence divide by 8 here to get spec->blockID
     */
    if (REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
	{
        spec->block = r->blockID >> 3;
		if (spec->block == 0)
			printf("%s: for spec->block==0, r->blockID was %u\n", 
				__FUNCTION__, r->blockID);
	}
    else
	{
		if (!warmupflag)
			assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
					DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY);
        spec->block = r->blockID;
	}
    //fprintf(stdout, "spec->bytes=%u\n", spec->bytes);

#if 0
    /* If event is none of I/O events, then read next event */
    if (strcmp(event, "OFR") && strcmp(event, "OFNW") && strcmp(event, "OFDW"))
    {
        fprintf(stdout, "found event %s, so read again\n", event);
        goto again;
    }
#endif

    if (!strcmp(r->event, "OFR"))
    {	
		assert(0);			//temporary!!!!!!!!!!!!!!!!1
        spec->rw = 1;       /* read */
        spec->content = NULL;
    }
    else if (!strcmp(r->event, "CFR"))
    {
        spec->rw = 1;       /* read */	
        copycontent(&spec->content, (__u8 *)r->dataorkey, MD5HASHLEN_STR-1);
    }
    else if (!strcmp(r->event, "CFNW"))
    {
        spec->rw = 0;       /* write */
        copycontent(&spec->content, (__u8 *)r->dataorkey, MD5HASHLEN_STR-1);
    }
    else	/* OFNW, DFR */
    {
    	if (strcmp(r->event, "DFR")==0)
	        spec->rw = 1;       /* read */
    	else if (strcmp(r->event, "OFNW")==0)
	        spec->rw = 0;       /* write */
		else 
			RET_ERR("r->event=%s is unknown\n", r->event);
        assert(r->dataorkey != NULL);
        assert(r->nbytes != 0);
//		r->dataorkey[r->nbytes] = '\0';
        copycontent(&spec->content, (__u8 *)r->dataorkey, r->nbytes);
    }

    /* r has served its purpose, so free it here */
    rfree(r);

    return 1;
}

