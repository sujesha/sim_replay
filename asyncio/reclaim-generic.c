
#ifdef ASYNCIO
/* 
 * ========================================================================
 * ==== RECLAIM ROUTINES ==================================================
 * ========================================================================
 */

#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <assert.h>
#include "per-input-file.h"
#include "debugg.h"
#include "iocbs.h"
#include "pdd_config.h"
#include "replay-generic.h"

extern int naios;         // Number of AIOs per thread
extern int verbose;         // Boolean: Output some extra info
extern int nfiles;          // Number of files to handle
long total_reclaimed = 0;

/*
 * Variables managed under control of condition variables.
 *
 * n_reclaims_done:     Counts number of reclaim threads that have completed.
 * n_replays_done:  Counts number of replay threads that have completed.
 * n_replays_ready: Counts number of replay threads ready to start.
 * n_iters_done:    Counts number of replay threads done one iteration.
 * iter_start:      Starts an iteration for the replay threads.
 */
static volatile int n_reclaims_done = 0;
static pthread_mutex_t reclaim_done_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t reclaim_done_cond = PTHREAD_COND_INITIALIZER;

inline void set_reclaim_done(void)
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
    __set_cv(&reclaim_done_mutex, &reclaim_done_cond, &n_reclaims_done,
         nfiles);
}

inline void wait_reclaims_done(void)
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
    __wait_cv(&reclaim_done_mutex, &reclaim_done_cond, &n_reclaims_done,
          nfiles);
}

/**
 * is_reap_done - Returns true if reaper should quit early
 * @tip: Per-thread information
 */
static inline int is_reap_done(struct thr_info *tip)
{
    return tip->send_done && tip->naios_out == 0;
}


/**
 * reap_wait_aios - Wait for and return number of outstanding AIOs
 *
 * Will return 0 if we are done
 */
static int reap_wait_aios(struct thr_info *tip)
{
    int naios = 0;
#if defined(DIRECTREPLAY_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    if (!is_reap_done(tip)) {
        pthread_mutex_lock(&tip->mutex);
        while (tip->naios_out == 0) {
            tip->reap_wait = 1;
            if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
                fatal("pthread_cond_wait", ERR_SYSCALL,
                    "nfree_current cond wait failed\n");
                /*NOTREACHED*/
            }
        }
        naios = tip->naios_out;
        pthread_mutex_unlock(&tip->mutex);
    }
    assert(is_reap_done(tip) || naios > 0);

    return is_reap_done(tip) ? 0 : naios;
}

/**
 * reclaim_ios - Reclaim AIOs completed, recycle IOCBs
 * @tip: Per-thread information
 * @naios_out: Number of AIOs we have outstanding (min)
 */
static void reclaim_ios(struct thr_info *tip, long naios_out)
{
    long i, ndone;
    struct io_event *evp, events[naios_out];

#if defined(RECLAIM_DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(DIRECTREPLAY_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
again:
    assert(naios > 0);
    for (;;) {
        ndone = io_getevents(tip->ctx, 1, naios_out, events, NULL);
        if (ndone > 0)
            break;

        if (errno && errno != EINTR) {
            fatal("io_getevents", ERR_SYSCALL,
                "io_getevents failed\n");
            /*NOTREACHED*/
        }
    }
	total_reclaimed += ndone;
#ifdef PRO_STATS
	fprintf(stdout, "total_reclaimed = %ld\n", total_reclaimed);
#endif    
    assert(0 < ndone && ndone <= naios_out);

    pthread_mutex_lock(&tip->mutex);
    for (i = 0, evp = events; i < ndone; i++, evp++) {
        struct iocb_pkt *iocbp = evp->data;

                if (evp->res != iocbp->iocb.u.c.nbytes) {
                        fatal(NULL, ERR_SYSCALL,
                              "Event failure %ld/%ld\t(%ld + %u)\n",
                              (long)evp->res, (long)evp->res2,
                              (long)iocbp->iocb.u.c.offset / BLKSIZE,
                  (size_t)iocbp->iocb.u.c.nbytes / BLKSIZE);
                        /*NOTREACHED*/
                }
				if (verbose)
					fprintf(stdout, "reclaimed sector %llu\n", 
									iocbp->iocb.u.c.offset / BLKSIZE);


        list_move_tail(&iocbp->head, &tip->free_iocbs);
    }

    tip->naios_free += ndone;
    tip->naios_out -= ndone;
    naios_out = minl(naios_out, tip->naios_out);
    if (tip->send_wait) {
        tip->send_wait = 0;
        pthread_cond_signal(&tip->cond);
    }
    pthread_mutex_unlock(&tip->mutex);

    /*
     * Short cut: If we /know/ there are some more AIOs, go handle them
     */
    if (naios_out)
        goto again;
}


/**
 * replay_rec - Worker thread to reclaim AIOs
 * @arg: Pointer to thread information
 */         
void *replay_rec(void *arg)
{   
    long naios_out;
    struct thr_info *tip = arg;
#if defined(RECLAIM_DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	assert(tip != NULL);    
    while ((naios_out = reap_wait_aios(tip)) > 0)
        reclaim_ios(tip, naios_out);
    
    assert(tip->send_done);
    tip->reap_done = 1;
    set_reclaim_done();

#ifdef PRO_STATS
	fprintf(stdout, "total_reclaimed = %ld\n", total_reclaimed);
#endif    

    return NULL;
}   

#endif	/* ASYNCIO */
