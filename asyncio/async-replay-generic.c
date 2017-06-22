#ifdef ASYNCIO

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
#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
	#include "replay-plugins.h"	/* will be defined everywhere else */
	#include "sync-disk-interface.h"
#endif
#include "replay-generic.h"

extern volatile int signal_done;    // Boolean: Signal'ed, need to quit

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
size_t pgsize;           // System Page size, used in buf_alloc()
//LIST_HEAD(input_devs);       // List of devices to handle
//LIST_HEAD(input_files);      // List of input files to handle
//LIST_HEAD(map_devs);     // List of device maps
int no_stalls = 1;       // Boolean: default disabled stalls
int speedupfactor = 1;	//Default: no speedup
int find_records = 0;        // Boolean: Find record files auto
long total_submitted = 0;

extern int naios;         // Number of AIOs per thread
extern int ncpus;           // Number of CPUs in the system
extern int syncfd;
extern int verbose;

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

int open_async_device(char* path)
{
    int ofd;
#if defined(TESTVMREPLAY_DEBUG) || defined(RECREATE_DEBUG_SS) || defined(DIRECTRECREATE_DEBUG_SS)
    fprintf(stdout, "device = %s\n", path);
#endif

    if (write_enabled)
        ofd = open(path, O_RDWR | O_DIRECT);
    else
        ofd = open(path, O_RDONLY | O_DIRECT);

    if (ofd < 0)
    {
        RET_ERR("Failed device open\n");
    }
    return ofd;
}

void close_async_device(int dp)
{
    close(dp);
}


#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
/** map_n_process_bunch - Process a bunch of requests after mapping first
 * @tip: Per-thread information
 * @bunch: Bunch to map and process
 */
void async_map_n_process_bunch(struct thr_info *tip, struct vm_bunch *bunch)
{
	int vmp_iter=0;
	struct io_pkt *iopl = NULL;
	struct preq_spec *preql = NULL;
	int tot_pkts = 0;

#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
    assert(0 < bunch->hdr.npkts && bunch->hdr.npkts <= MAP_BT_MAX_PKTS);

	if (!no_stalls)
		stall(tip, (bunch->hdr.time_stamp - genesis) / speedupfactor);

	while (!is_send_done(tip) && (vmp_iter < (int)bunch->hdr.npkts)) 
	{
		int num_vop = 0, vop_iter = 0;
#ifdef REPLAYDIRECT_TEST
		int i = 0;
		struct vm_pkt *pkt;
#endif

		if (preplayflag)
			num_vop = preplay_map(tip, &preql, &bunch->pkts[vmp_iter]);
		else if (sreplayflag)
			num_vop = sreplay_map(tip, &preql, &bunch->pkts[vmp_iter]);
		else if (ioreplayflag)
			num_vop = ioreplay_map(tip, &preql, &bunch->pkts[vmp_iter]);

#ifdef REPLAYDIRECT_TEST
		pkt = &bunch->pkts[vmp_iter];
		fprintf(stdout, "vblk=%llu: pblk=[", pkt->block);
	    for (i = 0; i < num_vop; i++) 
		{	
			fprintf(stdout, "(%u,%llu,%u) ", preql[i].start, preql[i].ioblk,
									preql[i].end);
		}
		fprintf(stdout, "]\n");
#endif	

	    preql_map(&preql, &iopl, num_vop);
#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS)
		fprintf(stdout, "num_vop = %d, vop_iter =%d, vmp_iter =%d\n",
						num_vop, vop_iter, vmp_iter);
#endif
		while (vop_iter < num_vop)
		{
			struct iocb *list[num_vop];
			long ndone;
			int ntodo = min(nfree_current(tip), num_vop - vop_iter);
        	assert(0 < ntodo && ntodo <= naios);

			iocbs_map(tip, list, iopl+vop_iter, ntodo);

			if (ntodo)
			{
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
#ifdef PRO_STATS
				total_submitted += ndone;
				fprintf(stdout, "total_submitted = %ld\n", total_submitted);
#endif
	            pthread_mutex_lock(&tip->mutex);
    	        tip->naios_out += ndone;
        	    assert(tip->naios_out <= naios);
	            if (tip->reap_wait) {
	                tip->reap_wait = 0;
	                pthread_cond_signal(&tip->cond);
    	        }
        	    pthread_mutex_unlock(&tip->mutex);

				vop_iter += ndone;
				tot_pkts += ndone;
				assert(vop_iter <= num_vop);
            }
		}
		free(iopl);	iopl = NULL;	/* Work over, free it */
		vmp_iter++;
	}

	/* btreplay requires number of pkts in a bunch to be less than BT_MAX_PKTS
	 * so, checking the same here.
	 */
	if (tot_pkts >= BT_MAX_PKTS)
		fprintf(stderr, "Num of pkts in this bunch = %d, more than %d\n",
						tot_pkts, BT_MAX_PKTS);

#ifdef PRO_STATS
	/* Above while loop is for a single vmbunch, containing #npkts requests */
	stotalreq += bunch->hdr.npkts;

	/* #npkts requests are split into tot_pkts #blks */
	stotalblk += tot_pkts;
#endif	


}
#else	/* will be true only in testvmreplay */
/**
 * process_bunch - Process a bunch of requests
 * @tip: Per-thread information
 * @bunch: Bunch to process
 */
void async_process_bunch(struct thr_info *tip, struct vm_bunch *bunch)
{
    __u64 i = 0;
    struct iocb *list[bunch->hdr.npkts];

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
				fprintf(stdout, "total_submitted = %ld\n", total_submitted);
#endif
            assert(i <= bunch->hdr.npkts);
        }
    }
}
#endif	/* process_bunch only present in testvmreplay */

/**
 * replay_sub - Worker thread to submit AIOs that are being replayed
 */
void *async_replay_sub(void *arg)
{
    char path[MAXPATHLEN];
	struct vm_bunch bunch;
    struct thr_info *tip = arg;
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "10. iterations=%d\n", tip->iterations);
#endif

    //pin_to_cpu(tip);
	prctl(PR_SET_NAME,"pddreplay",0,0,0);
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "11. iterations=%d\n", tip->iterations);
#endif

    sprintf(path, "/dev/%s", idevnm);
#if defined(TESTVMREPLAY_DEBUG) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "Device is %s\n", path);
#endif
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "12. iterations=%d\n", tip->iterations);
#endif
	tip->ofd = open_async_device((char*)path);
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "13. iterations=%d\n", tip->iterations);
#endif

#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
	/* Used for synchronous reads of controller-utils, used by PROVIDED */
	syncfd = open_sync_device((char*)path);
#endif
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "14. iterations=%d\n", tip->iterations);
#endif

    set_replay_ready();
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "is_send_done(tip)=%d, iterations=%d\n", 
					is_send_done(tip), tip->iterations);
#endif
	assert(tip->iterations == 1);	//hard-coded for 1 iteration of replay
    while (!is_send_done(tip) && tip->iterations--) {
        wait_iter_start();
        if (verbose > 1)
		{
            fprintf(stdout, "\n=== %d ===\n", tip->iterations);
            fprintf(tip->vfp, "\n=== %d ===\n", tip->iterations);
		}
        while (!is_send_done(tip) && next_bunch(tip, &bunch))
		{
#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
            async_map_n_process_bunch(tip, &bunch);
#else
			async_process_bunch(tip, &bunch);
#endif
		}
        set_iter_done();
        reset_input_file(tip);
    }
    tip->send_done = 1;
    set_replay_done();
#ifdef PRO_STATS
	fprintf(stdout, "total_submitted = %ld\n", total_submitted);
#endif

	close(tip->ofd);	
#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */
	close(syncfd);
#endif
    return NULL;
}

#endif /* ASYNCIO */
