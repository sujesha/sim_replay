#ifndef _IOCBS_H_
#define _IOCBS_H_


/*
 * Every Asynchronous IO used has one of these (naios per file/device).
 *
 * @iocb:   IOCB sent down via io_submit
 * @head:   Linked onto file_list.free_iocbs or file_list.used_iocbs
 * @tip:    Pointer to per-thread information this IO is associated with
 * @nbytes: Number of bytes in buffer associated with iocb
 */
struct iocb_pkt {
    struct iocb iocb;
    struct slist_head head;
    struct thr_info *tip;
    __u16 nbytes;
};


#endif /* _IOCBS_H_ */
