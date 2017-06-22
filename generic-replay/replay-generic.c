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
#ifndef TESTVMREPLAY    /* will be defined only in pdd_testvmreplay */
    #include "replay-plugins.h" /* will be defined everywhere else */
    #include "sync-disk-interface.h"
#endif
#include "md5.h"
#include "replay-defines.h"

extern int disksimflag;
extern int collectformat;
extern int nfiles;          // Number of files to handle
extern volatile int signal_done;    // Boolean: Signal'ed, need to quit
extern int verbose;         // Boolean: Output some extra info
extern __u64 rgenesis;          // Our start time

/*
 * Variables managed under control of condition variables.
 *
 * n_reclaims_done:     Counts number of reclaim threads that have completed.
 * n_replays_done:  Counts number of replay threads that have completed.
 * n_replays_ready: Counts number of replay threads ready to start.
 * n_iters_done:    Counts number of replay threads done one iteration.
 * iter_start:      Starts an iteration for the replay threads.
 */
static volatile int n_replays_done = 0;
static pthread_mutex_t replay_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replay_done_cond = PTHREAD_COND_INITIALIZER;

static volatile int n_replays_ready = 0;
static pthread_mutex_t replay_ready_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t replay_ready_cond = PTHREAD_COND_INITIALIZER;

static volatile int n_iters_done = 0;
static pthread_mutex_t iter_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t iter_done_cond = PTHREAD_COND_INITIALIZER;

static volatile int iter_start = 0;
static pthread_mutex_t iter_start_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t iter_start_cond = PTHREAD_COND_INITIALIZER;

/* 
 * ========================================================================
 * ==== CONDITION VARIABLE ROUTINES =======================================
 * ========================================================================
 */

/**
 * __set_cv - Increments a variable under condition variable control.
 * @pmp:    Pointer to the associated mutex
 * @pcp:    Pointer to the associated condition variable
 * @vp:     Pointer to the variable being incremented
 * @mxv:    Max value for variable (Used only when ASSERTS are on)
 */
inline void __set_cv(pthread_mutex_t *pmp, pthread_cond_t *pcp,
                volatile int *vp,
                __attribute__((__unused__))int mxv)
{
    pthread_mutex_lock(pmp);
    assert(*vp < mxv);
    *vp += 1;
    pthread_cond_signal(pcp);
    pthread_mutex_unlock(pmp);
}

/**
 * __wait_cv - Waits for a variable under cond var control to hit a value
 * @pmp:    Pointer to the associated mutex
 * @pcp:    Pointer to the associated condition variable
 * @vp:     Pointer to the variable being incremented
 * @mxv:    Value to wait for
 */
inline void __wait_cv(pthread_mutex_t *pmp, pthread_cond_t *pcp,
                 volatile int *vp, int mxv)
{
    pthread_mutex_lock(pmp);
    while (*vp < mxv)
        pthread_cond_wait(pcp, pmp);
    *vp = 0;
    pthread_mutex_unlock(pmp);
}

inline void set_replay_ready(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    __set_cv(&replay_ready_mutex, &replay_ready_cond, &n_replays_ready,
         nfiles);
}

inline void wait_replays_ready(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    __wait_cv(&replay_ready_mutex, &replay_ready_cond, &n_replays_ready,
          nfiles);
}

/* set_replay_done - signals that all iterations by thread are done */
inline void set_replay_done(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    __set_cv(&replay_done_mutex, &replay_done_cond, &n_replays_done,
        nfiles);
}

/** wait_replays_done - waiting for above signal from set_replay_done
 * 						before main thread can proceed/exit 
 */
inline void wait_replays_done(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    __wait_cv(&replay_done_mutex, &replay_done_cond, &n_replays_done,
          nfiles);
}

/** set_iter_done - signals that current iteration by thread is done */
inline void set_iter_done(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    __set_cv(&iter_done_mutex, &iter_done_cond, &n_iters_done,
        nfiles);
}

/** 
 * wait_iters_done - Wait for an interation to end 
 * 						Waits for signal from set_iter_done
 */
inline void wait_iters_done(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    __wait_cv(&iter_done_mutex, &iter_done_cond, &n_iters_done,
          nfiles);
}

/**
 * wait_iter_start - Wait for an iteration to start 
 * waits for signal by start_iter()
 * 
 * This is /slightly/ different: we are waiting for a value to become
 * non-zero, and then we decrement it and go on. 
 */
inline void wait_iter_start(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    pthread_mutex_lock(&iter_start_mutex);
    while (iter_start == 0)
        pthread_cond_wait(&iter_start_cond, &iter_start_mutex);
    assert(1 <= iter_start && iter_start <= nfiles);
    iter_start--;
    pthread_mutex_unlock(&iter_start_mutex);
}

/**
 * start_iter - Start an iteration at the replay thread level
 * 			wait_iter_start() receives this signal and proceeds
 */
inline void start_iter(void)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    pthread_mutex_lock(&iter_start_mutex);
    assert(iter_start == 0);
    iter_start = nfiles;
    pthread_cond_broadcast(&iter_start_cond);
    pthread_mutex_unlock(&iter_start_mutex);
}
/**
 * is_send_done - Returns true if sender should quit early
 * @tip: Per-thread information
 */
inline int is_send_done(struct thr_info *tip)
{
    return signal_done || tip->send_done;
}


/**
 * get_bunch - Returns the number of io_pkt successfully read for given bunch
 * @tip: Per-thread information
 * @bunch: Bunch information
 * @count: Number of io_pkt in this bunch
 *
 * Returns the number of io_pkt read up. If the io_pkt is for a write 
 * request, content needs to be read up separately. This is different
 * than the requirement in btreplay.c
 */
__u64 get_vmbunch(struct thr_info *tip, struct vm_bunch *bunch, __u64 count)
{
    __u64 i = 0;
    struct vm_pkt_frame vof;
    __u16 ret;

#if defined(DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    while (i < count)
    {
        ret = read(tip->ifd, &vof, sizeof(struct vm_pkt_frame));
        if (ret != sizeof(struct vm_pkt_frame))
        {
            if (ret == 0)
                return 0;
            fatal(tip->file_name, ERR_SYSCALL, "Short io_pkt_frame(%ld)\n",
                (long)ret);
        }

        bunch->pkts[i].block = vof.block;
        bunch->pkts[i].nbytes = vof.nbytes;
        bunch->pkts[i].rw = vof.rw;
        strcpy(bunch->pkts[i].vmname, vof.vmname);
#if defined(DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS)
        fprintf(stdout, "vmname = %s, Block = %u, nbytes = %u, rw = %u\n",
                vof.vmname, vof.block, vof.nbytes, vof.rw);
#endif
#ifdef DEBUG_SS
        assert(vof.rw == 0 || vof.rw == 1);
        assert(bunch->pkts[i].rw == 0 || bunch->pkts[i].rw == 1);
#endif

		//if (collectformat && (!vof.rw || disksimflag))	/* read and write */
		if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY || 
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
				DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY |
				DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY)	
		{
			__u8 localcontent[MD5HASHLEN_STR];
#if defined(SIMREPLAY_DEBUG_SS_DONE)
			if (vof.rw)
				printf("Trying malloc of %d bytes for read\n", MD5HASHLEN_STR-1);
			else
				printf("Trying malloc of %d bytes for write\n", MD5HASHLEN_STR-1);
#endif
			bunch->pkts[i].content = malloc((MD5HASHLEN_STR-1) * sizeof(char));
			if (bunch->pkts[i].content == NULL)
				fatal("No malloc of HASHLEN_STR", ERR_SYSCALL, " sad\n");
			ret = read(tip->ifd, localcontent, MD5HASHLEN_STR);
			if (ret != MD5HASHLEN_STR)	//read into localcontent successful?
            {
                if (ret == 0) 
                   	return 0;
	    		fatal(tip->file_name, ERR_SYSCALL, "Short io_pkt for R(%ld)\n",
    	               (long)ret);
            }
			memcpy(bunch->pkts[i].content, localcontent, MD5HASHLEN_STR-1);
			//bunch->pkts[i].content[HASHLEN_STR-1] = '\0'; no null char
		}
        else if (!vof.rw)        /* write */
        {
			assert(0);	//not expected for now -- 29 apr 2014
			printf("Trying malloc of %d bytes\n", vof.nbytes);
   	        bunch->pkts[i].content = malloc(vof.nbytes * sizeof(__u8)); //free in sync_map_n_process_bunch()
       	    if (bunch->pkts[i].content == NULL)
           	{
                fatal("No malloc", ERR_SYSCALL, " sad\n");
   	        }

       	    ret = read(tip->ifd, bunch->pkts[i].content, vof.nbytes);
           	if (ret != vof.nbytes)
            {
   	            if (ret == 0)
       			    return 0;
                fatal(tip->file_name, ERR_SYSCALL, "Short io_pkt(%ld)\n",
   	                (long)ret);
       	    }
		}
		else	/* read */
		{
			assert(0);	//not expected for now -- 29 apr 2014
            bunch->pkts[i].content = NULL;
		}

        i++;
    }

    return i;
}

/**
 * next_bunch - Retrieve next bunch of AIOs to process
 * @tip: Per-thread information
 * @bunch: Bunch information
 *
 * Returns TRUE if we recovered a bunch of IOs, else hit EOF
 */
int next_bunch(struct thr_info *tip, struct vm_bunch *bunch)
{
    size_t count;
    __u64 result;

#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    count = read(tip->ifd, &bunch->hdr, sizeof(bunch->hdr));
    if (count != sizeof(bunch->hdr)) {
        if (count == 0)
            return 0;

        fatal(tip->file_name, ERR_SYSCALL, "Short hdr(%ld)\n",
            (long)count);
        /*NOTREACHED*/
    }
#if defined(DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS_DONE)
    fprintf(stdout, "bunch->hdr.npkts = %llu\n", bunch->hdr.npkts);
#endif
    assert(bunch->hdr.npkts > 0 && bunch->hdr.npkts <= BT_MAX_PKTS);

    result = get_vmbunch(tip, bunch, bunch->hdr.npkts);
    if (result != bunch->hdr.npkts) {
        fatal(tip->file_name, ERR_SYSCALL, "Short pkts(%ld/%ld)\n",
            (long)result, (long)bunch->hdr.npkts);
        /*NOTREACHED*/
    }

    return 1;
}

/**
 * ts2ns - Convert timespec values to a nanosecond value
 */
#define NS_TICKS        ((__u64)1000 * (__u64)1000 * (__u64)1000)
inline __u64 ts2ns(struct timespec *ts)
{
    return ((__u64)(ts->tv_sec) * NS_TICKS) + (__u64)(ts->tv_nsec);
}

/**
 * ts2ns - Convert timeval values to a nanosecond value
 */
inline __u64 tv2ns(struct timeval *tp)
{
    return ((__u64)(tp->tv_sec)) + ((__u64)(tp->tv_usec) * (__u64)1000);
}

/**
 * gettime - Returns current time 
 */
inline __u64 gettime(void)
{
    static int use_clock_gettime = -1;      // Which clock to use

    if (use_clock_gettime < 0) {
        use_clock_gettime = clock_getres(CLOCK_MONOTONIC, NULL) == 0;
        if (use_clock_gettime) {
            struct timespec ts = {
                .tv_sec = 0,
                .tv_nsec = 0
            };
            clock_settime(CLOCK_MONOTONIC, &ts);
        }
    }

    if (use_clock_gettime) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return ts2ns(&ts);
    }
    else {
        struct timeval tp;
        gettimeofday(&tp, NULL);
        return tv2ns(&tp);
    }
}

/**
 * stall - Stall for the number of nanoseconds requested
 *
 * We may be late, in which case we just return.
 */
void stall(struct thr_info *tip, unsigned long long oc)
{
    struct timespec req;
	unsigned long long oclock;
    unsigned long long dreal, tclock = gettime() - rgenesis;

	//incomplete.... 
	oclock = tclock + (oc-tclock); ///speedupfactor;

#ifdef REPLAYDIRECT_DEBUG_SS
        fprintf(stdout, "   stall(%lld.%09lld, %lld.%09lld)\n",
            du64_to_sec(oclock), du64_to_nsec(oclock),
            du64_to_sec(tclock), du64_to_nsec(tclock));
#endif
    if (verbose > 1)
    {
        fprintf(stdout, "   stall(%lld.%09lld, %lld.%09lld)\n",
            du64_to_sec(oclock), du64_to_nsec(oclock),
            du64_to_sec(tclock), du64_to_nsec(tclock));
        fprintf(tip->vfp, "   stall(%lld.%09lld, %lld.%09lld)\n",
            du64_to_sec(oclock), du64_to_nsec(oclock),
            du64_to_sec(tclock), du64_to_nsec(tclock));
        if (tclock > oclock)
            fprintf(stdout, "we were late, so just return\n");
    }

    while (!is_send_done(tip) && tclock < oclock) {
        dreal = oclock - tclock;
        req.tv_sec = dreal / (1000 * 1000 * 1000);
        req.tv_nsec = dreal % (1000 * 1000 * 1000);

        if (verbose > 1) {
            fprintf(stdout, "++ stall(%lld.%09lld) ++\n",
                (long long)req.tv_sec,
                (long long)req.tv_nsec);
            fprintf(tip->vfp, "++ stall(%lld.%09lld) ++\n",
                (long long)req.tv_sec,
                (long long)req.tv_nsec);
        }

        if (nanosleep(&req, NULL) < 0 && signal_done)
        {
            break;
        }

        tclock = gettime() - rgenesis;
    }
}

