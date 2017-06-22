#ifdef SIM_REPLAY
//retaining above SYNCIO ifdef just to differentiate from ASYNCIO code, remove 
// asyncio code later and these ifdefs can also go... FIXME

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>
#include <time.h>
#include <sched.h>
#include <sys/time.h>
#include <fcntl.h>
#include <libaio.h>
#include <sys/prctl.h>
#include "vmbunching_structs.h"
#include "per-input-file.h"
#include "debugg.h"
#include "debug.h"
#include "pdd_config.h"
#include "replay-generic.h"
#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
	#include "replay-plugins.h"	/* will be defined everywhere else */
	#include "sync-disk-interface.h"
#endif
#include "sectorcache.h"
#include "ioserveio.h"
#include "serveio.h"
#include "fserveio.h"
#include "sync-replay-generic.h"
#include "ioruntime.h"
#include "fruntime.h"
#include "pruntime.h"
#include "v2p-map.h"
#include "replay-defines.h"
#include "vmfile-handling.h"
#include "content-gen.h"
#include "contentdedup-API.h"
#include "simdisk-API.h"

int cachesimflag = 0;	/* Simulating the sector-cache */
int writebackflag = 0;
int collectformat = 0;	/* MD5 hash present instead of BLKSIZE content */

unsigned long gtotalreadreq = 0;    /* # Read requests in trace */
unsigned long gtotalwritereq = 0;   /* # Write requests in trace */

struct ifile_info *iip = NULL;
struct ifile_info *wip = NULL;	/* file for cache warm-up */

long total_warmed = 0;
extern long total_submitted;
extern long total_warmed;
extern int warmupflag;
extern __u64 fixed_dedup_hits;
extern __u64 fixed_self_hits;
extern __u64 fixed_dedup_misses;
extern __u64 fixed_self_misses;
extern __u64 capacity_misses;
extern __u64 fmapmiss_cachehits;
extern __u64 fmapdirty_cachehits;
extern __u64 fmapmiss_cachemisses;
extern __u64 vmapmiss_cachehits;
extern __u64 vmapdirty_cachehits;
extern __u64 vmapmiss_cachemisses;

extern const char zeroarray[65537];

/* Extern'ed from replay-plugins.c */
extern int preplayflag;
extern int freplayflag;
extern int sreplayflag;    /* default */
extern int ioreplayflag;

extern volatile int signal_done;    // Boolean: Signal'ed, need to quit
extern int disksimflag;
extern int runtimemap;

extern char *ibase;      // Input base name
extern char *ifile;
extern char *idir;        // Input directory base
extern char *idevnm;	//Input device name
extern int cpus_to_use;        // Number of CPUs to use
extern int def_iterations;      // Default number of iterations
extern int verbose;         // Boolean: Output some extra info
extern int write_enabled;       // Boolean: Enable writing
extern int read_enabled;       // Boolean: Enable reading
extern __u64 genesis;      // Earliest time seen
__u64 rgenesis;          // Our start time
size_t pgsize;           // System Page size
//LIST_HEAD(input_devs);       // List of devices to handle
//LIST_HEAD(input_files);      // List of input files to handle
//LIST_HEAD(map_devs);     // List of device maps
extern int no_stalls;       // Boolean: default disabled stalls
extern int speedupfactor;	//Default: no speedup
extern int find_records;        // Boolean: Find record files auto
extern __u64 disk_hits;
extern __u64 disk_hits_r;
extern __u64 disk_hits_w;
extern unsigned char fmapdirty_flag;
extern unsigned char vmapdirty_flag;
extern unsigned char fmaphit_flag;
extern unsigned char vmaphit_flag;
extern unsigned char cmaphit_flag;
extern __u32 cmaphit_iodedupID; 
extern __u32 fmaphit_fixedID; 
extern unsigned char ccache_already_had_flag;
extern __u32 ccache_already_had_obj_ioblkID;

extern int naios;         // Number of AIOs per thread
extern int syncfd;
extern int verbose;
extern FILE * ftimeptr;
//inline __u64 gettime(void);

#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
	void iocbs_map(struct thr_info *tip, struct iocb **list,
                         struct io_pkt *pkts, int ntodo);
#else
	void iocbs_map(struct thr_info *tip, struct iocb **list,
                         struct vm_pkt *pkts, int ntodo);
#endif

void reset_input_file(struct thr_info *tip);
int nfree_current(struct thr_info *tip);

void sim_replay(struct thr_info *tip, 
                        struct io_pkt *pkts, int ntodo)
{
    int i;              
    struct io_pkt *pkt; 
#if defined (PDDREPLAY_DEBUG_SS) || defined(TESTVMREPLAY_DEBUG) || defined(SIMREPLAY_DEBUG_SS_DONE)   
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(pkts != NULL);
    assert(0 < ntodo);
#endif 
                
    pthread_mutex_lock(&tip->mutex);
    for (i = 0, pkt = pkts; i < ntodo; i++, pkt++) {
        assert(pkt != NULL);
        __u32 rw = pkt->rw;

        if (verbose > 1)
        {
            fprintf(stdout, "\t%10u + %10lu %c%c\n",
                pkt->ioblk,
                (unsigned long)(pkt->nbytes >> 12),
                rw ? 'R' : 'W',
                (rw == 1 && pkt->rw == 0) ? '!' : ' ');
            fprintf(tip->vfp, "\t%10u + %10lu %c%c\n",
                pkt->ioblk,
                (unsigned long)pkt->nbytes >> 12,
                rw ? 'R' : 'W',
                (rw == 1 && pkt->rw == 0) ? '!' : ' ');
        }

		/* Dont do actual disk read, we will just simulate it */
		/* pkt->content should already contain the hash/conten for the block
		 * that was read/written here.
		 */
		continue;
	}
	pthread_mutex_unlock(&tip->mutex);
}


#ifndef TESTVMREPLAY	/* TESTVMREPLAY defined only in pdd_testvmreplay */
/** map_n_process_bunch - Process a bunch of requests after mapping first
 *
 * @tip: Per-thread information
 * @bunch: Bunch to map and process
 */
void sim_map_n_process_bunch(struct thr_info *tip, struct vm_bunch *bunch)
{
	__u64 vmp_iter=0;
	struct io_pkt *iopl = NULL;
	struct preq_spec *preql = NULL;
	int tot_pkts = 0;

#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
    assert(0 < bunch->hdr.npkts && bunch->hdr.npkts <= MAP_BT_MAX_PKTS);

	/* If there are stalls enabled, implies that the duration of replay will
	 * be proportional to the actual play duration by a factor called 
	 * "stall factor". and if stalls are disabled, then requests executed
	 * as fast as possible.
	 * no_stalls => stalls disabled	(default)
	 * !no_stalls => stalls enabled
	 */
	if (!no_stalls)
	{
#ifdef REPLAYDIRECT_TEST
		unsigned long long amt = 18446744073709528226ULL;			
		printf("try stalling %llu\n", amt);
		stall(tip, amt);
#else		
		printf("try stalling %llu\n", bunch->hdr.time_stamp - genesis);
		stall(tip, bunch->hdr.time_stamp - genesis);
#endif
	}
	else		/* If stalls diabled, constant sleep of 10 us */
		usleep(10);	//1ms sleep on pyramid temporary, so that can work
						//parallely!

	//while (!is_send_done(tip) && (vmp_iter < bunch->hdr.npkts)) 
	while ((vmp_iter < bunch->hdr.npkts))/* Iterating thru' pkts in a bunch */
	{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "%s for vmp_iter=%llu\n", __FUNCTION__, vmp_iter);
#endif
		int num_vop = 0, vop_iter = 0;
		int disk_iter = 0;
		int /*ntodo = 0, */ num_disk = 0;	
		unsigned long long stime=0, etime=0;
#if defined(REPLAYDIRECT_TEST) || defined(SIMREPLAY_DEBUG_SS_DONE)
//		int i = 0;
		struct vm_pkt *pkt;
#endif

	if (bunch->pkts[vmp_iter].block == 10
				|| bunch->pkts[vmp_iter].block == 33414267
				|| bunch->pkts[vmp_iter].block == 34600770 
				|| bunch->pkts[vmp_iter].block == 10100928)
	{
		if (bunch->pkts[vmp_iter].rw)
			fprintf(stdout, "-------- READ REQUEST for %u (%lu) -----\n",
					bunch->pkts[vmp_iter].block, gtotalreadreq);
		else
			fprintf(stdout, "-------- WRITE REQUEST for %u (%lu) -----\n",
					bunch->pkts[vmp_iter].block, gtotalwritereq);
	}
#if defined(SIMREPLAY_DEBUG_SS_DONE)
		if (bunch->pkts[vmp_iter].block != 10 &&
				bunch->pkts[vmp_iter].block != 33414267 &&
				bunch->pkts[vmp_iter].block == 34600770 &&
				bunch->pkts[vmp_iter].block != 10100928)
		{
			if (bunch->pkts[vmp_iter].content)
				free(bunch->pkts[vmp_iter].content); //malloc in get_vmbunch()
			total_submitted++;
			vmp_iter++;
			if ((total_submitted & 0x1FFF) == 0)
			{
				fprintf(stdout, ".");	//to show progress after 131072 entries
				fflush(stdout);
			}
			continue;
		}
#endif

		/* If this is a read request and read_enabled==0 or
		 * if this is a write request and write_enabled==0, 
		 * do nothing and continue to next request.
		 */
		if ((bunch->pkts[vmp_iter].rw && !read_enabled) ||
			(!bunch->pkts[vmp_iter].rw && !write_enabled))
		{
			if (bunch->pkts[vmp_iter].content)				
				free(bunch->pkts[vmp_iter].content); //malloc in get_vmbunch()
			if (!warmupflag)
				total_submitted++;
			else
				total_warmed++;
			vmp_iter++;
			continue;	/* to next request in bunch */
		}

		if (!warmupflag) {
		if (bunch->pkts[vmp_iter].rw)	/* Read request */
			gtotalreadreq++;
		else	/* Write request */
			gtotalwritereq++;
		}

#if defined(REPLAYDIRECT_TEST) || defined(SIMREPLAY_DEBUG_SS_DONE)
		pkt = &bunch->pkts[vmp_iter];
		fprintf(stdout, "vmname=%s, vblk=%u: pblk=[\n", pkt->vmname, pkt->block);
#endif	
		//maphit_flag = 0; 	//initialize
		vmaphit_flag = 0; 	//initialize
		vmapdirty_flag = 0;	//initialize
		fmaphit_flag = 0; 	//initialize
		fmapdirty_flag = 0;	//initialize
		fmaphit_fixedID = 0;	//initialize
		cmaphit_flag = 0; 	//initialize
		cmaphit_iodedupID = 0; 	//initialize
		ccache_already_had_flag = 0;	//initialize

		stime = gettime();	/* START P2V/DEDUP mapping lookup time */
		/* Before sector-based base-cache, PROVIDED & CONFIDED perform
		 * deduplication metadata lookup, whereas IODEDUP just follows STANDARD!
		 */
		if (preplayflag)	/* dedup lookup for PROVIDED */
			num_vop = preplay_map(tip, &preql, &bunch->pkts[vmp_iter]); 
		else if (freplayflag)	/* dedup lookup for CONFIDED */
			num_vop = freplay_map(tip, &preql, &bunch->pkts[vmp_iter]); 
		else if (sreplayflag)	/* V2P lookup for STANDARD */
			num_vop = sreplay_map(tip, &preql, &bunch->pkts[vmp_iter]);
		else if (ioreplayflag)	/* V2P lookup for IODEDUP */
			num_vop = sreplay_map(tip, &preql, &bunch->pkts[vmp_iter]);
		etime = gettime();	/* END P2V/DEDUP mapping lookup time */;
		if (num_vop < 0)
			fatal(NULL, ERR_USERCALL, "Error in above map function");

		/* A single request could be a multi-block request, and/or, every
		 * single block could split into multi-block requests in PROVIDED.
		 * So, printing the time taken to lookup mapping, as well as the 
		 * actual size of request, number of block requests it results in, 
		 * and whether it is read or write request, noted only for reads
		 * but check read flag to be sure.
		 */
		if (!warmupflag) {
		if ((ioreplayflag || sreplayflag) && bunch->pkts[vmp_iter].rw)
		{
			ACCESSTIME_PRINT("V2P/dedup-mapping-lookup time: "
					"%llu z%d %d rw(%d)\n",
				 etime - stime, num_vop,
                 bunch->pkts[vmp_iter].nbytes/BLKSIZE,
				 bunch->pkts[vmp_iter].rw);
		}
		else if (preplayflag && vmaphit_flag)	//hit marked only for read
		{
			ACCESSTIME_PRINT("V2P/dedup-mapping-lookup-success time: "
					"%llu z%d %d rw(%d)\n",
				 etime - stime, num_vop,
                 bunch->pkts[vmp_iter].nbytes/BLKSIZE,
				 bunch->pkts[vmp_iter].rw);
		}
		else if (freplayflag && fmaphit_flag)	//hit marked only for read
		{
			ACCESSTIME_PRINT("V2P/dedup-mapping-lookup-success time: "
					"%llu z%d %d rw(%d)\n",
				 etime - stime, num_vop,
                 bunch->pkts[vmp_iter].nbytes/BLKSIZE,
				 bunch->pkts[vmp_iter].rw);
		}
		else if (preplayflag && !vmaphit_flag && bunch->pkts[vmp_iter].rw)
		{
			ACCESSTIME_PRINT("V2P/dedup-mapping-lookup-fail time: "
					"%llu z%d %d rw(%d)\n",
				 etime - stime, num_vop,
                 bunch->pkts[vmp_iter].nbytes/BLKSIZE,
				 bunch->pkts[vmp_iter].rw);
		}
		else if (freplayflag && !fmaphit_flag && bunch->pkts[vmp_iter].rw)
		{
			ACCESSTIME_PRINT("V2P/dedup-mapping-lookup-fail time: "
					"%llu z%d %d rw(%d)\n",
				 etime - stime, num_vop,
                 bunch->pkts[vmp_iter].nbytes/BLKSIZE,
				 bunch->pkts[vmp_iter].rw);
		}
		else if (bunch->pkts[vmp_iter].rw)
		{
			ACCESSTIME_PRINT("V2P/dedup-mapping-lookup time: "
					"%llu z%d %d rw(%d)\n",
				 etime - stime, num_vop,
                 bunch->pkts[vmp_iter].nbytes/BLKSIZE,
				 bunch->pkts[vmp_iter].rw);
		}
		}

#if defined(REPLAYDIRECT_TEST) || defined(SIMREPLAY_DEBUG_SS_DONE)
		/* For debugging purposes only, printing the start offset, end offset
		 * and the block number for every block being requested due to this
		 * requests. The start and end offsets have significant information
		 * only for PROVIDED, for all others it may be just 0 and BLKSIZE-1.
		 */
		if (!warmupflag) {
	    for (i = 0; i < num_vop; i++) 
		{	
			fprintf(stdout, "(%u,%u,%u) ", preql[i].start, preql[i].ioblk,
									preql[i].end);
		}
		fprintf(stdout, "]\n");
		}
#endif	

		/* Note that, even for 1 block per request, num_vop can be > 1 in
		 * case of PROVIDED, not for others.
		 * For multi-block requests, num_vop > 1 always.
		 * Whenever num_vop > 1, need to achieve effect of fetching 1 block
		 * after another, i.e. if blk1 to be fetched twice, first time it will
		 * be fetched into cache, second time it should be found in cache.
		 * So, handling one block at a time here. 
		 */
		for(vop_iter=0; vop_iter<num_vop; vop_iter++)
		{
			/* In cache simulation mode, -c option, we assume that the requests
			 * are being intercepted at a layer above the page cache, whereas in
			 * the non-cache simulation mode, we assume that the page cache is
			 * the underlying cache of this system itself, and hence an actual
			 * disk replay would auto look it up, i.e. no simulation of cache.
			 * Note that, cache size settings for sector-cache done only for
			 * the cache-simulation mode, default 1GB.
			 */
			if (cachesimflag && num_vop)
			{
				/* Look-up base-cache in cache simulation mode */
				if (bunch->pkts[vmp_iter].rw)	/* Read request */
				{
					/* Look-up for each blk (for given request) into base-cache.
					 * If the lookup succeeds, the "done" should be set for 
					 * those blks and only the remaining need to be 
					 * "fetched from disk". 
					 */
					int rc = 0;
					rc = find_in_sectorcache(&preql, vop_iter);
					if (rc < 0)
		                fatal(NULL, ERR_USERCALL, "Error in sectorcache_lookup\n");
					/* For a read request and metadata hit in CONFIDE, do 
					 * categorization as dedup-hit, self-hit, dedup-miss, 
					 * self-miss.
					 * Total number of bcache_hits is already counted above.
					 */
					if (!warmupflag) {	//dont count stats in warmup (begin)
					if (freplayflag && fmaphit_flag)
					{
						int ret = 0;
						__u32 orig_vblk = bunch->pkts[vmp_iter].block+vop_iter;
						if ((ret = getVolNum(&bunch->pkts[vmp_iter])) < 0)
							fatal(NULL, ERR_USERCALL, "failed getVolNum\n");
						__u16 volID = (__u16) ret;
						__u32 orig_ioblk;
						__u32 dedup_ioblk = (preql+vop_iter)->ioblk;
						if (getVirttoPhysMap(volID, orig_vblk, &orig_ioblk))
			                    VOID_ERR("getVirttoPhysMap error'ed\n");
						if (orig_ioblk == dedup_ioblk)
						{
							if ((preql+vop_iter)->done)
								fixed_self_hits++; //self-hit
							else
							{
								fixed_self_misses++; //self-miss
								//capacity_misses++;	dont count here
							}
						}
						else
						{
							if ((preql+vop_iter)->done)
								fixed_dedup_hits++; //dedup-hit
							else
							{
								fixed_dedup_misses++; //dedup-miss
								//capacity_misses++;		dont count here;
							}
						}
					}
					else if (freplayflag && !fmaphit_flag)	//wo-write-udpates
					{
                    	if ((preql+vop_iter)->done)
						{
		                	fmapmiss_cachehits++;
							if (fmapdirty_flag)
								fmapdirty_cachehits++;
						}
						else
							fmapmiss_cachemisses++;	
					}
					else if (preplayflag && !vmaphit_flag)	//wo-write-udpates
					{
                    	if ((preql+vop_iter)->done)
						{
		                	vmapmiss_cachehits++;
							if (vmapdirty_flag)
								vmapdirty_cachehits++;
						}
						else
							vmapmiss_cachemisses++;	
					}
					}	//dont count stats in warmup (end)
				}//End of lookup for reads
				else	/* Write request */
				{
					int rc = 0;
#if 0
					NOTE: dont do this, write request hits are already noted
							  in overwrite_in_sectorcache->add_to_cache()
					/* Even for a write request, do a sectorcache lookup
					 * first to note whether it is a cache hit or miss.
					 * The "done" flag is set here for write requests as well,
					 * but at a later stage, they are reset because the
					 * write requests needs to go all the way to the disk.
					 */
					rc = find_in_sectorcache(&preql, vop_iter);
					if (rc < 0)
		                fatal(NULL, ERR_USERCALL, "Error in sectorcache_lookup\n");
#endif
					/* Write to cache irrespective of hit or miss.
					 * If hit, will internally update content & cache-hits
					 * If miss, will internally evict LRU and write new content
					 */
					rc = overwrite_in_sectorcache(&preql, vop_iter);
	                if (rc < 0)
						fatal(NULL, ERR_USERCALL, "Error in overwrite_in_sectorcache\n");
				}//End of lookup+update for writes
			}//End of sectorcache lookup/access/update for read/write

#if 0
			Threading not necessary since this is simulation after all ...?
			/* In write-back mode for sector-cache, we need to spawm a thread
			 * to perform the actual write from cache to disk 
			 */
			if (writebackflag && !(bunch->pkts[vmp_iter].rw))
			{
				pthread_t cachetodisk_thread;
				if (pthread_create(&cachetodisk_thread, NULL, cachetodisk_sub, 
					bunch->pkts[vmp_iter])) {
					fatal("pthread_create", ERR_SYSCALL, "cachetodisk_sub\n");
					/* NOT REACHED */
				}
			}
			else
			{
				/* cachetodisk_sub to be performed INLINE */
				cachetodisk_sub((void*)bunch->pkts[vmp_iter]);
			}
#endif

			/* For IODEDUP writes, invalidate map
			 * For IODEDUP read/write, retrieve mapping
			 */
			if (ioreplayflag && num_vop)
			{
				/* The content-based cache simulated by default	 */
				int rc = 0;
				if (!bunch->pkts[vmp_iter].rw)
				{
					stime = gettime();	/* START IODEDUP map invalidate time */
					if (mappingTrimScanIO(&preql, vop_iter))
						fatal(NULL, ERR_USERCALL, "Error in mappingTrimScanIO\n");
					etime = gettime();	/* END IODEDUP map invalidate time */
					ACCESSTIME_PRINT("iodedmap-invalidate time: %llu %d\n",
						 etime - stime, vop_iter);
				}

				/* Perform mapping for read request and write to content-cache
				 * for write request 
				 */
				rc = ioreplay_map(&preql, vop_iter);
				if (rc < 0)
					fatal(NULL, ERR_USERCALL, "Error in ioreplay_map\n");
			}//End of IODEDUP map access/invalidate

			/* List for accessing disk for read/write requests */
		    num_disk = preql_map(&preql, &iopl, vop_iter, disk_iter);
			if (num_disk < 0)
				fatal(NULL, ERR_USERCALL, "Error in preql_map\n");
#if defined(DEBUG_SS) || defined (PDD_REPLAY_DEBUG_SS_DONE)
			if (!cachesimflag)
				assert(num_disk == 1);
			else
				assert(num_disk <= 1);
			fprintf(stdout, "vop_iter=%d, disk_iter=%d\n",
						vop_iter, disk_iter);
#endif

			/* If we simulate the disk, no need to actually read from disk */
			if (disksimflag && num_disk)
				sim_replay(tip, iopl+disk_iter, num_disk);
			else if (num_disk)	/* No disk simulation, actual disk read */
			{
				/* Only synchronous read supported right now FIXME if needed */
				sync_replay(tip, iopl+disk_iter, num_disk);	/* sync read */
	
				/* copy the content from iopl to preql == hack, can fix later */
				//num_disk <= 1 each time.
				if (bunch->pkts[vmp_iter].rw)	/* Disk read only for reads */
				{
					int j=vop_iter;
		    		struct io_pkt *pkt=iopl+disk_iter; 
					if (!(preql+(j))->done)
					{
						assert((preql+(j))->content == NULL);
						if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
							DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
							DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
							DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
						{
							/* Null char not to be copied as content */
							(preql+(j))->content = malloc(MD5HASHLEN_STR-1);
							memcpy((preql+(j))->content, pkt->content,MD5HASHLEN_STR-1);
						}
						else
						{
					assert(DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
						 || DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
						|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY);
							(preql+(j))->content = malloc(BLKSIZE);
							if ((preql+(j))->content == NULL)
							{
								VOID_ERR("malloc failed\n");
							}
							memcpy((preql+(j))->content, pkt->content, BLKSIZE);
						}
						j++;
					}
				}
			}//End of actual read from disk required, i.e. disk not simulated
			if (num_disk)
				disk_iter++;	//increment for next time

#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS_DONE)
			fprintf(stdout, "sim_map_n_process_bunch:total_submitted = %ld\n", total_submitted);
#endif
			tot_pkts += num_disk;		//num_disk = [0, 1]

			if (ioreplayflag && bunch->pkts[vmp_iter].rw)
			{
				/* ARC-cache update to be done */
				if (ioread_contentcacheupdate(&preql, vop_iter))
					fatal(NULL, ERR_USERCALL, "err in ioread_contentcacheupdate\n");
			}//End ARC cache update for IODEDUP only read

			/*
			 * In case we are building mapping at runtime, then the disk blocks
			 * that have been read from disk need to be deduplicated & mapping 
			 * updated. So for all read requests with done==0, update their
			 * mapping in the deduplication metadata. This wouldnt be needed if
			 * apriori mapping were used via scanning.
			 * Similarly, do metadata updates for CONFIDED/PROVIDED.
			 * Also, if we are simulating cache, add content to cache if needed.
			 * Note, map updates can be done as a lower priority thread, 
			 * instead of inline.
			 */
			if (bunch->pkts[vmp_iter].rw && runtimemap)	/* Read request */
			{
				int rc = 0;
				stime = gettime();	/* START IODEDUP map-update for write time */
				if (ioreplayflag)
				{
					rc = io_mapupdate(&preql, vop_iter);		//IODEDUP
					etime = gettime();	/* END IODEDUP map-update for read */
					ACCESSTIME_PRINT(
							"iodedmap-map-update-for-read time: %llu %d\n", 
							etime - stime, vop_iter);

				}
				else if (freplayflag) 							//CONFIDED
				{
					rc = f_mapupdate(&preql, &bunch->pkts[vmp_iter], vop_iter);
					etime = gettime();	/* END CONFIDED map-update for read */
					ACCESSTIME_PRINT(
							"confided-map-update-for-read time: %llu %d\n", 
							etime - stime, vop_iter);
				}
				else if (preplayflag)							//PROVIDED
				{
					/* For non-spanning PROVIDED, following works whereas...
					 * For spanning PROVIDED, this is just a stub, 
					 * the actual update is 
					 * done within provideWriteRequest() itself. FIXME.
					 * Since this is variable-sized chunking and mapping,
					 * it requires inputs like pre-chunk and post-chunk for
					 * re-chunking and mapping update. Howeever, this requires
					 * that the previous block(s) and next block(s) data
					 * should not changed (maybe locked?) while this update
					 * is in progress, and if so, this may not be possible
					 * in an asynchronous setting? So, the 2 options are:-
					 * 1. map-update in write-path => provideWriteRequest
					 * 2. map-update with just current block data, nothing else
					 * Currently 1 is being followed. 
					 * If 2 is accomplished, can fix the p_mapupdate_sub() call
					 * below to no longer be just a stub.
					 */
					rc = p_mapupdate(&preql, &bunch->pkts[vmp_iter], vop_iter);
					etime = gettime();	/* END PROVIDED map-update for read */
					ACCESSTIME_PRINT(
							"provided-map-update-for-read time: %llu %d\n", 
							etime - stime, vop_iter);
				}
				if (rc < 0)
					fatal(NULL, ERR_USERCALL, "Error in above mapupdate\n");
			}//End of read request runtime map update
			//version 1: MASCOTS had write-request update
			//version 2: Apr 1, 2014: no map update upon writes
			else if (!bunch->pkts[vmp_iter].rw)	/* Write request map update */
			{
				//FIXME: Do we need to set low priority for this thread here?
				struct mapupdate_info mip; // = malloc(sizeof(struct mapupdate_info));
				mip.preql = &preql;
				mip.blkReq = &(bunch->pkts[vmp_iter]);
				mip.vop_iter = vop_iter;

				/* If we are simulating disk, we need to write the data
				 * to simulated disk, before trying to update metadata here
				 */
				__u16 writevolidx = get_volID(bunch->pkts[vmp_iter].vmname);
				__u32 writelen = 0;
				if (collectformat)
					writelen = MD5HASHLEN_STR-1;
				else
					writelen = BLKSIZE;
				if (disk_write_trap(writevolidx, bunch->pkts[vmp_iter].block,
					bunch->pkts[vmp_iter].content, writelen))
				{
					EXIT_TRACE("disk_write_trap failed\n");
				}

#ifdef IOMETADATAUPDATE_UPON_WRITES
				if (ioreplayflag)
				{
					/* Implementation of IODEDUP map update */
				    if (pthread_create(&mip.upd_thread, NULL, io_mapupdate_sub,
										   	&mip))
					{
	        			fatal("pthread_create", ERR_SYSCALL,
		            		"io_mapupdate_sub thread create failed\n");
	        				/*NOTREACHED*/
    				}
				}
#endif
#ifdef METADATAUPDATE_UPON_WRITES
				if (freplayflag) 							//CONFIDED
				{
					/* Implementation of CONFIDED map update */
			    	if (pthread_create(&mip.upd_thread, NULL, f_mapupdate_sub, 
											&mip)) 
					{
        				fatal("pthread_create", ERR_SYSCALL,
			            	"f_mapupdate_sub thread create failed\n");
		        			/*NOTREACHED*/
	    			}
				}
				else if (preplayflag)							//PROVIDED
				{
					/* This is just a stub right now, the actual update is 
					 * done within provideWriteRequest() itself. FIXME
					 * Same as before.
					 */
				    if (pthread_create(&mip.upd_thread, NULL, p_mapupdate_sub, 
											&mip)) 
					{
        				fatal("pthread_create", ERR_SYSCALL,
		    	        	"p_mapupdate_sub thread create failed\n");
	    	    			/*NOTREACHED*/
    				}
				}
#endif

#if defined(IOMETADATAUPDATE_UPON_WRITES) && defined(METADATAUPDATE_UPON_WRITES)
				if (ioreplayflag || freplayflag || preplayflag)
#elif defined(IOMETADATAUPDATE_UPON_WRITES)
				if (ioreplayflag)
#elif defined(METADATAUPDATE_UPON_WRITES)
				if (freplayflag || preplayflag)
#endif
#if defined(IOMETADATAUPDATE_UPON_WRITES) || defined(METADATAUPDATE_UPON_WRITES)
				{
		        	/* Wait for the map updating thread to finish */
			        if (pthread_join(mip.upd_thread, NULL))
    	    		{
    			        fatal("pthread_join", ERR_SYSCALL, "pthread map join failed\n");
	    	    	}
				}
#endif
			}//End of write request (and map, optionally) update
		
			/* For all read blocks that were "from disk" or from content cache
			*  i.e. bcachefound = 0
			 * perform cache add for the new content. 
			 * Performing deduplication and updating metadata for runtimemap
			 * was already done above.
			 */
			if (cachesimflag)
			{
				/* Look-up base-cache in cache simulation mode */
				if (bunch->pkts[vmp_iter].rw)	/* Read request */
				{
					/* update the base-cache with new value */
					/* if a new block is being added, corresponding blk will
					 * not be present in base-cache, hence may result in some 
					 * eviction 
 					 */
					int rc = 0;
					rc = overwrite_in_sectorcache(&preql, vop_iter);//FIXME
	                if (rc < 0)
    	                fatal(NULL, ERR_USERCALL, "overwrite_in_sectorcache\n");
				}
			}//End of performing sectorcache add for new content per block

		}//End of handling one block at a time.

		/* If this is a PROVIDED read request, we have to stitch the buffer.*/
		if (preplayflag && bunch->pkts[vmp_iter].rw)
		{
			if (preplay_stitch(&preql, num_vop, bunch->pkts[vmp_iter].content))
				fatal(NULL, ERR_USERCALL, "preplay_stitch failed\n");
		}
#if 0
        /* If we are NOT in writeback mode, or this is a READ request, 
         * note the "response time" all the way till here because for all
		 * read requests and for all write-through cache write requests, 
		 * this does contribute to user-perceived response time.
         */
        if (!writebackflag || bunch->pkts[vmp_iter].rw)
        {
            etime = gettime();
            ACCESSTIME_PRINT("%llu z%d %d rw(%d)\n", etime - stime, num_vop,
                                bunch->pkts[vmp_iter].nbytes/BLKSIZE,
                                bunch->pkts[vmp_iter].rw);
        }
#endif

		/* Freeing all the malloc'ed data-structures not needed beyond
		 * this point.
		 */
		if (bunch->pkts[vmp_iter].content)
		{
			free(bunch->pkts[vmp_iter].content); //malloc in get_vmbunch()
		}
		if (preql)
		{
			for (vop_iter=0; vop_iter<num_vop; vop_iter++)
			{
				assert((preql+(vop_iter))->content != NULL);
				free((preql+(vop_iter))->content);
				if ((preql+(vop_iter))->blkidkey)
					free((preql+(vop_iter))->blkidkey);//strdup:create_preq_spec
			}
			free(preql);	preql = NULL;	/* Work over, free it */
		}
		if (iopl)
		{
			for (vop_iter=0; vop_iter<num_disk; vop_iter++)
			{
				assert((iopl+(vop_iter))->content != NULL);
				free((iopl+(vop_iter))->content);
			}
			free(iopl);	iopl = NULL;	/* Work over, free it */
		}

		vmp_iter++;	/* Next pkt in bunch */
		if (!warmupflag)
		{
			total_submitted++;
			if ((total_submitted & 0x1FFF) == 0)
			{
				fprintf(stdout, ".");	//to show progress after 131072 entries
				fflush(stdout);
			}
		}
		else
		{
			total_warmed++;
			if ((total_warmed & 0x1FFF) == 0)
			{
				fprintf(stdout, ".");	//to show progress after 131072 entries
				fflush(stdout);
			}
		}

#ifdef CONTENTDEDUP
	print_total_uniq_items();
#endif
	}//End of iterating thru pkts in a bunch
}
#else	/* will be true only in testvmreplay */
/**
 * process_bunch - Process a bunch of requests
 * @tip: Per-thread information
 * @bunch: Bunch to process
 */
//FIXME: this is incomplete ==== it is async version still, not sync
void sync_process_bunch(struct thr_info *tip, struct vm_bunch *bunch)
{
    __u32 i = 0;
    struct iocb *list[bunch->hdr.npkts];
    fprintf(stdout, "In %s: this is incomplete ==== it is async version still, not sync\n", __FUNCTION__);

#if defined(TESTVMREPLAY_DEBUG)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    assert(0 < bunch->hdr.npkts && bunch->hdr.npkts <= BT_MAX_PKTS);
    while (!is_send_done(tip) && (i < bunch->hdr.npkts)) {
#if defined(TESTVMREPLAY_DEBUG)
    	fprintf(stdout, "In while loop in %s\n", __FUNCTION__);
#endif
        long ndone;
        int ntodo = min(nfree_current(tip), bunch->hdr.npkts - i);

        assert(0 < ntodo && ntodo <= naios);
        iocbs_map(tip, list, &bunch->pkts[i], ntodo);
        if (!no_stalls)
            stall(tip, bunch->hdr.time_stamp - genesis);
		else
			usleep(1000*10);

        if (ntodo) {
            if (verbose > 1)
            {
                fprintf(stdout, "submit(%d)\n", ntodo);
                fprintf(tip->vfp, "submit(%d)\n", ntodo);
            }
            ndone = io_submit(tip->ctx, ntodo, list);
            if (ndone != (long)ntodo) {
                fatal("io_submit", ERR_SYSCALL,
                    "io_submit(%d:%ld) failed (%s)\n",
                    ntodo, ndone,
                    strerror(labs(ndone)));
                /*NOTREACHED*/
            }

            pthread_mutex_lock(&tip->mutex);
            tip->naios_out += ndone;
            assert(tip->naios_out <= naios);
            if (tip->reap_wait) {
                tip->reap_wait = 0;
                pthread_cond_signal(&tip->cond);
            }
            pthread_mutex_unlock(&tip->mutex);

            i += ndone;
#ifdef PRO_STATS
			total_submitted += ndone;
			fprintf(stdout, "xxxtal_submitted = %ld\n", total_submitted);
#endif
            assert(i <= bunch->hdr.npkts);
        }
    }
}
#endif	/* process_bunch only present in testvmreplay */

#if 0
/* Assumes that entire bunch is read or, entire bunch is write. This is the
 * case when traces are from preadwritedump or pcollect
 * However, the very old case of bunching up records based on timestamps
 * such that multiple reads and writes may be collated into a single bunch,
 * will not obey this assumption. If such replays need to be done, this
 * function may need to be fixed.
 */
void update_simdisk_writes(struct vm_bunch *bunch)
{
	unsigned char potbuf[BLKSIZE];
	static __u16 volidx;
	__u32 numblks;
	__u32 iter, actualiter=0;
	__u32 len = 0;

	if (bunch->pkts[0].rw)	/* Ignore reads here, already done */
		return;

	/* If we are here, this is a write request */
 	numblks = bunch->hdr.npkts;
	if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
		len = BLKSIZE;
	else
		len = MD5HASHLEN_STR-1;

	for(iter=0; iter<numblks; iter++)
	{
		/* To eliminate zero blocks from replay */
		volidx = (__u16) get_volID(bunch->pkts[actualiter].vmname);
		assert(bunch->pkts[actualiter].content != NULL);
		if (len == BLKSIZE)
		{
			memcpy(potbuf, bunch->pkts[actualiter].content, BLKSIZE);
    	    if (memcmp(potbuf, zeroarray, BLKSIZE) == 0)
			{
				//printf("ignore zero block at iter=%u, continue loop\n", iter);
				continue;	
			}
		}

		if (disk_write_trap(volidx, bunch->pkts[actualiter].block,
			bunch->pkts[actualiter].content, len))
		{
			EXIT_TRACE("disk_write_trap failed\n");
		}
	
		free(bunch->pkts[actualiter].content); //from convert_spec_to_bunch()
		actualiter++;
	}

	/* Since we had already eliminated zero blocks from the bunch, there
	 * should be no zero blocks in the bunch => iter == actualiter 
	 */
	 assert(iter == actualiter);
}
#endif

void convert_spec_to_bunch(struct vmreq_spec *spec, struct vm_bunch *bunch)
{
	//savemem unsigned char potbuf[BLKSIZE];
	unsigned char *potbuf = malloc(BLKSIZE);
	static __u16 volidx;
	static __u32 blkidx[20] = {0};
	__u32 numblks;
	__u32 iter, actualiter=0;
	bunch->hdr.npkts=0;
	__u32 len = 0;

 	numblks = spec->bytes >> 12;
	if (spec->block == 0)
		printf("%s: we have spec->block == 0!\n", __FUNCTION__);

	if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
		len = BLKSIZE;
	else
		len = MD5HASHLEN_STR-1;

	volidx = (__u16) get_volID(spec->vmname);
	if (spec->bytes == BLKSIZE)
		assert(numblks == 1);
	else
		assert(numblks > 1);

	for(iter=0; iter<numblks; iter++)
	{
#if defined(SIMREPLAY_DEBUG_SS_DONE)
//		if (spec->block+iter == 558350)
	    fprintf(stdout, "In %s, numblks = %u, rw=%d, iter = %u\n", 
				__FUNCTION__, numblks, spec->rw, iter);
#endif
		/* To eliminate zero blocks from replay */
		if (len == BLKSIZE)
		{
			memcpy(potbuf, spec->content + iter*BLKSIZE, BLKSIZE);
    	    if (memcmp(potbuf, zeroarray, BLKSIZE) == 0)
			{
				//printf("ignore zero block at iter=%u, continue loop\n", iter);
				continue;	
			}
		}

		/* Proceed for non-zero block */
		strcpy(bunch->pkts[actualiter].vmname, spec->vmname);
		bunch->pkts[actualiter].nbytes = BLKSIZE;
		if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY)
			bunch->pkts[actualiter].rw = 1;
		else
		{
			assert(warmupflag || 
				DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY);
			bunch->pkts[actualiter].rw = spec->rw;
		}
	    if (spec->content != NULL)
    	{
        	if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY)
	        {
				assert(numblks == 1);
				bunch->pkts[actualiter].block = blkidx[volidx];	//just sequential
        	    bunch->pkts[actualiter].content = malloc(BLKSIZE);
				memset(bunch->pkts[actualiter].content, 0, BLKSIZE);
				generate_BLK_content(bunch->pkts[actualiter].content, spec->content, 
					MD5HASHLEN_STR-1, BLKSIZE);
        	}
			else if (DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
			{
				bunch->pkts[actualiter].block = spec->block+iter;	//retain block IDs 
            	bunch->pkts[actualiter].content = malloc(BLKSIZE);
				if (bunch->pkts[actualiter].content == NULL)
					VOID_ERR("malloc failed\n");
				memset(bunch->pkts[actualiter].content, 0, BLKSIZE);
				assert(spec->content + iter*BLKSIZE != NULL);
				assert(bunch->pkts[actualiter].content != NULL);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
				if (numblks == 50)
				{
					__u32 crazy;
					for (crazy=0; crazy < BLKSIZE; crazy++)
					{
						assert(spec->content + iter*BLKSIZE + crazy != NULL);
						assert(bunch->pkts[actualiter].content+crazy !=NULL);
						if (iter == 2 && crazy > 1100)
						{
						printf("crazy=%u\n", crazy);
						printf("%p %c\n", spec->content + iter*BLKSIZE + crazy, *(spec->content + iter*BLKSIZE + crazy));
						}
					}
					printf("bunch->pkts[actualiter].content addr = %p\n", 
							bunch->pkts[actualiter].content);
					printf("spec->content addr = %p\n", spec->content);
					printf("spec->content + (iter*BLKSIZE) addr = %p\n",
							spec->content + (iter*BLKSIZE));
				}
#endif
        	    memcpy(bunch->pkts[actualiter].content, 
					spec->content + (iter*BLKSIZE), BLKSIZE);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
				if (bunch->pkts[actualiter].block == 558350)
				fprintf(stdout, "%s: bunch->pkts[%u].content = %s, "
						"spec->block = %u\n", __FUNCTION__, 
					actualiter, bunch->pkts[actualiter].content, spec->block);
#endif
			}
#if 0
			else if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY &&
					preplayflag)	//PROVIDED replay+warmup in runtime
			{
				bunch->pkts[actualiter].block = spec->block+iter;	//retain block IDs 
            	bunch->pkts[actualiter].content = malloc(BLKSIZE);
				if (bunch->pkts[actualiter].content == NULL)
					VOID_ERR("malloc failed\n");
				memset(bunch->pkts[actualiter].content, 0, BLKSIZE);
				generate_BLK_content(bunch->pkts[actualiter].content, spec->content, 
					MD5HASHLEN_STR-1, BLKSIZE);
				printf("generate_BLK_content for block %u\n", bunch->pkts[actualiter].block);		//temporary!!!!!!!!!!!!!!!!
			}
#endif
			else if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY || 
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY || warmupflag
					||	DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
						DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)
        	{
				assert(numblks == 1);
				bunch->pkts[actualiter].block = spec->block;
	            bunch->pkts[actualiter].content = malloc(MD5HASHLEN_STR-1);
    	        memcpy(bunch->pkts[actualiter].content, spec->content, 
						MD5HASHLEN_STR-1);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
				printf("%s:bunch content in %u pkt = %s\n", __FUNCTION__,
					actualiter, bunch->pkts[actualiter].content);
#endif
	        }
			else
			{
				assert(0);	//not expected
			}
//        free(spec->content);    //to prevent memory leak
	    }
    	else
        	bunch->pkts[actualiter].content = NULL;

		if (updateVirttoPhysMap(volidx, bunch->pkts[actualiter].block))
		{
			VOID_ERR("%s: updateVirttoPhysMap failed (%u, %u)\n", __FUNCTION__,
					volidx, bunch->pkts[actualiter].block);
		}

		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
			|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
		{
			if (bunch->pkts[actualiter].rw)
			{
				//printf("going for disk_read_checkconsistency\n");
				if(disk_read_checkconsistency(volidx, 
							bunch->pkts[actualiter].block,
						   	bunch->pkts[actualiter].content, len)==0)
				{
					/* disk_read_checkconsistency succeeds in 2 cases:-
					 * 1. block present and consistent
					 * 2. block not present at all, and created.
					 * In both cases, things are fine. 
					 * disk_read_checkconsistency fails if block present
					 * but not consistent. It gets fixed, so that following
					 * assert can succeed.
					 */
				//fprintf(stdout, "Trace found inconsistent, but fixed!\n");
					assert(disk_read_checkconsistency(volidx, 
						bunch->pkts[actualiter].block, 
						bunch->pkts[actualiter].content, len)==1);
				}
			}
			else
			{
				/* Write to disk done later --- especially required when
				 * bunch has multiple blocks
				 */
#if 0
				if (bunch->pkts[actualiter].block == 10 ||
						bunch->pkts[actualiter].block == 33414267 ||
						bunch->pkts[actualiter].block == 34600770 ||
						bunch->pkts[actualiter].block == 10100928)
					printf("going for disk_write_trap itself\n");
				assert(bunch->pkts[actualiter].content != NULL);
				if (disk_write_trap(volidx, bunch->pkts[actualiter].block,
							bunch->pkts[actualiter].content, len))
				{
					EXIT_TRACE("disk_write_trap failed\n");
				}
#endif
			}
		}	
#if defined(SIMREPLAY_DEBUG_SS)
		if (spec->block+iter == 10 ||
			spec->block+iter == 33414267 ||
			spec->block+iter == 34600770 ||
			spec->block+iter == 10100928)
		{
		    fprintf(stdout, "In %s, numblks = %u, rw=%d done\n", 
				__FUNCTION__, numblks, spec->rw);
			if (actualiter != iter)
				printf("%s: some (%u) zero blocks eliminated from %u\n", 
					__FUNCTION__, iter-actualiter, iter);
		}
#endif

		blkidx[volidx]++;
		bunch->hdr.npkts++;
		actualiter++;
	}
#if 0
	if (spec->block+iter == 10 ||
			spec->block+iter == 33414267 ||
			spec->block+iter == 34600770 ||
			spec->block+iter == 10100928)
		fprintf(stdout, "Leaving %s only now for %u!\n", __FUNCTION__,
				spec->block+iter);A
#endif
	free(potbuf);
}

void *sim_warmup_sub(void *arg)
{
    char path[MAXPATHLEN];

	/* Get the data passed to this thread */
    struct thr_info *wtip = arg;
#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	/* If disk is not being simulated, open the disk for read/write */
	if (!disksimflag)
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
	    sprintf(path, "/dev/%s", idevnm);
		wtip->ofd = open_sync_device((char*)path);
#if defined(TESTVMREPLAY_DEBUG) || defined (PDDREPLAY_DEBUG_SS)  || defined(SIMREPLAY_DEBUG_SS)
		fprintf(stdout, "Device is %s\n", path);
#endif
	}

	/* This loop can be used to perform multiple iterations of same replay */
    while (wtip->iterations--) {
		/* Until not finito and requests still remaining, continue */
		struct vmreq_spec spec;
		struct vm_bunch bunch;
        while (!is_send_done(wtip) && next_io_tip(wtip, &spec))
		{
			/* Convert the spec to bunch with single packet */
//			fprintf(stdout, "spec.content=%s\n", spec.content);
//			bunch.pkts[0].content = NULL;
			if ((spec.rw == 1 && !read_enabled)
				|| (spec.rw == 0 && !write_enabled))
			{
				free(spec.content);
				spec.content = NULL;
				continue;
			}
			convert_spec_to_bunch(&spec, &bunch);
			/* bunch->hdr.npkts could be 0 due to ignored zero blocks above. */
			if (bunch.hdr.npkts > 0)
            	sim_map_n_process_bunch(wtip, &bunch);	/* replay per bunch */

			free(spec.content);
			spec.content = NULL;
		}
	}
	fprintf(stdout, "outside warmup while loop\n");
    wtip->send_done = 1;
	return NULL;
}

/**
 * sim_replay_sub - Worker thread to submit requests that are being replayed
 * 				in the simulation module
 */
void *sim_replay_sub(void *arg)
{
    char path[MAXPATHLEN];

	/* Get the data passed to this thread */
    struct thr_info *tip = arg;
#if defined (PDDREPLAY_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	/* If disk is not being simulated, open the disk for read/write */
	if (!disksimflag)
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
	    sprintf(path, "/dev/%s", idevnm);
		tip->ofd = open_sync_device((char*)path);
#if defined(TESTVMREPLAY_DEBUG) || defined (PDDREPLAY_DEBUG_SS)  || defined(SIMREPLAY_DEBUG_SS)
		fprintf(stdout, "Device is %s\n", path);
#endif
	}

    //pin_to_cpu(tip);		/* dont bother about cpu pinning for now */
	prctl(PR_SET_NAME,"simreplay",0,0,0);	/* setting thread name */
    set_replay_ready();		/* signaling thread is ready for replay */
#if defined(SIMREPLAY_DEBUG_SS)
	fprintf(stdout, "is_send_done(tip)=%d, iterations=%d\n", 
					is_send_done(tip), tip->iterations);
#endif

	/* This loop can be used to perform multiple iterations of same replay */
    while (tip->iterations--) {
        wait_iter_start();	/* wait for signal by start_iter() */
		if (!DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY &&
				!DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY &&
				!DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY &&
				!DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY &&
				!DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
		{
			struct vm_bunch *bunch = calloc(1, sizeof(struct vm_bunch));
			assert(DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
					sreplayflag);
			/* Until not finito and bunches still remaining, continue */
    	    while (!is_send_done(tip) && next_bunch(tip, bunch))
			{
#ifdef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
				sync_process_bunch(tip, bunch);
#else
    	        sim_map_n_process_bunch(tip, bunch);	/* replay per bunch */
#endif
			}
			free(bunch);
		}
		else
		{
			/* Until not finito and requests still remaining, continue */
			struct vmreq_spec spec;
			struct vm_bunch bunch;
			int numtempreq = 0;		//temporary!!!!!
    	    while (!is_send_done(tip) && next_io_tip(tip, &spec) && 
				numtempreq >= 0)		//temporary!!!!!
			{
				/* Convert the spec to bunch with single packet */
//				fprintf(stdout, "spec.content=%s\n", spec.content);
//				bunch.pkts[0].content = NULL;
				if ((spec.rw == 1 && !read_enabled)
					|| (spec.rw == 0 && !write_enabled))
				{
					free(spec.content);
					spec.content = NULL;
					continue;
				}
				convert_spec_to_bunch(&spec, &bunch);
				/* bunch->hdr.npkts could be 0 due ignored zero blocks above. */
				if (bunch.hdr.npkts > 0)
    		        sim_map_n_process_bunch(tip, &bunch);	/* replay bunch */
#if 0
				if (disksimflag)
					update_simdisk_writes(&bunch);
#endif
				free(spec.content);
				spec.content = NULL;
				numtempreq++;
			}
		}
        set_iter_done();	/* Signals done to wait_iters_done() */
        reset_input_file(tip);		/* Reset for next iter */
    }
	fprintf(stdout, "outside while loop\n");
    tip->send_done = 1;
    set_replay_done();	/* Send signal to wait_replays_done() */
#ifdef PRO_STATS
	fprintf(stdout, "sim_replay_sub:total_submitted = %ld\n", total_submitted);
#endif

	/* If disk is not being simulated, close the disk that we opened */
	if (!disksimflag)
	{
		assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
		close(tip->ofd);	
	}
    return NULL;
}

#endif /* SYNCIO */
