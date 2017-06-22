#include <linux/types.h>

char pddversion[] = "0.1.0";
int pddver_mjr = 0;
int pddver_mnr = 1;
int pddver_sub = 0;


inline __u64 mk_pddversion(int mjr, int mnr, int sub)
{
    return ((mjr & 0xff) << 16) | ((mnr & 0xff) << 8) | (sub & 0xff);
}

inline void get_pddversion(__u64 version, int *mjr, int *mnr, int *sub)
{
    *mjr = (int)((version >> 16) & 0xff);
    *mnr = (int)((version >>  8) & 0xff);
    *sub = (int)((version >>  0) & 0xff);
}

