/* Routines that work per input file for replay */

#define _MULTI_THREADED
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include <sched.h>
#include <fcntl.h>
#include "per-input-file.h"
#include "vmbunching_structs.h"
#include "slist.h"
#ifdef ASYNCIO
	#include "iocbs.h"
#endif
#include "debugg.h"
#include "debug.h"


extern char pddversion[];
extern int pddver_mjr;
extern int pddver_mnr;
extern int pddver_sub;

extern int warmupflag;

extern int nfiles;          // Number of files to handle
extern struct slist_head input_files_replay;      // List of input files
extern struct slist_head input_files_warmup;      // List of warmup files
extern int naios;         /* replay-generic.c */
extern size_t pgsize;           // System Page size
extern int def_iterations;      // Default number of iterations
extern int verbose;         // Boolean: Output some extra info
extern __u64 genesis;      // Earliest time seen
extern char *idir;        // Input directory base
extern char *ifile;
extern int nfiles;          // Number of files to handle

#ifdef SYNCIO
	void *sync_replay_sub(void *arg);
#endif
#ifdef SIM_REPLAY
	void *sim_replay_sub(void *arg);
	void *sim_warmup_sub(void *arg);
#endif

#ifdef ASYNCIO	
	void *async_replay_sub(void *arg);
	void *replay_rec(void *arg);
	void iocb_init(struct thr_info *tip, struct iocb_pkt *iocbp);
#endif
inline __u64 mk_pddversion(int mjr, int mnr, int sub);

/**
 * buf_alloc - Returns a page-aligned buffer of the specified size
 * @nbytes: Number of bytes to allocate
 */
void *buf_alloc(__u16 nbytes)
{
    void *buf;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	assert(pgsize != 0);
    if (posix_memalign(&buf, pgsize, nbytes)) {
        fatal("posix_memalign", ERR_SYSCALL, "Allocation failed\n");
        /*NOTREACHED*/
    }

    return buf;
}

/**
 * touch_memory - Force physical memory to be allocating it
 * 
 * For malloc()ed memory we need to /touch/ it to make it really
 * exist. Otherwise, for write's (to storage) things may not work
 * as planned - we see Linux just use a single area to /read/ from
 * (as there isn't any memory that has been associated with the 
 * allocated virtual addresses yet).
 */
inline void touch_memory(__u8 *buf, __u16 bsize)
{
#if defined(PREP_BUFS)
    memset((void*)buf, 0, bsize);
#else
    __u16 i;

    for (i = 0; i < bsize; i += pgsize)
        buf[i] = 0;
#endif
}


/**
 * add_input_file - Allocate and initialize per-input file structure
 * @file_name: Fully qualifed input file name
 * @tip: thread for this input file
 */
void add_input_file(char *file_name, char *wfile_name)
{
	struct stat infilebuf;
    struct vm_file_hdr hdr;
	struct thr_info *tip = buf_alloc(sizeof(*tip));
	struct thr_info *wtip = buf_alloc(sizeof(*wtip));		//memleak
    __u64 my_version = mk_pddversion(pddver_mjr, pddver_mnr, pddver_sub);

#if defined(PDDREPLAY_DEBUG_SS) || defined(RECLAIM_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    //assert(0 <= cpu && cpu < ncpus);

    memset(&hdr, 0, sizeof(hdr));
    memset(tip, 0, sizeof(struct thr_info));
	if (wfile_name)
		memset(wtip, 0, sizeof(struct thr_info));
    //tip->cpu = cpu % cpus_to_use;
    tip->iterations = def_iterations;
	if (wfile_name)
    	wtip->iterations = def_iterations;

    //tip->ifd = open(file_name, O_RDONLY | O_DIRECT);	//not working out
    tip->ifd = open(file_name, O_RDONLY);
    if (tip->ifd < 0) {
        fatal(file_name, ERR_ARGS, "Unable to open\n");
        /*NOTREACHED*/
    }
	if (wfile_name)
	{
    	wtip->ifd = open(wfile_name, O_RDONLY);
	    if (wtip->ifd < 0) {
	        fatal(wfile_name, ERR_ARGS, "Unable to open\n");
	        /*NOTREACHED*/
    	}
	}

	/* FADVISE so that cache can be released immediately for this file */
	posix_fadvise(tip->ifd, 0, 0, POSIX_FADV_DONTNEED);
	if (wfile_name)
		posix_fadvise(wtip->ifd, 0, 0, POSIX_FADV_DONTNEED);
#if 0
	if (tip->fp == NULL)
		tip->fp = fdopen(tip->ifd, "r");
    (*tipp)->fp = fopen(file_name, "r");
    if (!(*tipp)->fp) {
        fatal(file_name, ERR_SYSCALL, "Read Open failed\n");
        /*NOTREACHED*/
    }

	(*tipp)->ifd = fileno((*tipp)->fp);
#endif
    if (fstat(tip->ifd, &infilebuf) < 0) {
        fatal(file_name, ERR_SYSCALL, "fstat failed\n");
        /*NOTREACHED*/
    }

	if (strstr(file_name, "vmbunch"))
	{
    if (infilebuf.st_size < (off_t)sizeof(hdr)) {
        if (verbose)
            fprintf(stderr, "\t%s empty\n", file_name);
        goto empty_file;
    }
    if (read(tip->ifd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fatal(file_name, ERR_ARGS, "Header read failed\n");
        /*NOTREACHED*/
    }

#if 0
	if (fread(&hdr, sizeof(struct hdr), 1, tip->fp) != 1)
	{
		fatal((*tipp)->file_name, ERR_SYSCALL, "Short io_pkt_frame\n");
    }
#endif

    if (hdr.version != my_version) {
        fprintf(stderr, "%llx %llx %llx %llx\n",
            (long long unsigned)hdr.version,
            (long long unsigned)hdr.genesis,
            (long long unsigned)hdr.nbunches,
            (long long unsigned)hdr.total_pkts);
        fatal(NULL, ERR_ARGS,
            "BT version mismatch: %lx versus my %lx\n",
            (long)hdr.version, (long)my_version);

    }
	else if (verbose > 1)
		fprintf(stderr, "BT version matches: %lx versus my %lx\n",
	            (long)hdr.version, (long)my_version);

    if (hdr.nbunches == 0) {
empty_file:
		fprintf(stderr, "hdr.nbunches == 0!\n");
        close(tip->ifd);
        free(tip);
        return;
    }

    if (hdr.genesis < genesis) {
        if (verbose > 1)
            /*printf(stderr, "Setting genesis to %llu.%llu\n",
                du64_to_sec(hdr.genesis),
                du64_to_nsec(hdr.genesis));*/
            fprintf(stderr, "\tSetting new genesis: %llu\n",
                hdr.genesis);
        genesis = hdr.genesis;
    }
	} // only for vmbunch file
    tip->file_name = strdup(file_name);
    slist_add(&tip->head, &input_files_replay);
	if (warmupflag)
	{
		tip->wfile_name = strdup(wfile_name);
		slist_add(&wtip->head, &input_files_warmup);
	}

    fprintf(stdout, "Added %s\n", file_name);
	if (warmupflag)
	{
    	fprintf(stdout, "Added %s for cache warm-up\n", wfile_name);
	}

	nfiles++;
}


/**
 * tip_init - Per thread initialization function
 */
void tip_init(struct thr_info *tip, struct thr_info *wtip)
{
#ifdef ASYNCIO	
    int i;
#endif

#if defined(RECLAIM_DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(tip != NULL);
	if (warmupflag)
		assert(wtip != NULL);
#endif

#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "5. iterations=%d\n", tip->iterations);
#endif
#ifdef ASYNCIO	
    INIT_LIST_HEAD(&tip->free_iocbs);
    INIT_LIST_HEAD(&tip->used_iocbs);
#endif

    pthread_mutex_init(&tip->mutex, NULL);
    pthread_cond_init(&tip->cond, NULL);
	if (warmupflag)
	{
    	pthread_mutex_init(&wtip->mutex, NULL);
	    pthread_cond_init(&wtip->cond, NULL);
	}
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "6. iterations=%d\n", tip->iterations);
#endif

#ifdef ASYNCIO	
	/* Asynchronous I/O needs io_setup to be done before replay */
    if (io_setup(naios, &tip->ctx)) {
        fatal("io_setup", ERR_SYSCALL, "io_setup failed\n");
        /*NOTREACHED*/
    }
#endif
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "7. iterations=%d\n", tip->iterations);
#endif

    tip->ofd = -1;			/* Output file not yet ready */
    tip->send_done = 0;
    tip->send_wait = 0;
	if (warmupflag)
	{
	    wtip->ofd = -1;			/* Output file not yet ready */
	    wtip->send_done = 0;
	    wtip->send_wait = 0;
	}
#ifdef ASYNCIO	
    tip->naios_out = 0;
   	tip->reap_wait = 0;
	tip->reap_done = 0;
#endif

	/* Initializing thread structures */
    memset(&tip->sub_thread, 0, sizeof(pthread_t));	/* Replay/submit thread */
	if (warmupflag)
    	memset(&tip->sub_thread, 0, sizeof(pthread_t));	/*warm up thread */

#ifdef ASYNCIO	
    memset(&tip->rec_thread, 0, sizeof(pthread_t));	/* Reclaim thread in async*/
    for (i = 0; i < naios; i++) /* Number of Asynchronous I/Os per thread */
	{
        struct iocb_pkt *iocbp = buf_alloc(sizeof(*iocbp));

        iocb_init(tip, iocbp);
        slist_add(&iocbp->head, &tip->free_iocbs);
    }
    tip->naios_free = naios;
#endif
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "8. iterations=%d\n", tip->iterations);
#endif

    if (verbose > 1)	/* Print verbose output if requested */
    {
        char fn[MAXPATHLEN];

        fprintf(stdout, "%s/%s.pddrep", idir, ifile);	/* into stdout */
            //tip->cpu);
        sprintf(fn, "%s/%s.pddrep", idir, ifile);		/* outfile name */
            //tip->cpu);
        tip->vfp = fopen(fn, "w");						/* open file */
        if (!tip->vfp) {
            fatal(fn, ERR_SYSCALL, "Failed to open report\n");
            /*NOTREACHED*/
        }
        fprintf(stdout, "file %s opened\n", fn);		/* into file */

        setlinebuf(tip->vfp);
    }
	else
	{
		tip->vfp = NULL;
		if (warmupflag)
			wtip->vfp = NULL;
	}

#ifdef SIM_REPLAY
	/* In general, SIM_REPLAY indicates that cache and disk are being 
	 * simulated.
	 */
	if (warmupflag)
	{
		fprintf(stdout, "starting cache-warmup\n");
	    if (pthread_create(&wtip->sub_thread, NULL, sim_warmup_sub, wtip)) {
	        fatal("pthread_create", ERR_SYSCALL,
	            "cache-warmup create failed\n");
		        /*NOTREACHED*/
	    }
		if (pthread_join(wtip->sub_thread, NULL))
		{
			fatal("pthread_join", ERR_SYSCALL, "warmup join failed\n");
		}
		warmupflag = 0;		/* indicate done for sim_warmup_sub() */
	}

	fprintf(stdout, "starting sub_thread sim_replay_sub\n");
    if (pthread_create(&tip->sub_thread, NULL, sim_replay_sub, tip)) {
        fatal("pthread_create", ERR_SYSCALL,
            "sim_replay_sub thread create failed\n");
        /*NOTREACHED*/
    }

    if (verbose > 1)
    {
        fprintf(stdout, "sim_replay_sub thread started\n");
    }
#else
#ifdef SYNCIO
	/* SYNCIO implies that cache and disk are not being simulated and 
	 * sync I/O used, this is basically pdd_replay (the older version!)
	 */
	fprintf(stdout, "starting sub_thread sync_replay_sub\n");
    if (pthread_create(&tip->sub_thread, NULL, sync_replay_sub, tip)) {
        fatal("pthread_create", ERR_SYSCALL,
            "sync_replay_sub thread create failed\n");
        /*NOTREACHED*/
    }
    if (verbose > 1)
    {
        fprintf(stdout, "sync_replay_sub thread started\n");
    }
#endif
#endif

#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "9. iterations=%d\n", tip->iterations);
#endif
#ifdef ASYNCIO	
	/* ASYNCIO implies that cache and disk are not being simulated and
	 * asynchronous I/O used, this is from btreplay (much older version!)
	 */
	/* Async I/O needs both replay and reclaim threads */
    if (pthread_create(&tip->sub_thread, NULL, async_replay_sub, tip)) {
        fatal("pthread_create", ERR_SYSCALL,
            "replay_sub thread create failed\n");
        /*NOTREACHED*/
    }
    if (verbose > 1)
    {
        fprintf(stdout, "replay_sub thread started\n");
    }
    if (pthread_create(&tip->rec_thread, NULL, replay_rec, tip)) {
        fatal("pthread_create", ERR_SYSCALL,
            "replay_rec thread create failed\n");
        /*NOTREACHED*/
    }
    if (verbose > 1)
    {
        fprintf(stdout, "replay_rec thread started\n");
	}
#endif
}

/**
 * tip_release - Release resources associated with this thread
 */
void tip_release(struct thr_info *tip)
{
#ifdef ASYNCIO
    struct slist_head *p, *q;
#endif	

	fprintf(stdout, "In %s\n", __FUNCTION__);
#ifdef ASYNCIO
    assert(tip->reap_done);
    assert(slist_len(&tip->used_iocbs) == 0);
    assert(tip->naios_free == naios);
#endif	

    if (pthread_join(tip->sub_thread, NULL)) {
        fatal("pthread_join", ERR_SYSCALL, "pthread sub join failed\n");
        /*NOTREACHED*/
    }

#ifdef ASYNCIO
    if (pthread_join(tip->rec_thread, NULL)) {
        fatal("pthread_join", ERR_SYSCALL, "pthread rec join failed\n");
        /*NOTREACHED*/
    }

    io_destroy(tip->ctx);

    list_splice(&tip->used_iocbs, &tip->free_iocbs);
    slist_for_each_safe(p, q, &tip->free_iocbs) {
        struct iocb_pkt *iocbp = slist_entry(p, struct iocb_pkt, head);

        slist_del(&iocbp->head);
        if (iocbp->nbytes)
            free(iocbp->iocb.u.c.buf);
        free(iocbp);
    }
#endif	

    pthread_cond_destroy(&tip->cond);
    pthread_mutex_destroy(&tip->mutex);
}

/**
 * reset_input_file - Reset the input file for the next iteration
 * @tip: Thread information
 *
 * We also do a dummy read of the file header to get us to the first bunch.
 */
void reset_input_file(struct thr_info *tip)
{
    struct vm_file_hdr hdr;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    lseek(tip->ifd, 0, 0);

    if (read(tip->ifd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        fatal(tip->file_name, ERR_ARGS, "Header reread failed\n");
        /*NOTREACHED*/
    }
}

/**
 * rem_input_file - Release resources associated with an input file
 * @tip: Per-input file information
 */
void rem_input_file(struct thr_info *tip)
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
    //slist_del(&tip->head);

	free(tip->file_name);
	if (!warmupflag)	//is temporarily set before rem_input_file of warmup
	    tip_release(tip);

	if (tip->ofd > 0)
	    close(tip->ofd);
	if (tip->ifd > 0)
    close(tip->ifd);
    //free(tip->devnm);
    free(tip);
}

/**
 * rem_input_files - Remove all input files
 */
void rem_input_files(void)
{
    struct slist_head *p, *q;

    slist_for_each_safe(p, q, &input_files_replay) {
        rem_input_file(slist_entry(p, struct thr_info, head));
    }

	warmupflag = 1;	//temporarily set for rem_input_file()
    slist_for_each_safe(p, q, &input_files_warmup) {
        rem_input_file(slist_entry(p, struct thr_info, head));
    }
	warmupflag = 0;
}

