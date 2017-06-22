#ifndef _REPLAY_GENERIC_H_
#define _REPLAY_GENERIC_H_

#include "vmbunching_structs.h"

inline __u64 gettime(void);
inline void __set_cv(pthread_mutex_t *pmp, pthread_cond_t *pcp,
                volatile int *vp,
                __attribute__((__unused__))int mxv);
inline void __wait_cv(pthread_mutex_t *pmp, pthread_cond_t *pcp,
                 volatile int *vp, int mxv);
inline void set_replay_ready(void);
inline void wait_replays_ready(void);
inline void set_replay_done(void);
inline void wait_replays_done(void);
inline void set_iter_done(void);
inline void wait_iters_done(void);
inline void wait_iter_start(void);
inline void start_iter(void);
void stall(struct thr_info *tip, long long oclock);
inline int is_send_done(struct thr_info *tip);

int next_bunch(struct thr_info *tip, struct vm_bunch *bunch);

#endif /* _REPLAY_GENERIC_H_*/
