#ifndef _REPLAY_PLUGINS_H_
#define _REPLAY_PLUGINS_H_

#include <pthread.h>
#include "vmbunching_structs.h"
#include "per-input-file.h"
#include "serveio-utils.h"

/********* Parameter to pass to 
* 1. f_mapupdate_sub()
* 2. p_mapupdate_sub()
* 3. io_mapupdate_sub()
*******************************************************/
struct mapupdate_info {
//	pthread_mutex_t mutex;
//  pthread_cond_t cond;
	struct preq_spec **preql;
	struct vm_pkt *blkReq;
	int vop_iter;
	pthread_t upd_thread;
};


/************** Prototypes ****************************/
int verify_replayflags(void);
int verify_preplayflags(void);
void pdd_replay_init(void);
void pdd_replay_exit(void);
int preplay_stitch(struct preq_spec **preql, int num_vop, 
		unsigned char *simcontent);
int preplay_map(struct thr_info *tip, struct preq_spec **preql, 
				struct vm_pkt *vmpkt);
int sreplay_map(struct thr_info *tip, struct preq_spec **preql, 
				struct vm_pkt *vmpkt);
int ioreplay_map(struct preq_spec **preql, int numpkts);
int freplay_map(struct thr_info *tip, struct preq_spec **preql, 
				struct vm_pkt *vmpkt);
int preql_map(struct preq_spec **preql, struct io_pkt **iopl, int vop_iter,
				int disk_iter);
int next_io_tip(struct thr_info *tip, struct vmreq_spec *spec);

#endif /* _REPLAY_PLUGINS_H_ */
