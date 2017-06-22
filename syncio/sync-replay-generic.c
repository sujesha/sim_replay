#ifdef SYNCIO

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
#include "replay-defines.h"

extern volatile int signal_done;    // Boolean: Signal'ed, need to quit
extern int warmupflag;

extern char *ibase;      // Input base name
extern char *ifile;
extern char *idir;        // Input directory base
extern char *idevnm;	//Input device name
extern int cpus_to_use;        // Number of CPUs to use
extern int def_iterations;      // Default number of iterations
extern int verbose;         // Boolean: Output some extra info
extern int write_enabled;       // Boolean: Enable writing
__u64 genesis = ~0;      // Earliest time seen
__u64 rgenesis;          // Our start time
size_t pgsize;           // System Page size
//LIST_HEAD(input_devs);       // List of devices to handle
//LIST_HEAD(input_files);      // List of input files to handle
//LIST_HEAD(map_devs);     // List of device maps
int no_stalls = 1;       // Boolean: default disabled stalls
int speedupfactor = 1;	//Default: no speedup
int find_records = 0;        // Boolean: Find record files auto
long total_submitted = 0;

extern int naios;         // Number of AIOs per thread
extern int syncfd;
extern int verbose;
extern FILE *ftimeptr;
extern int disksimflag;

//inline __u64 gettime(void);

/* Stats for sreplay */
#ifdef PRO_STATS
    extern unsigned long stotalreq;    /* Including read/write reqs */
    extern unsigned long stotalblk;    /* Including read/write blks */

    extern unsigned long stotalreadreq;    /* Read req received */
    extern unsigned long stotalwritereq;   /* Write req received */

    extern unsigned long stotalblkread;    /* Count of blks to-be-read */
    extern unsigned long stotalblkwrite;   /* Count of blks to-be-written */

#endif


#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
	/* Extern'ed from replay-plugins.c */
	extern int preplayflag;
	extern int freplayflag;
	extern int sreplayflag;    /* default */
	extern int ioreplayflag;

	void iocbs_map(struct thr_info *tip, struct iocb **list,
                         struct io_pkt *pkts, int ntodo);
#else
	void iocbs_map(struct thr_info *tip, struct iocb **list,
                         struct vm_pkt *pkts, int ntodo);
#endif

void reset_input_file(struct thr_info *tip);
int nfree_current(struct thr_info *tip);

void sync_replay(struct thr_info *tip, 
                        struct io_pkt *pkts, int ntodo)
{
    int i;              
    struct io_pkt *pkt; 
#if defined (PDDREPLAY_DEBUG_SS) || defined(TESTVMREPLAY_DEBUG) || defined(REPLAYDIRECT_DEBUG_SS)   
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(pkts != NULL);
    assert(0 < ntodo);
#endif 
	assert(REALDISK_SCANNING_NOCOLLECTFORMAT_VMBUNCHREPLAY);
                
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

		if (rw)
		{
			pkt->content = malloc(pkt->nbytes);
			_do_bread(tip->ofd, pkt->ioblk, pkt->nbytes, pkt->content);
		}
		else
			_do_bwrite(tip->ifd, pkt->ioblk, pkt->nbytes, pkt->content);
	}
	pthread_mutex_unlock(&tip->mutex);
}


#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
/** map_n_process_bunch - Process a bunch of requests after mapping first
 * @tip: Per-thread information
 * @bunch: Bunch to map and process
 */
void sync_map_n_process_bunch(struct thr_info *tip, struct vm_bunch *bunch)
{
	__u64 vmp_iter=0;
	struct io_pkt *iopl = NULL;
	struct preq_spec *preql = NULL;
	int tot_pkts = 0;

#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(0 < bunch->hdr.npkts && bunch->hdr.npkts <= MAP_BT_MAX_PKTS);
#endif 

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
	else
		usleep(1000);

	//while (!is_send_done(tip) && (vmp_iter < bunch->hdr.npkts)) 
	while ((vmp_iter < bunch->hdr.npkts)) 
	{
		int num_vop = 0, vop_iter = 0;
		unsigned long long stime=0, etime=0;
#ifdef REPLAYDIRECT_TEST
		int i = 0;
		struct vm_pkt *pkt;
#endif

		stime = gettime();
		if (preplayflag)
			num_vop = preplay_map(tip, &preql, &bunch->pkts[vmp_iter]); //PROVIDED
		else if (sreplayflag)
			num_vop = sreplay_map(tip, &preql, &bunch->pkts[vmp_iter]); //STANDARD
		else if (freplayflag)
			num_vop = freplay_map(tip, &preql, &bunch->pkts[vmp_iter]); //CONFIDED

#ifdef REPLAYDIRECT_TEST
		pkt = &bunch->pkts[vmp_iter];
		fprintf(stdout, "vblk=%u: pblk=[", pkt->block);
	    for (i = 0; i < num_vop; i++) 
		{	
			fprintf(stdout, "(%u,%u,%u) ", preql[i].start, preql[i].ioblk,
									preql[i].end);
		}
		fprintf(stdout, "]\n");
#endif	
		if (num_vop == 0)
		{
#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS)
			fprintf(stderr, "Error in above map function or !write_enabled\n");
#endif
			if (!(bunch->pkts[vmp_iter].rw))				
				free(bunch->pkts[vmp_iter].content); //malloc in get_vmbunch()
			else
			{
				total_submitted++;
				etime = gettime();
				ACCESSTIME_PRINT("%llu z%d %d\n", etime - stime, num_vop,
								bunch->pkts[vmp_iter].nbytes/BLKSIZE);
			}
			vmp_iter++;
			continue;
		}

	    preql_map(&preql, &iopl, num_vop, num_vop);		//FIXME
#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS)
		fprintf(stdout, "num_vop = %d, vop_iter =%d, vmp_iter =%llu\n",
						num_vop, vop_iter, vmp_iter);
#endif
		while (vop_iter < num_vop)
		{
			int ntodo = num_vop - vop_iter;
        	assert(0 < ntodo);

			sync_replay(tip, iopl+vop_iter, ntodo);
#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS)
			fprintf(stdout, "sync_map_n_process_bunch:total_submitted = %ld\n", total_submitted);
#endif
			tot_pkts += ntodo;
			vop_iter += ntodo;
		}
		etime = gettime();
		ACCESSTIME_PRINT("%llu %d\n", etime - stime, num_vop);

		if (!(bunch->pkts[vmp_iter].rw))				
			free(bunch->pkts[vmp_iter].content); //malloc in get_vmbunch()
		for (vop_iter=0; vop_iter<num_vop; vop_iter++)
			free((iopl+(vop_iter))->content);
		free(iopl);	iopl = NULL;	/* Work over, free it */

#ifdef PRO_STATS
		total_submitted++;
		if ((total_submitted & 0x1FFF) == 0)
		{
			fprintf(stdout, ".");	//to show progress after 131072 entries
			fflush(stdout);
		}
#endif
		vmp_iter++;
	}

#if 0
	/* btreplay requires number of pkts in a bunch to be less than BT_MAX_PKTS
	 * so, checking the same here.
	 */
	if (tot_pkts >= BT_MAX_PKTS)
		fprintf(stderr, "Num of pkts in this bunch = %d, more than %d\n",
						tot_pkts, BT_MAX_PKTS);
#endif

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

int sync_disk_init()
{
    char path[MAXPATHLEN];

    sprintf(path, "/dev/%s", idevnm);
#if defined(TESTVMREPLAY_DEBUG) || defined (PDDREPLAY_DEBUG_SS)  || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "Device is %s\n", path);
#endif
	syncfd = open_sync_device((char*)path);
	if (syncfd < 0)
		RET_ERR("open_sync_device failed\n");
	return 0;
}

void sync_disk_exit()
{
	if (syncfd == -1)
	{
		fprintf(stderr, "syncfd is already closed\n");
		return;
	}
	close(syncfd);
	syncfd = -1;
}

/**
 * replay_sub - Worker thread to submit AIOs that are being replayed
 */
void *sync_replay_sub(void *arg)
{
    char path[MAXPATHLEN];
	struct vm_bunch *bunch = malloc(sizeof(struct vm_bunch));

    struct thr_info *tip = arg;
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    sprintf(path, "/dev/%s", idevnm);
#if defined(TESTVMREPLAY_DEBUG) || defined (PDDREPLAY_DEBUG_SS)  || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "Device is %s\n", path);
#endif
	tip->ofd = open_sync_device((char*)path);

    //pin_to_cpu(tip);
	prctl(PR_SET_NAME,"pddreplay",0,0,0);
    set_replay_ready();
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "is_send_done(tip)=%d, iterations=%d\n", 
					is_send_done(tip), tip->iterations);
#endif
    while (tip->iterations--) {
        wait_iter_start();
        if (verbose > 1)
		{
            fprintf(stdout, "\n=== %d ===\n", tip->iterations);
            fprintf(tip->vfp, "\n=== %d ===\n", tip->iterations);
		}
        while (!is_send_done(tip) && next_bunch(tip, bunch))
		{
#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
            sync_map_n_process_bunch(tip, bunch);
#else
			sync_process_bunch(tip, bunch);
#endif
		}
        set_iter_done();
        reset_input_file(tip);
    }
	fprintf(stdout, "outside while loop\n");
    tip->send_done = 1;
    set_replay_done();
#ifdef PRO_STATS
	fprintf(stdout, "sync_replay_sub:total_submitted = %ld\n", total_submitted);
#endif

	close(tip->ofd);	
	free(bunch);
    return NULL;
}

#endif /* SYNCIO */
