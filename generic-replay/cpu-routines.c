/* 
 * ========================================================================
 * ==== CPU RELATED ROUTINES ==============================================
 * ========================================================================
 */
#include <assert.h>
#include "per-input-file.h"
#include "debugg.h"

int ncpus = 0;           // Number of CPUs in the system

extern int verbose;         // Boolean: Output some extra info

/**
 * get_ncpus - Sets up the global 'ncpus' value
 */
void get_ncpus(void)
{
    cpu_set_t cpus;

    if (sched_getaffinity(getpid(), sizeof(cpus), &cpus)) {
        fatal("sched_getaffinity", ERR_SYSCALL, "Can't get CPU info\n");
        /*NOTREACHED*/
    }

    /*
     * XXX This assumes (perhaps wrongly) that there are no /holes/ 
     * XXX in the mask.
     */
    for (ncpus = 0; ncpus < CPU_SETSIZE && CPU_ISSET(ncpus, &cpus); ncpus++)
        ;
    if (ncpus == 0) {
        fatal(NULL, ERR_SYSCALL, "Insufficient number of CPUs\n");
        /*NOTREACHED*/
    }
}

#if 0
/**
 * pin_to_cpu - Pin this thread to a specific CPU
 * @tip: Thread information
 */
void pin_to_cpu(struct thr_info *tip)
{
    cpu_set_t cpus;

    assert(0 <= tip->cpu && tip->cpu < ncpus);

    CPU_ZERO(&cpus);
    CPU_SET(tip->cpu, &cpus);
    if (sched_setaffinity(getpid(), sizeof(cpus), &cpus)) {
        fatal("sched_setaffinity", ERR_SYSCALL, "Failed to pin CPU\n");
        /*NOTREACHED*/
    }

    if (verbose > 1) {
        int i;
        cpu_set_t now;

        (void)sched_getaffinity(getpid(), sizeof(now), &now);
        fprintf(tip->vfp, "Pinned to CPU %02d ", tip->cpu);
        for (i = 0; i < ncpus; i++)
            fprintf(tip->vfp, "%1d", CPU_ISSET(i, &now));
        fprintf(tip->vfp, "\n");
    }
}
#endif
