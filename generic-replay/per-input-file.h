#ifndef _PER_INPUT_FILE_H_
#define _PER_INPUT_FILE_H_

#include <pthread.h>
#include <unistd.h>
#include <libaio.h>
#include "slist.h"

/*
 * Per input file information
 *
 * @head:   Used to link up on input_files
 * @free_iocbs: List of free iocb's available for use
 * @used_iocbs: List of iocb's currently outstanding
 * @mutex:  Mutex used with condition variable to protect volatile values
 * @cond:   Condition variable used when waiting on a volatile value change
 * @naios_out:  Current number of AIOs outstanding on this context
 * @naios_free: Number of AIOs on the free list (short cut for list_len)
 * @send_wait:  Boolean: When true, the sub thread is waiting on free IOCBs
 * @reap_wait:  Boolean: When true, the rec thread is waiting on used IOCBs
 * @send_done:  Boolean: When true, the sub thread has completed work
 * @reap_done:  Boolean: When true, the rec thread has completed work
 * @sub_thread: Thread used to submit IOs.
 * @rec_thread: Thread used to reclaim IOs.
 * @ctx:    IO context
 * @devnm:  Copy of the device name being managed by this thread
 * @file_name:  Full name of the input file
 * @cpu:    CPU this thread is pinned to
 * @ifd:    Input file descriptor
 * @ofd:    Output file descriptor
 * @iterations: Remaining iterations to process
 * @vfp:    For verbose dumping of actions performed
 */
#ifdef ASYNCIO
struct thr_info {
    struct slist_head head, free_iocbs, used_iocbs;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile long naios_out, naios_free;
    volatile int send_wait, reap_wait, send_done, reap_done;
    int ifd, ofd, iterations;
    FILE *vfp;
    char *file_name;
    io_context_t ctx;
    pthread_t sub_thread, rec_thread;
};
#endif

#ifdef SYNCIO
struct thr_info {
    struct slist_head head;
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    volatile int send_wait, send_done;
    int ifd, ofd, iterations;
    FILE *vfp;
    char *file_name, *wfile_name;
    io_context_t ctx;
    pthread_t sub_thread;
};
#endif

/********************* PROTOTYPES *************************/
//void add_input_file(char *file_name);
void add_input_file(char *file_name, char *wfile_name);
//void tip_init(struct thr_info *tip);
void tip_init(struct thr_info *tip, struct thr_info *wtip);
void rem_input_files(void);


#endif /* _PER_INPUT_FILE_H_ */
