
#ifdef ASYNCIO

#include <pthread.h>
#include <assert.h>
#include <asm/types.h>
#include "vmbunching_structs.h"
#include "per-input-file.h"
#include "iocbs.h"
#include "pdd_config.h"
#include "debugg.h"

int naios = 512;         // Number of AIOs per thread

extern int write_enabled;       // Boolean: Enable writing
extern int verbose;         // Boolean: Output some extra info

inline void *buf_alloc(size_t nbytes);
inline void touch_memory(__u8 *buf, size_t bsize);
inline int is_send_done(struct thr_info *tip);

/**
 * iocb_init - Initialize the fields of an IOCB
 * @tip: Per-thread information
 * iocbp: IOCB pointer to update
 */
void iocb_init(struct thr_info *tip, struct iocb_pkt *iocbp)
{
    iocbp->tip = tip;
    iocbp->nbytes = 0;
    iocbp->iocb.u.c.buf = NULL;
}

#if 0
void copycontent(char **d, char *s, size_t nbytes)
{
//    *d = malloc(nbytes);    //free in bunch_output_pkts()
    *d = buf_alloc(nbytes);		//free?
    if (d == NULL)
        fprintf(stderr, "malloc for d failed\n");
	assert(d != NULL);
    memcpy(*d, s, nbytes);
}
#endif

/**
 * iocb_setup - Set up an iocb with this AIOs information
 * @iocbp: IOCB pointer to update
 * @rw: Direction (0 == write, 1 == read)
 * @n: Number of bytes to transfer
 * @off: Offset (in bytes)
 */
void iocb_setup(struct iocb_pkt *iocbp, int rw, size_t n, __u8 *content, 
		long long off)
{
    __u8 *buf;
    struct iocb *iop = &iocbp->iocb;

#if defined (PDDREPLAY_DEBUG_SS) || defined(TESTVMREPLAY_DEBUG)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 
	if (rw == 0)
		assert(content != NULL);
	else
	{
		fprintf(stdout, "rw is %d\n", rw);
		assert(rw == 1);
	}
#ifdef TESTVMREPLAY_DEBUG
	fprintf(stdout, "n = %u\n", n);
#endif	
    assert((rw == 0 && content != NULL) || (rw == 1));
    assert(0 < n && (n % BLKSIZE) == 0);
    assert(0 <= off);

    if (iocbp->nbytes) {
        if (iocbp->nbytes >= n) {
            buf = iop->u.c.buf;
            goto prep;
        }

        assert(iop->u.c.buf);
        free(iop->u.c.buf);
    }

    buf = (__u8*)buf_alloc(n);
    iocbp->nbytes = n;

prep:
    if (rw)
        io_prep_pread(iop, iocbp->tip->ofd, buf, n, off);
    else {
        assert(write_enabled);
        io_prep_pwrite(iop, iocbp->tip->ofd, buf, n, off);
        touch_memory(buf, n);	//SSS: What does this do?
	memcpy(buf, content, n);
//	copycontent(&buf, content, n);
	//free(content);	//malloc was done in ?
    }

    iop->data = iocbp;
}


#ifndef TESTVMREPLAY
/**
 * iocbs_map - Map a set of AIOs onto a set of IOCBs
 * @tip: Per-thread information
 * @list: List of AIOs created
 * @pkts: AIOs to map
 * @ntodo: Number of AIOs to map
 */
void iocbs_map(struct thr_info *tip, struct iocb **list,
                         struct io_pkt *pkts, int ntodo)
{
    int i;
    struct io_pkt *pkt;
#if defined (PDDREPLAY_DEBUG_SS) || defined(REPLAYDIRECT_DEBUG_SS)
    fprintf(stdout, "In %s with io_pkt\n", __FUNCTION__);
#endif 

	assert(list != NULL && pkts != NULL);
    assert(0 < ntodo && ntodo <= naios);

    pthread_mutex_lock(&tip->mutex);
    assert(ntodo <= slist_len(&tip->free_iocbs));
    for (i = 0, pkt = pkts; i < ntodo; i++, pkt++) {
		assert(pkt != NULL);
        __u32 rw = pkt->rw;
        struct iocb_pkt *iocbp;

        if (!pkt->rw && !write_enabled)
            rw = 1;

        if (verbose > 1)
		{
            fprintf(stdout, "\t%10llu + %10lu %c%c\n",
                (unsigned long long)pkt->ioblk,
                (unsigned long)(pkt->nbytes >> 12),
                rw ? 'R' : 'W',
                (rw == 1 && pkt->rw == 0) ? '!' : ' ');
            fprintf(tip->vfp, "\t%10llu + %10lu %c%c\n",
                (unsigned long long)pkt->ioblk,
                (unsigned long)pkt->nbytes >> 12,
                rw ? 'R' : 'W',
                (rw == 1 && pkt->rw == 0) ? '!' : ' ');
		}
        iocbp = slist_entry(tip->free_iocbs.next, struct iocb_pkt, head);
        iocb_setup(iocbp, rw, pkt->nbytes, pkt->content,pkt->ioblk * BLKSIZE);
		free(pkt->content);	//malloc in get_vmbunch()

        list_move_tail(&iocbp->head, &tip->used_iocbs);
        list[i] = &iocbp->iocb;
    }

    tip->naios_free -= ntodo;
    assert(tip->naios_free >= 0);
    pthread_mutex_unlock(&tip->mutex);
}
#else
/**
 * iocbs_map - Map a set of AIOs onto a set of IOCBs
 * @tip: Per-thread information
 * @list: List of AIOs created
 * @pkts: AIOs to map
 * @ntodo: Number of AIOs to map
 */
void iocbs_map(struct thr_info *tip, struct iocb **list,
                         struct vm_pkt *pkts, int ntodo)
{
    int i;
    struct vm_pkt *pkt;
#if defined (PDDREPLAY_DEBUG_SS) || defined(TESTVMREPLAY_DEBUG)
    fprintf(stdout, "In %s with vm_pkt\n", __FUNCTION__);
#endif 

    assert(0 < ntodo && ntodo <= naios);

    pthread_mutex_lock(&tip->mutex);
    assert(ntodo <= slist_len(&tip->free_iocbs));
    for (i = 0, pkt = pkts; i < ntodo; i++, pkt++) {
        assert(pkt != NULL);
        __u32 rw = pkt->rw;
        struct iocb_pkt *iocbp;

        if (!pkt->rw && !write_enabled)
            rw = 1;

        if (verbose > 1)
        {
            fprintf(stdout, "\t%10llu + %10lu %c%c\n",
                (unsigned long long)pkt->block,
                (unsigned long)(pkt->nbytes >> 12),
                rw ? 'R' : 'W',
                (rw == 1 && pkt->rw == 0) ? '!' : ' ');
            fprintf(tip->vfp, "\t%10llu + %10lu %c%c\n",
                (unsigned long long)pkt->block,
                (unsigned long)pkt->nbytes >> 12,
                rw ? 'R' : 'W',
                (rw == 1 && pkt->rw == 0) ? '!' : ' ');
        }
        iocbp = slist_entry(tip->free_iocbs.next, struct iocb_pkt, head);
        iocb_setup(iocbp, rw, pkt->nbytes, pkt->content,pkt->block* BLKSIZE);
		free(pkt->content);	//malloc in get_vmbunch()

        list_move_tail(&iocbp->head, &tip->used_iocbs);
        list[i] = &iocbp->iocb;
#if defined (PDDREPLAY_DEBUG_SS) || defined(TESTVMREPLAY_DEBUG)
		fprintf(stdout, "vmname = %s\n", pkt->vmname);
#endif
    }

    tip->naios_free -= ntodo;
    assert(tip->naios_free >= 0);
    pthread_mutex_unlock(&tip->mutex);
}
#endif

/**
 * nfree_current - Returns current number of AIOs that are free
 *
 * Will wait for available ones...
 *
 * Returns 0 if we have some condition that causes us to exit
 */
int nfree_current(struct thr_info *tip)
{
    long nfree = 0;

    pthread_mutex_lock(&tip->mutex);
    while (!is_send_done(tip) && ((nfree = tip->naios_free) == 0)) {
        tip->send_wait = 1;
        if (pthread_cond_wait(&tip->cond, &tip->mutex)) {
            fatal("pthread_cond_wait", ERR_SYSCALL,
                "nfree_current cond wait failed\n");
            /*NOTREACHED*/
        }
    }
    pthread_mutex_unlock(&tip->mutex);

    return nfree;
}

#endif
