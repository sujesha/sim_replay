/* Ignoring multiple threads on multiple CPU, and multiple devices
* and multiple hostnames for the time-being. Doing just
* single threaded on single input trace file.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/param.h>
#include <getopt.h>
#include <assert.h>
#include <signal.h>
#include <fcntl.h>
#include "debugg.h"
#include "debug.h"
#include "per-input-file.h"
#include "replay-plugins.h"
#include "pdd_config.h"
#include "v2c-map.h"
#include "v2f-map.h"
#include "p2d-map.h"
#include "v2p-map.h"
#include "c2pv-map.h"
#include "f2pv-map.h"
#include "d2pv-map.h"
#include "mapdump.h"
#include "fmapdump.h"
#include "replay-generic.h"
#include "pro_outputhashfn.h"
#include "pro_outputtimefn.h"
#include "contentcache.h"
#include "sectorcache.h"
#include "lru_cache.h"
#include "vmfile-handling.h"
#include "replay-defines.h"
#include "content-simfile.h"
#include "contentdedup-API.h"
#include "blkidtab-API.h"

/* 
 * ========================================================================
 * ==== GLOBAL VARIABLES ==================================================
 * ========================================================================
 */
static char build_date[] = __DATE__ " at "__TIME__;
char ifull_name[MAXPATHLEN];
LIST_HEAD(input_files_replay);      // List of input files to replay
LIST_HEAD(input_files_warmup);      // List of input files to warmup cache
int nfiles = 0;          // Number of files to handle
volatile int signal_done = 0;    // Boolean: Signal'ed, need to quit

char *ibase = "replay";      // Input base name
char *ifile = NULL;
char *wfile = NULL;
char *idir = ".";        // Input directory base
char *idevnm = NULL;	// Input device name
int def_iterations = 1;      // Default number of iterations
int verbose = 0;         // Boolean: Output some extra info
int read_enabled = 0;       // Boolean: Enable reading
int write_enabled = 0;       // Boolean: Enable writing
int cpus_to_use = -1;        // Number of CPUs to use

/* For scanning the trace file itself, instead of disk */
char ifull_name[MAXPATHLEN];
static LIST_HEAD(input_files);          // List of input files to "scan"

char wfull_name[MAXPATHLEN];
extern struct ifile_info *iip;
extern struct ifile_info *wip;	/* file for cache warm-up */

int vmbunchreplay = 0;
int warmupflag = 0;
int intraonlyflag = 0;	/* Simulating both intra and inter, by default */

extern int collectformat;

extern __u32 dedupfactor[];
extern __u32 max_dedupfactor;
extern char pddversion[];

extern int naios;         // Number of AIOs per thread
extern int ncpus;           // Number of CPUs in the system
extern __u64 genesis;      // Earliest time seen
extern __u64 rgenesis;          // Our start time
extern size_t pgsize;           // System Page size
extern int no_stalls;       // Boolean: Disable pre-stalls
extern int speedupfactor;	//Speedup for stalls, default no speedup
extern int find_records;        // Boolean: Find record files auto

/* Extern'ed from replay-plugins.c */
extern int freplayflag;
extern int preplayflag;
extern int sreplayflag;    /* default */
extern int ioreplayflag;

/* Extern'ed from scandisk.c */
extern int scanharddiskp;
extern int scanharddisks;
extern int initmapfromfile;

/* Extern'ed from fruntime.c */
extern int runtimemap;

/* Extern'ed from sim-replay-generic.c */
extern int cachesimflag;

/* Extern'ed from sync-disk-interface.c */
extern int disksimflag;

/* Extern'ed from sim-replay-generic.c */
extern int writebackflag;

/* Extern'ed from sectorcache.c */
extern int RAMsize_MB;

/* Extern'ed from contentcache.c */
extern int CCACHEsize_MB;

/* Extern'ed from cparse */
extern int collectformat;

extern FILE *voltfp, *v2pfp;
extern FILE *v2ffp;
extern FILE *f2pvfp;
extern FILE *v2cfp;
extern FILE *c2pvfp;

/* Extern'ed from v2p-map.c */
extern char *V2PmapFile;
extern char *default_V2PmapFile;
extern FILE *mapp;
extern FILE *writemapp;

extern FILE * ftimeptr;
extern FILE * fhashptr;

extern __u32 pcollisions;
extern __u32 pcollisionstp;
extern __u32 pzerocollisions;
extern __u32 pcollisionsdiffclen;
extern __u32 fcollisions;
extern __u32 fcollisionstp;
extern __u32 fzeros;

__u64 compulsory_misses = 0;
__u64 capacity_misses = 0;

extern __u64 vmap_hits;
extern __u64 vmap_misses;
extern __u64 vmap_dirties;
extern __u64 vmapmiss_cachehits;
extern __u64 vmapdirty_cachehits;
extern __u64 vmapmiss_cachemisses;

extern __u64 fmapmiss_cachehits;
extern __u64 fmapdirty_cachehits;
extern __u64 fmapmiss_cachemisses;
extern __u64 bcache_hits;
extern __u64 bcache_misses;
extern __u64 bcache_hits_r;
extern __u64 bcache_hits_w;
extern __u64 bcache_misses_r;
extern __u64 bcache_misses_w;
extern __u64 ccache_hits;
extern __u64 ccache_misses;
extern __u64 ccache_hits_r;
extern __u64 ccache_hits_w;
extern __u64 ccache_misses_r;
extern __u64 ccache_misses_w;
extern __u64 cmap_hits;
extern __u64 cmap_misses;
extern __u64 cmap_dirties;
extern __u64 fmap_hits;
extern __u64 fmap_misses;
extern __u64 fmap_dirties;
extern __u64 fixed_dedup_hits;
extern __u64 fixed_self_hits;
extern __u64 fixed_dedup_misses;
extern __u64 fixed_self_misses;
extern __u64 fmap_self_is_leader;
extern __u64 fmap_self_is_not_leader;
extern __u64 disk_hits;
extern __u64 disk_hits_r;
extern __u64 disk_hits_w;
extern __u64 ccache_dedup_hits;
extern __u64 ccache_nondedup_hits;
extern __u64 ccache_dedup_misses;
extern __u64 ccache_nondedup_misses;
extern __u64 cmap_self_is_leader;
extern __u64 cmap_self_is_not_leader;

extern Node * currReusableFixedIDUList;
extern Node * currReusableDedupIDUList;
extern Node * currReusableChunkIDUList;
extern Node * newReusableFixedIDUList;
extern Node * newReusableDedupIDUList;
extern Node * newReusableChunkIDUList;

/* extern'ed from mapdump.c */
extern int mapdumpflag;

void sync_disk_exit(void);
int write_input_v2p_mapping(void);

/* Extern'ed from sim-replay-generic.c */
extern unsigned long gtotalreadreq;    /* # Read requests in trace */
extern unsigned long gtotalwritereq;   /* # Write requests in trace */

/* Stats for sreplay */
#ifdef PRO_STATS
    extern unsigned long stotalreq;    /* Including read/write reqs */
    extern unsigned long stotalblk;    /* Including read/write blks */

    extern unsigned long stotalreadreq;    /* Read req received */
    extern unsigned long stotalwritereq;   /* Write req received */

    extern unsigned long stotalblkread;    /* Count of blks to-be-read */
    extern unsigned long stotalblkwrite;   /* Count of blks to-be-written */

#endif

/* Stats for preplay */
#ifdef PRO_STATS
    extern unsigned long ptotalreq; /* Including read/write reqs */
    extern unsigned long ptotalblk; /* Including read/write blks */

    extern unsigned long porigblkread;  /* Original blks-to-be-read */
    extern unsigned long porigblkwrite; /* Original blks-to-be-written */

    extern unsigned long ptotalreadreq; /* Read req received */
    extern unsigned long ptotalwritereq;    /* Write req received */

    extern unsigned long ptotalblkread; /* Count of blks to-be-read */
    extern unsigned long ptotalblkwrite;    /* Count of blks to-be-written */
	extern unsigned long pro_zeroblksread;	/* Count of zeroblks so-not-to-be-read*/

    extern unsigned long pro_blkread;  /* Blk read on PROVIDED success */
    extern unsigned long pro_fallback_blkread; /* Blk read on PROVIDED fail */

    extern unsigned long chunking_blkread; /*Blk read for write chunking*/
#endif

/* Stats for freplay */
#ifdef PRO_STATS
	extern unsigned long ctotalreq;	/* Including read/write reqs */
	extern unsigned long ctotalblk;	/* Including read/write blks */

	extern unsigned long corigblkread;	/* Original blks-to-be-read */
	extern unsigned long corigblkwrite;	/* Original blks-to-be-written */

	extern unsigned long ctotalreadreq;	/* Read req received */
	extern unsigned long ctotalwritereq;	/* Write req received */

	extern unsigned long ctotalblkread;	/* Count of blks to-be-read */
	extern unsigned long ctotalblkwrite;	/* Count of blks to-be-written */
	extern unsigned long con_zeroblksread;	/* Count of zeroblks so-not-to-be-read*/

	extern unsigned long con_blkread;	/* Blk read on CONFIDED success */
	extern unsigned long con_fallback_blkread;	/* Blk read on CONFIDED fail */
#endif

/* Stats for ioreplay */
#ifdef PRO_STATS
	extern unsigned long iototalreq;	/* Including read/write reqs */
	extern unsigned long iototalblk;	/* Including read/write blks */

	extern unsigned long iorigblkread;	/* Original blks-to-be-read */
	extern unsigned long iorigblkwrite;	/* Original blks-to-be-written */

	extern unsigned long iototalreadreq;	/* Read req received */
	extern unsigned long iototalwritereq;	/* Write req received */

	extern unsigned long iototalblkread;	/* Count of blks to-be-read */
	extern unsigned long iototalblkwrite;	/* Count of blks to-be-written */
	extern unsigned long io_zeroblksread;	/* Count of zeroblks so-not-to-be-read*/

	extern unsigned long io_blkread;	/* Blk read on IODEDUP success */
	extern unsigned long io_fallback_blkread;	/* Blk read on IODEDUP fail */
#endif

//inline __u64 gettime(void);
void get_ncpus(void);
inline void wait_replays_ready(void);
inline void start_iter(void);
inline void wait_iters_done(void);
inline void wait_replays_ready(void);
inline void wait_replays_done(void);
void rem_input_file(struct thr_info *tip);

#ifdef ASYNCIO
	inline void wait_reclaims_done(void);
#endif

/**
 * set_signal_done - Signal handler, catches signals & sets signal_done
 */
static void set_signal_done(__attribute__((__unused__))int signum)
{
	fprintf(stdout, "caught signal\n");
    signal_done = 1;
}

/**
 * setup_signal - Set up a signal handler for the specified signum
 */
static inline void setup_signal(int signum, sighandler_t handler)
{
    if (signal(signum, handler) == SIG_ERR) {
        fatal("signal", ERR_SYSCALL, "Failed to set signal %d\n",
            signum);
        /*NOTREACHED*/
    }
}

/* 
 * ========================================================================
 * ==== COMMAND LINE ARGUMENT HANDLING ====================================
 * ========================================================================
 */

static char usage_str[] =                       \
        "\n"                                                           \
        "\t[ -A        : --intra-only-dedup		] Default: 0\n"        \
        "\t[ -b        : --disk-simulation     ] Default: 0\n"        \
        "\t[ -c        : --cache-simulation     ] Default: 0\n"        \
        "\t[ -C        : --collect.ko-format     ] Default: 0\n"        \
        "\t[ -d <dir>  : --input-directory=<dir> ] Default: .\n"        \
        "\t[ -D <dev>  : --input-device=<dev> 	] Default: sda\n"        \
        "\t[ -e        : --read-enable          ] Default: Off\n"      \
        "\t[ -E        : --write-enable          ] Default: Off\n"      \
        "\t[ -f <file> : --input-file=<file> 	] \n"        \
        "\t[ -h        : --help                  ] Default: Off\n"      \
        "\t[ -i  	   : --initmapfromfile  	] Default: Off\n"      \
        "\t[ -I <iters>: --iterations=<iters>    ] Default: 1\n"        \
        "\t[ -M <file> : --map-devs=<file>       ] Default: None\n"     \
        "\t[ -m <file> : --mapv2p=<file>       	 ] Default: v2p_map.txt\n"     \
        "\t[ -F        : --stall-factor          ] Default: None\n"      \
        "\t[ -S        : --scanhardisk-parallel  ] Default: Off\n"      \
        "\t[ -s        : --scanhardisk-sequential] Default: Off\n"      \
        "\t[ -r        : --runtimemap			 ] Default: Off\n"      \
        "\t[ -O <in-MB>: --overall-RAM-size=<in-MB>] Default:1024\n"     \
        "\t[ -o <in-MB>: --iodedup-contentcache-size=<in-MB>] Default:200\n"     \
        "\t[ -P        : --preplayflag			 ] Default: Off\n"      \
        "\t[ -Q        : --sreplayflag			 ] Default: On\n"      \
        "\t[ -R        : --freplayflag			 ] Default: Off\n"      \
        "\t[ -T        : --ioreplayflag			 ] Default: Off\n"      \
        "\t[ -v        : --verbose               ] Default: Off\n"      \
        "\t[ -V        : --version               ] Default: Off\n"      \
        "\t[ -w        : --writeback-cache		 ] Default: writethrough\n"      \
        "\t[ -W        : --warmup-cache		 ] Default: Off\n"      \
        "\t<dev...>                                Default: None\n"     \
        "\n";

#define S_OPTS  "AbcCd:D:eEf:F:hiI:m:SsO:o:PQrRTvVwW:"
static struct option l_opts[] = {
    {
        .name = "intra-only-dedup",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'A'
    },
    {
        .name = "disk-simulation",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'b'
    },
    {
        .name = "cache-simulation",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'c'
    },
    {
        .name = "collect.ko-format",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'C'
    },
    {
        .name = "input-directory",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'd'
    },
    {
        .name = "device-name",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'D'
    },
    {
        .name = "read-enable",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'e'
    },
    {
        .name = "write-enable",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'E'
    },
    {
        .name = "input-file",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'f'
    },
    {
        .name = "stall-factor",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'F'
    },
    {
        .name = "help",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'h'
    },
    {
        .name = "iterations",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'I'
    },
    {
        .name = "initmapfromfile",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'i'
    },
    {
        .name = "mapv2p",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'm'
    },
    {
        .name = "scanharddisk-parallel",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'S'
    },
    {
        .name = "scanharddisk-sequential",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 's'
    },
    {
        .name = "runtimemap",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'r'
    },
    {
        .name = "overall-RAM-size",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'O'
    },
    {
        .name = "iodedup-contentcache-size",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'o'
    },
    {
        .name = "preplayflag",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'P'
    },
    {
        .name = "sreplayflag",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'Q'
    },
    {
        .name = "freplayflag",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'R'
    },
    {
        .name = "verbose",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'v'
    },
    {
        .name = "version",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'V'
    },
    {
        .name = "writeback-cache",
        .has_arg = no_argument,
        .flag = NULL,
        .val = 'w'
    },
    {
        .name = "warmup-cache",
        .has_arg = required_argument,
        .flag = NULL,
        .val = 'W'
    },
    {
        .name = NULL
    }
};


/**
 * usage - Display usage string and version
 */
static inline void usage(void)
{
    fprintf(stdout, "Usage: sim_replay -- version %s\n%s",
        pddversion, usage_str);
}

/**
 * handle_args: Parse passed in argument list
 * @argc: Number of arguments in argv
 * @argv: Arguments passed in
 *
 * Does rudimentary parameter verification as well.
 */
static void handle_args(int argc, char *argv[])
{
    int c;
#if defined(RECLAIM_DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    while ((c = getopt_long(argc, argv, S_OPTS, l_opts, NULL)) != -1) {
        switch (c) {
		case 'A':
			/* Perform dedup for intra-VM similarity only.
			 * By default, if there are multiple VMs I/O in the trace file,
			 * both intra-VM and inter-VM dedup is done.
			 * Use of this flag indicates that intra similarity only has to
			 * be identified. If trace is for single VM, this flag is
			 * immaterial.
			 */
			 intraonlyflag = 1;

        case 'b':
			/* no need for actual disk recreate & disk read/write */
			disksimflag = 1; 
            break;

        case 'c':
			cachesimflag = 1;
            break;

		case 'C': 
			/* Logs in Koller's collect.ko format => content is hex hash */
			collectformat = 1;
			break;

        case 'd':
			/* This is the input directory where traces for replay 
			 * are present.
			 */
            idir = optarg;
            if (access(idir, R_OK | X_OK) != 0) {
                fatal(idir, ERR_ARGS,
                      "Invalid input directory specified\n");
                /*NOTREACHED*/
            }
            break;

		case 'D':
			/* This is input device name, default is present in DISKNAME */
			idevnm = optarg;
			break;

        case 'f':
			/* This is the input file containing traces for replay */
            ifile = optarg;
            break;

        case 'W':
			/* This is the input file containing traces for cache warmup */
            wfile = optarg;
			warmupflag = 1;
            break;

        case 'F':
			/* Input of stall factor, default no-speedup */
            no_stalls = 0;
			speedupfactor = atoi(optarg);
            if (speedupfactor <= 0) {
                fprintf(stderr,
                    "Invalid stall speedup factor %d\n",
                    speedupfactor);
                exit(ERR_ARGS);
                /*NOTREACHED*/
            }
			printf("speedupfactor = %d\n", speedupfactor);
            break;

        case 'h':
            usage();
            exit(0);
            /*NOTREACHED*/

        case 'I':
            def_iterations = atoi(optarg);
            if (def_iterations <= 0) {
                fprintf(stderr,
                    "Invalid number of iterations %d\n",
                    def_iterations);
                exit(ERR_ARGS);
                /*NOTREACHED*/
            }
            break;

		case 'i':
			/* Set this flag to request reading up of mappings from input
			 * file, before starting the read/write request servicing
			 * of PROVIDED. 
			 * This flag is valid only if preplayflag=1 or freplayflag=1 or
			 * 		ioreplayflag=1
			 * One and only one of 'S', 's', 'i' and 'r' flags to be used.
			 */
			initmapfromfile = 1;
			break;

		case 'm':
			/* Input of file containing V2P mappings 
			 * If not specified, default file is default_V2PmapFile			 
			 */
			V2PmapFile = strdup(optarg);
			break;

		case 'O': 
			/* Use this option to specify the RAM size in MB. Default is 1024=>1GB */
			RAMsize_MB = atoi(optarg);
			break;

		case 'o':
			/* Use this option to specify the content cache size for IODEDUP */
			CCACHEsize_MB = atoi(optarg);
			break;

		case 'P':
			/* Set this flag to enable PROVIDED module during replay 
			 * Only one of the replay flags should be 1
			 */
			preplayflag = 1;
			sreplayflag = 0;
			break;

		case 'Q':
			/* Set this flag to perform STANDARD replay.
			 * Only one of the replay flags should be 1
			 */
			sreplayflag = 1;
			break;

		case 'R':
			/* Set this flag to enable CONFIDED module during replay 
			 * Only one of the replay flags should be 1
			 */
			freplayflag = 1;
			sreplayflag = 0;
			break;

		case 'T':
			/* Set this flag to enable IODEDUP module during replay 
			 * Only one of the replay flags should be 1
			 */
			ioreplayflag = 1;
			sreplayflag = 0;
			break;

		case 'r':
			/* Set this flag to request runtimemap creation, from ongoing
			 * reads & writes, instead of scanning apriori. This can be 
			 * for CONFIDED/PROVIDED/IODEDUP. This flag is valid only if
			 * preplayflag = 1 or freplayflag = 1 or ioreplayflag=1
			 * One and only one of 'S', 's', 'i', 'r' flags to be used.
			 */
			runtimemap = 1;
			break;

		case 'S':
			/* Set this flag to request scanning and map building from
			 * hard-disk, in parallel to the read/write request servicing
			 * of PROVIDED/CONFIDED. This flag is valid only if 
			 * preplayflag = 1 or freplayflag = 1 or ioreplayflag=1
			 * One and only one of 'S', 's', 'i', 'r' flags to be used.
			 */
			scanharddiskp = 1;
			break;

		case 's':
			/* Set this flag to request scanning and map building from
			 * hard-disk, in sequence before the read/write request servicing
			 * of PROVIDED/CONFIDED. This flag is valid only if 
			 * preplayflag = 1 or freplayflag = 1 or ioreplayflag=1
			 * One and only one of 'S', 's', 'm', 'r' flags to be used.
			 */
			scanharddisks = 1;
			break;

        case 'V':
            fprintf(stdout, "sim_replay -- version %s\n",
                pddversion);
            fprintf(stdout, "            Built on %s\n",
                build_date);
            exit(0);
            /*NOTREACHED*/

        case 'v':
            fprintf(stdout, "verbose requested\n");
            verbose++;
            break;

        case 'e':
			/* This flag enables block reads, disabled by default */
            read_enabled = 1;
            break;

        case 'E':
			/* This flag enables block writes, disabled by default */
            write_enabled = 1;
            break;

        default:
            usage();
            fatal(NULL, ERR_ARGS,
                  "Invalid command line argument %c\n", c);
            /*NOTREACHED*/
        }
    }
#if defined(DDREPLAY_DEBUG_SS ) || defined(RECLAIM_DEBUG_SS)
	    fprintf(stdout, "input options over\n");
#endif

   	if (V2PmapFile == NULL)
       	V2PmapFile = default_V2PmapFile;
#if defined(PDDREPLAY_DEBUG_SS) || defined(RECLAIM_DEBUG_SS)
	    fprintf(stdout, "V2PmapFile = %s\n", V2PmapFile);
#endif

	/* Options are available to disable read/write requests. This is just
	 * a check that both are not disabled at the same time
	 */
	if (!read_enabled && !write_enabled)
	{
		usage();
		fprintf(stdout, "both reads and writes are disabled, fix this\n");
		fprintf(stdout, "use -e for reads and -E for writes\n");
		fatal(NULL, ERR_ARGS, "Missing option -e or -E\n");
	}

#if 0
#ifndef METADATAUPDATE_UPON_WRITES
	if (!read_enabled && write_enabled && 
			(freplayflag || ioreplayflag || preplayflag))
	{
		usage();
		fprintf(stdout, "write-only replay with metadata-updates disabled "
				"is fruitless, isnt it?\n");
		fatal(NULL, ERR_ARGS, "Check METADATAUPDATE_UPON_WRITES in Makefile\n");
	}
#endif	
#endif	

	/* This simulator can perform 4 types of replay, but only one at a time:-
	 * 1. STANDARD or Vanilla
	 * 2. IODEDUP
	 * 3. CONFIDED
	 * 4. PROVIDED
	 */
	if (!verify_replayflags())
	{
		usage();
		fatal(NULL, ERR_ARGS, "One & only one of the replay flags may be 1\n");
	}

#ifdef NONSPANNING_PROVIDE
#ifdef SPANIMMEDIATE_PROVIDE
	if (preplayflag)
	{
		fatal(NULL, ERR_COMPILE, "In Makefile, specify either "
			"SPANIMMEDIATE_PROVIDE or NONSPANNING_PROVIDE, not both\n");
	}
#endif
#endif

	/* During IODEDUP, CONFIDED and PROVIDED, mapping information is
	 * required by scanning or by initializing from file, but not in STANDARD
	 */
	if (preplayflag != 1 && freplayflag != 1 && ioreplayflag != 1 &&
		(scanharddiskp || initmapfromfile || scanharddisks || runtimemap))
	{
		usage();
		fatal(NULL, ERR_ARGS, "Flags scanharddiskp, scanharddisks, " \
					"initmapfromfile valid only if and only if " \
					"preplayflag/freplayflag/ioreplayflag = 1\n");
	}
	/* During IODEDUP, CONFIDED and PROVIDED, mapping information is
	 * to be built either by scanning or from file, not both
	 */
	if ((preplayflag || freplayflag || ioreplayflag) && !verify_preplayflags())
	{
		usage();
		fatal(NULL, ERR_ARGS, "Check flags for PROVIDED/CONFIDED/IODEDUP\n");
	}

	/* In case of a write-back sector-cache, the IODEDUP write request 
	 * response time would not include the time to update mapping metadata,
	 * and would otherwise. Since we are only working with a simulation here,
	 * and dont have real implementation of IODEDUP, so a write-back cache
	 * can only be "simulated" right now.
	 */
	if (writebackflag && !cachesimflag && ioreplayflag)
	{
		usage();
		fatal(NULL, ERR_ARGS, "Write-back setting for cache is valid only if " \
			"base-cache is being simulated in the first place!\n");
	}

	/* This is a cache-simulation module so far, needs more work if cache
	 * simulation has to be removed and underlying cache has to be used.
	 */
	if (!cachesimflag)
	{
		usage();
		fatal(NULL, ERR_ARGS, "Use option -c for cache simulation. "\
			"Currently, testing has been done only with " \
			"cache-simulation settings. If cache simulation not to be " \
			"done, do appropriate testing before using.\n");
	}

	if (ioreplayflag && CCACHEsize_MB <= 0)
	{
		usage();
		fatal(NULL, ERR_ARGS, "Specify non-negative value for contentcache\n");
	}

	if (RAMsize_MB <= 0 && cachesimflag)
	{
		usage();
		fatal(NULL, ERR_ARGS, "Specify non-negative value for sectorcache\n");
	}

	if (ioreplayflag && CCACHEsize_MB >= RAMsize_MB)
	{
		usage();
		fatal(NULL, ERR_ARGS, "content-cache size should be < sector-cache\n");
	}

	fprintf(stdout, "RAM-size = %d (MB)\n", RAMsize_MB);
	if (ioreplayflag)
		fprintf(stdout, "CCACHE-size = %d (MB)\n", CCACHEsize_MB);

	/* If device name not specified via command-line, it may still have 
	 * been specified via compile-time argument (DISKNAME). If even not
	 * that, then throw "missing argument" error.
	 * Note that, DISKNAME is needed only if disk scanning is to be done, 
	 * and disk is not being simulated, else need trace file as input for
	 * scanning.
	 */
    if (idevnm == NULL) 
	{
#ifdef DISKNAME			
		if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT)
		{
			usage();
			fatal(NULL, ERR_ARGS, "For scanning with disk sim, need input trace"
					"file using option -D");
		}
		idevnm = strdup(xstringify(DISKNAME));
#else
		usage();
        fatal(NULL, ERR_ARGS, "Missing required input dev name.\n");
        /*NOTREACHED*/
#endif
    }
	else
	{
		if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT)
		{
			if (strstr(idevnm, "pioevents") == NULL)
			{
				usage();
				fatal(NULL, ERR_ARGS, 
						"Specify pioevents name for scan, not device name!\n");
			}
			if (strstr(idevnm, "vmbunch") != NULL)
			{
				usage();
				fatal(NULL, ERR_ARGS, 
						"Specify pioevents name, not vmbunch name!\n");
			}
			vmbunchreplay = 0;
			vm_add_input_file(idevnm, ifull_name, &iip);
		}
		else
		{
			if (strstr(idevnm, "dev") != NULL)
			{
				usage();
				fatal(NULL, ERR_ARGS, 
						"Specify only the device name, example sda\n");
			}
			vmbunchreplay = 1;
		}
	}
#if defined(PDDREPLAY_DEBUG_SS) || defined(RECLAIM_DEBUG_SS)
		/* Printing the device name for debugging purposes */
	    fprintf(stdout, "idevnm = %s\n", idevnm);
#endif

	if (DISKSIM_SCANNINGTRACE_COLLECTFORMAT)
	{
		if (ifile != NULL)
		{
			if (strstr(ifile, "pioevents") == NULL)
			{
				usage();
				fatal(NULL, ERR_ARGS, 
						"Specify pioevents name for replay, not vmbunch!\n");
			}
			sprintf(ifull_name, "%s/%s", idir, ifile);
            if (access(ifull_name, R_OK) != 0) {
                fatal(ifull_name, ERR_ARGS,
                      "Invalid input file specified\n");
                /*NOTREACHED*/
            }
		}
		else //idevnm != NULL if we reached here
		{
			sprintf(ifull_name, "%s/%s", idir, idevnm);
            if (access(ifull_name, R_OK) != 0) {
                fatal(ifull_name, ERR_ARGS,
                      "Invalid input file specified\n");
                /*NOTREACHED*/
            }
		}
	}
	else
	{
		if (ifile != NULL)
		{
			if (!strstr(ifile, "vmbunch") && !strstr(ifile, "pioevents"))
			{
				usage();
				fatal(NULL, ERR_ARGS, 
					"Specify vmbunch/pioevents name for replay!\n");
			}
			if (strstr(ifile, "vmbunch"))
				vmbunchreplay = 1;
			else
				vmbunchreplay = 0;
			sprintf(ifull_name, "%s/%s", idir, ifile);
            if (access(ifull_name, R_OK) != 0) {
                fatal(ifull_name, ERR_ARGS,
                      "Invalid input file specified\n");
                /*NOTREACHED*/
            }
		}
		else
		{
			usage();
			fatal(NULL, ERR_ARGS, "Use option -f for input vmbunch file\n");
		}
	}
#if defined(PDDREPLAY_DEBUG_SS) || defined(RECLAIM_DEBUG_SS)
	/* Printing the input file name for debugging purposes */
    fprintf(stdout, "infile = %s\n", ifile);
#endif

#ifndef INCONSISTENT_TRACES
	if (!strstr(ifile, "consistent"))
	{
		usage();
		fprintf(stderr, "since input file %s is not .consistent, better"
				" use -DINCONSISTENT_TRACES in the Makefile\n", ifile);
		fatal(NULL, ERR_ARGS, "input consistent input file or "
				"use -DINCONSISTENT_TRACES in Makefile\n");
	}
#else
	if (strstr(ifile, "consistent") || strstr(ifile, "repeattimes") ||
			strstr(ifile, "fructify") || strstr(ifile, "insertwrite"))
	{
		usage();
		fprintf(stderr, "since input file %s is .consistent, better"
				" remove -DINCONSISTENT_TRACES in the Makefile\n", ifile);
		fatal(NULL, ERR_ARGS, "input is consistent input file so "
				"remove -DINCONSISTENT_TRACES in Makefile\n");
	}
#endif

	/* The warmupflag is set in the beginning, during the warm-up phase,
	 * after the entire file for warm-up has been replayed, the flag is reset
	 */
	if (warmupflag)
	{
		if (!strstr(wfile, "pioevents"))
		{
			usage();
			fatal(NULL, ERR_ARGS,
					"specify pioevents for cache-warmup\n");
		}
		sprintf(wfull_name, "%s/%s", idir, wfile);
		if (access(wfull_name, R_OK) != 0)
		{
			fatal(wfull_name, ERR_ARGS,
					"Invalid warmup file specified\n");
			/*NOTREACHED*/
		}
		vm_add_input_file(wfile, wfull_name, &wip);
	}

	if (preplayflag && runtimemap)
	{
#ifndef SIMULATED_DISK
		usage();
		fatal(NULL, ERR_ARGS, 
				"SIMULATED_DISK in Makefile needed for PROVIDED runtime!\n");
#endif
#ifdef INCONSISTENT_TRACES
		usage();
		fatal(NULL, ERR_ARGS,
				"consistent traces are needed for PROVIDED runtime!\n");
#endif
	}

	if (cachesimflag)
		sectorcache_init();

	if (ioreplayflag)
		contentcache_init();

#if 0
	if (scanharddisks)
	{
#if defined(PDDREPLAY_DEBUG_SS) || defined(RECLAIM_DEBUG_SS)
	    fprintf(stdout, "trying hard-disk sequential scan\n");
#endif
		char ch;
		printf("Since you have opted to do hard-disk scanning sequentially,"
			" there is the option to save the constructed mappings to"
			" an output file to be used in future runs using option -i\n");
		printf("So, do you want to save the mappings to file? (y/n): ");
		ch = getchar();
		while (ch != 'y' && ch != 'n')
		{
			printf("Do you want to save the mappings to file? (y/n): ");
			ch = getchar();
		}
		if (ch == 'y')
		{
			mapdumpflag = 1;
			printf("You have requested mappings to be dumped after scanning.\n");
			if (preplayflag)
				readinput_map_filenames();
			else if (freplayflag)
				readinput_map_filenamesF();
		}
	}
    else if (initmapfromfile)
    {
		printf("You have requested mappings to be inited from files.\n");
		if (preplayflag)
			readinput_map_filenames();
		else if (freplayflag)
			readinput_map_filenamesF();
    }
#endif

	return;
}

void sim_replay_exit(void)
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
	if (preplayflag || freplayflag || ioreplayflag)
	{
		int temp;
		fprintf(stdout, "max_dedupfactor = %u\n", max_dedupfactor);
		ACCESSTIME_PRINT("max_dedupfactor = %u\n", max_dedupfactor);
		for (temp=1; temp<10; temp++)
		{
			fprintf(stdout, "dedupfactor[%d] = %u\n", temp, dedupfactor[temp]);
			ACCESSTIME_PRINT("dedupfactor[%d] = %u\n", temp, dedupfactor[temp]);
		}
	}
#ifdef PRO_STATS
	if (preplayflag)
	{
		/* Stats for preplay */
		fprintf(stdout, "ptotalreq = %lu, ", ptotalreq);
		fprintf(stdout, "ptotalblk = %lu\n", ptotalblk);
		fprintf(stdout, "ptotalreadreq = %lu, ", ptotalreadreq);
		fprintf(stdout, "ptotalwritereq = %lu\n", ptotalwritereq);
		fprintf(stdout, "porigblkread = %lu, ", porigblkread);
		fprintf(stdout, "porigblkwrite = %lu (ORIGINAL)\n", porigblkwrite);
		fprintf(stdout, "ptotalblkread = %lu, ", ptotalblkread);
		fprintf(stdout, "ptotalblkwrite = %lu (PROVIDED)\n", ptotalblkwrite);
		fprintf(stdout, "pro_zeroblksread = %lu ", pro_zeroblksread);
		fprintf(stdout, "pro_fallback_blkread = %lu (PROVIDED)\n", pro_fallback_blkread);
		fprintf(stdout, "pcollisions = %u\n", pcollisions);

		ACCESSTIME_PRINT("ptotalreq = %lu, ", ptotalreq);
		ACCESSTIME_PRINT("ptotalblk = %lu\n", ptotalblk);
		ACCESSTIME_PRINT("ptotalreadreq = %lu, ", ptotalreadreq);
		ACCESSTIME_PRINT("ptotalwritereq = %lu\n", ptotalwritereq);
		ACCESSTIME_PRINT("porigblkread = %lu, ", porigblkread);
		ACCESSTIME_PRINT("porigblkwrite = %lu (ORIGINAL)\n", porigblkwrite);
		ACCESSTIME_PRINT("ptotalblkread = %lu, ", ptotalblkread);
		ACCESSTIME_PRINT("ptotalblkwrite = %lu (PROVIDED)\n", ptotalblkwrite);
		ACCESSTIME_PRINT("pro_zeroblksread = %lu ", pro_zeroblksread);
		ACCESSTIME_PRINT("pro_fallback_blkread = %lu (PROVIDED)\n", pro_fallback_blkread);
		ACCESSTIME_PRINT("pcollisions = %u\n", pcollisions);
	}
	else if (freplayflag)
	{
		/* Stats for freplay */
		fprintf(stdout, "ctotalreq = %lu, ", ctotalreq);
		fprintf(stdout, "ctotalblk = %lu\n", ctotalblk);
		fprintf(stdout, "ctotalreadreq = %lu, ", ctotalreadreq);
		fprintf(stdout, "ctotalwritereq = %lu\n", ctotalwritereq);
		fprintf(stdout, "ctotalblkread = %lu, ", ctotalblkread);
		fprintf(stdout, "ctotalblkwrite = %lu (CONFIDED)\n", ctotalblkwrite);
		fprintf(stdout, "con_zeroblksread = %lu ", con_zeroblksread);
		fprintf(stdout, "con_fallback_blkread = %lu (CONFIDED)\n", con_fallback_blkread);
		fprintf(stdout, "fcollisions = %u\n", fcollisions);

		ACCESSTIME_PRINT("ctotalreq = %lu, ", ctotalreq);
		ACCESSTIME_PRINT("ctotalblk = %lu\n", ctotalblk);
		ACCESSTIME_PRINT("ctotalreadreq = %lu, ", ctotalreadreq);
		ACCESSTIME_PRINT("ctotalwritereq = %lu\n", ctotalwritereq);
		ACCESSTIME_PRINT("ctotalblkread = %lu, ", ctotalblkread);
		ACCESSTIME_PRINT("ctotalblkwrite = %lu (CONFIDED)\n", ctotalblkwrite);
		ACCESSTIME_PRINT("con_zeroblksread = %lu ", con_zeroblksread);
		ACCESSTIME_PRINT("con_fallback_blkread = %lu (CONFIDED)\n", con_fallback_blkread);
		ACCESSTIME_PRINT("fcollisions = %u\n", fcollisions);
	}
	else if (sreplayflag)
	{
		/* Stats for sreplay */
		fprintf(stdout, "stotalreq = %lu, ", stotalreq);
		fprintf(stdout, "stotalblk = %lu\n", stotalblk);
		fprintf(stdout, "stotalreadreq = %lu, ", stotalreadreq);
		fprintf(stdout, "stotalwritereq = %lu\n", stotalwritereq);
		fprintf(stdout, "stotalblkread = %lu, ", stotalblkread);
		fprintf(stdout, "stotalblkwrite = %lu\n", stotalblkwrite);

		ACCESSTIME_PRINT("stotalreq = %lu, ", stotalreq);
		ACCESSTIME_PRINT("stotalblk = %lu\n", stotalblk);
		ACCESSTIME_PRINT("stotalreadreq = %lu, ", stotalreadreq);
		ACCESSTIME_PRINT("stotalwritereq = %lu\n", stotalwritereq);
		ACCESSTIME_PRINT("stotalblkread = %lu, ", stotalblkread);
		ACCESSTIME_PRINT("stotalblkwrite = %lu\n", stotalblkwrite);
	}
	else if (ioreplayflag)
	{
		/* Stats for ioreplay */
		fprintf(stdout, "iototalreq = %lu, ", iototalreq);
		fprintf(stdout, "iototalblk = %lu\n", iototalblk);
		fprintf(stdout, "iototalreadreq = %lu, ", iototalreadreq);
		fprintf(stdout, "iototalwritereq = %lu\n", iototalwritereq);
		fprintf(stdout, "iototalblkread = %lu, ", iototalblkread);
		fprintf(stdout, "iototalblkwrite = %lu (IODEDUP)\n", iototalblkwrite);
		fprintf(stdout, "io_zeroblksread = %lu ", io_zeroblksread);
		fprintf(stdout, "io_fallback_blkread = %lu (IODEDUP)\n", io_fallback_blkread);
		fprintf(stdout, "iocollisions = %u\n", fcollisions);

		ACCESSTIME_PRINT("iototalreq = %lu, ", iototalreq);
		ACCESSTIME_PRINT("iototalblk = %lu\n", iototalblk);
		ACCESSTIME_PRINT("iototalreadreq = %lu, ", iototalreadreq);
		ACCESSTIME_PRINT("iototalwritereq = %lu\n", iototalwritereq);
		ACCESSTIME_PRINT("iototalblkread = %lu, ", iototalblkread);
		ACCESSTIME_PRINT("iototalblkwrite = %lu (IODEDUP)\n", iototalblkwrite);
		ACCESSTIME_PRINT("io_zeroblksread = %lu ", io_zeroblksread);
		ACCESSTIME_PRINT("io_fallback_blkread = %lu (IODEDUP)\n", io_fallback_blkread);
		ACCESSTIME_PRINT("iocollisions = %u\n", fcollisions);
	}
#endif


	if (write_enabled==0)
	{
		ACCESSTIME_PRINT("Note that write_enable was NOT set now.\n");
		fprintf(stdout, "Repeat with -E flag if writes are to be done.\n");
	}
	if (read_enabled==0)
	{
		ACCESSTIME_PRINT("Note that read_enable was NOT set now.\n");
		fprintf(stdout, "Repeat with -e flag if reads are to be done.\n");
	}

	if (freplayflag)
	{
		fprintf(stdout, "io-redirections: self-is-leader=%llu, "
				"self-is-not-leader=%llu\n", fmap_self_is_leader, 
				fmap_self_is_not_leader);
		ACCESSTIME_PRINT("io-redirections: self-is-leader=%llu, "
				"self-is-not-leader=%llu\n", fmap_self_is_leader, 
				fmap_self_is_not_leader);
	}
	else if (ioreplayflag)
	{
		fprintf(stdout, "io-redirections: self-is-leader=%llu, "
				"self-is-not-leader=%llu\n", cmap_self_is_leader, 
				cmap_self_is_not_leader);
		ACCESSTIME_PRINT("io-redirections: self-is-leader=%llu, "
				"self-is-not-leader=%llu\n", cmap_self_is_leader, 
				cmap_self_is_not_leader);
	}

	if (freplayflag || ioreplayflag || preplayflag)
	{
		fprintf(stdout, "read-responses: compulsory-misses=%llu, "
				"cache-hits=%llu, capacity-misses=%llu\n", compulsory_misses,
				bcache_hits_r + ccache_hits_r, capacity_misses);
		ACCESSTIME_PRINT("read-responses: compulsory-misses=%llu, "
				"cache-hits=%llu, capacity-misses=%llu\n", compulsory_misses,
				bcache_hits_r + ccache_hits_r, capacity_misses);
	}
	if (freplayflag)
	{
		fprintf(stdout, "metadata-hit-conversions: deduphits=%llu, selfhits=%llu,"
				" dedupmisses=%llu, selfmisses=%llu\n", fixed_dedup_hits, 
				fixed_self_hits, fixed_dedup_misses, fixed_self_misses);
		ACCESSTIME_PRINT("metadata-hit-conversions: deduphits=%llu, "
				"selfhits=%llu,"
				" dedupmisses=%llu, selfmisses=%llu\n", fixed_dedup_hits, 
				fixed_self_hits, fixed_dedup_misses, fixed_self_misses);
	}
	else if (ioreplayflag)
	{
		fprintf(stdout, "metadata-hit-conversions: deduphits=%llu, selfhits=%llu,"
				" dedupmisses=%llu, selfmisses=%llu\n", ccache_dedup_hits, 
				ccache_nondedup_hits, ccache_dedup_misses, 
				ccache_nondedup_misses);
		ACCESSTIME_PRINT("metadata-hit-conversions: deduphits=%llu, "
				"selfhits=%llu,"
				" dedupmisses=%llu, selfmisses=%llu\n", ccache_dedup_hits, 
				ccache_nondedup_hits, ccache_dedup_misses, 
				ccache_nondedup_misses);
	}

	fprintf(stdout, "#READ=%lu, #WRITE=%lu\n", gtotalreadreq, gtotalwritereq);
	ACCESSTIME_PRINT("#READ=%lu, #WRITE=%lu\n", gtotalreadreq, gtotalwritereq);
	fprintf(stdout, "RAM-size = %d (MB)\n", RAMsize_MB);
	ACCESSTIME_PRINT("RAM-size = %d (MB)\n", RAMsize_MB);
	if (ioreplayflag)
	{
		fprintf(stdout, "CCACHE-size = %d (MB)\n", CCACHEsize_MB);
		ACCESSTIME_PRINT("CCACHE-size = %d (MB)\n", CCACHEsize_MB);
	}

	if (preplayflag)
	{
        fprintf(stdout, "provided metadata: hits=%llu, misses=%llu, "
                        "mapmisscachehits=%llu, dirties=%llu, "
                        "mapdirtycachehits=%llu\n",
                        vmap_hits, vmap_misses, vmapmiss_cachehits,
                        vmap_dirties, vmapdirty_cachehits);
		fprintf(stdout,"pcollisions = %u, pcollisionstp = %u, "
				"pzerocollisions = %u, pcollisionsdiffclen=%u\n", 
			pcollisions, pcollisionstp, pzerocollisions, pcollisionsdiffclen);
        ACCESSTIME_PRINT("provided metadata: hits=%llu, misses=%llu, "
                        "mapmisscachehits=%llu, dirties=%llu, "
                        "mapdirtycachehits=%llu\n",
                        vmap_hits, vmap_misses, vmapmiss_cachehits,
                        vmap_dirties, vmapdirty_cachehits);
		ACCESSTIME_PRINT("pcollisions = %u, pcollisionstp = %u, "
				"pzerocollisions = %u, pcollisionsdiffclen=%u\n", 
			pcollisions, pcollisionstp, pzerocollisions, pcollisionsdiffclen);
	}
	else if (freplayflag)
	{
		fprintf(stdout, "confided metadata: hits=%llu, misses=%llu, "
						"mapmisscachehits=%llu, dirties=%llu, "
						"mapdirtycachehits=%llu\n", 
						fmap_hits, fmap_misses, fmapmiss_cachehits, 
						fmap_dirties, fmapdirty_cachehits);
		fprintf(stdout, "fcollisions=%u, fcollisionstp=%u, fzeros=%u\n", 
			fcollisions, fcollisionstp, fzeros);
		ACCESSTIME_PRINT("confided metadata: hits=%llu, misses=%llu, "
						"mapmisscachehits=%llu, dirties=%llu, "
						"mapdirtycachehits=%llu\n", 
						fmap_hits, fmap_misses, fmapmiss_cachehits, 
						fmap_dirties, fmapdirty_cachehits);
		ACCESSTIME_PRINT("fcollisions=%u, fcollisionstp=%u, fzeros=%u\n", 
			fcollisions, fcollisionstp, fzeros);
	}
	
	if (preplayflag || sreplayflag || ioreplayflag || freplayflag)
	{
		fprintf(stdout, "buffer cache: hits=%llu, misses=%llu, "
						"readhits=%llu, writehits=%llu\n", 
						bcache_hits, bcache_misses, bcache_hits_r, 
						bcache_hits_w);
		ACCESSTIME_PRINT("buffer cache: hits=%llu, misses=%llu, "
						"readhits=%llu, writehits=%llu\n", 
						bcache_hits, bcache_misses, bcache_hits_r, 
						bcache_hits_w);
		free_lru_cache();
	}
	if (ioreplayflag)
	{
		fprintf(stdout, "content metadata: hits=%llu, misses=%llu "
						"dirties=%llu\n", 
						cmap_hits, cmap_misses, cmap_dirties);
		fprintf(stdout, "content cache: hits=%llu, misses=%llu "
						"hits-for-ccache-misses\n", 
						ccache_hits, ccache_misses);
		fprintf(stdout, "content cache: dedup hits=%llu, "
							"nondedup hits=%llu\n", 
						ccache_dedup_hits, ccache_nondedup_hits);
		ACCESSTIME_PRINT("content metadata: hits=%llu, misses=%llu "
						"dirties=%llu\n", 
						cmap_hits, cmap_misses, cmap_dirties);
		ACCESSTIME_PRINT("content cache: hits=%llu, misses=%llu "
						"hits-for-ccache-misses\n", 
						ccache_hits, ccache_misses);
		ACCESSTIME_PRINT("content cache: dedup hits=%llu, "
							"nondedup hits=%llu\n", 
						ccache_dedup_hits, ccache_nondedup_hits);
//		contentcache_exit();
	}
	fprintf(stdout, "disk hits=%llu\n", disk_hits);
	fprintf(stdout, "disk hits read=%llu, writes=%llu\n", 
					disk_hits_r, disk_hits_w);
	ACCESSTIME_PRINT("disk hits=%llu\n", disk_hits);
	ACCESSTIME_PRINT("disk hits read=%llu, writes=%llu\n", 
					disk_hits_r, disk_hits_w);

	if (freplayflag || ioreplayflag || preplayflag)
	{
		assert(bcache_hits == bcache_hits_r + bcache_hits_w);
		assert(bcache_misses == bcache_misses_r + bcache_misses_w);
		assert(ccache_hits == ccache_hits_r + ccache_hits_w);
		assert(ccache_misses == ccache_misses_r + ccache_misses_w);
		if (preplayflag)
			assert(ptotalblkread == compulsory_misses + bcache_hits_r + ccache_hits_r + capacity_misses);
		else
			assert(gtotalreadreq == compulsory_misses + bcache_hits_r + ccache_hits_r + capacity_misses);
	}
    if (preplayflag)
    {
        assert(bcache_misses_r == compulsory_misses + capacity_misses);
        assert(compulsory_misses == vmap_misses - vmap_dirties - vmapmiss_cachehits + vmapdirty_cachehits);
        assert(vmap_hits + ptotalblkread - ptotalreadreq == capacity_misses - (vmap_dirties-vmapdirty_cachehits) + bcache_hits_r - vmapmiss_cachehits);
    }
	if (freplayflag)
	{
		assert(fmap_hits == fixed_dedup_hits + fixed_self_hits + fixed_dedup_misses + fixed_self_misses);
		assert(bcache_misses_r == compulsory_misses + capacity_misses);
		assert(compulsory_misses == fmap_misses - fmap_dirties - fmapmiss_cachehits + fmapdirty_cachehits);
		assert(capacity_misses == fixed_dedup_misses + fixed_self_misses + fmap_dirties - fmapdirty_cachehits);
		assert(fmap_hits == capacity_misses - (fmap_dirties-fmapdirty_cachehits) + bcache_hits_r - fmapmiss_cachehits);
		assert(fmap_self_is_leader == fixed_self_hits + fixed_self_misses);
		assert(fmap_self_is_not_leader == fixed_dedup_hits+fixed_dedup_misses);
	}
	else if(ioreplayflag)
	{
		assert(cmap_hits == ccache_dedup_hits + ccache_nondedup_hits + ccache_dedup_misses + ccache_nondedup_misses);
		assert(ccache_misses_r + cmap_dirties == capacity_misses);
		assert(compulsory_misses + cmap_dirties == cmap_misses);
		assert(capacity_misses == ccache_dedup_misses + ccache_nondedup_misses + cmap_dirties);
		assert(cmap_hits == capacity_misses - cmap_dirties + ccache_hits_r);
		assert(cmap_self_is_leader == ccache_nondedup_hits + ccache_nondedup_misses);
		assert(cmap_self_is_not_leader == ccache_dedup_hits + ccache_dedup_misses);
	}

	outputtimefn_exit();
	outputhashfn_exit();
#ifdef CONTENTDEDUP
	free_contentdeduptab();
#endif

	//if (disksimflag && (preplayflag || !collectformat))
	if (disksimflag)
	{
		simdiskfn_exit();
		if (runtimemap || sreplayflag)
		{
			free_blkidtab();
			if (preplayflag)
				exitChunking();
		}
	}

	return;
}

/**
 * main - 
 * @argc: Number of arguments
 * @argv: Array of arguments
 */
int main(int argc, char *argv[])
{
    int i;
	struct slist_head *p, *p2;

	/* Just a check to ensure that at least one of these flags 
	 * 1. SYNCIO
	 * 2. ASYNCIO
	 * has been defined in Makefile. In case these flags are no longer 
	 * required, please remove this check, and the next as well.
	 * Basically, the struct thr_info differs based on this flag.
	 */
#ifndef SYNCIO
#ifndef ASYNCIO
    fprintf(stdout, "Please ensure that either SYNCIO or ASYNCIO is defined" \
                    " in Makefile\n");
	return -1;
#endif
#endif

	/* Just a check to ensure that only one of these flags are defined.
	 * If these flags are no longer required, please remove this check as well.
	 * Basically, the struct thr_info differs based on this flag.
	 */
#if defined(SYNCIO) && defined(ASYNCIO)
    fprintf(stdout, "Please ensure only one of SYNCIO and ASYNCIO is" \
                " defined in Makefile, not both.\n");
	return -1;
#endif

#if defined(RECLAIM_DEBUG_SS) || defined (PDDREPLAY_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    pgsize = getpagesize();		/* Usually 4K page size */
    assert(pgsize > 0);

    setup_signal(SIGINT, set_signal_done);
    setup_signal(SIGTERM, set_signal_done);

	handle_args(argc, argv);		/* Command-line arg handler */

	/* Register the exit handler */
	atexit(sim_replay_exit);

	/* We have only a single input file, but still using the same construct
	 * of btreplay which allows multiple input files.
	 */
	if (warmupflag)
		add_input_file(ifull_name, wfull_name);
	else
		add_input_file(ifull_name, NULL); /* Note the input replay file name */

	pdd_replay_init();			/* Initializes relevant data-strucutres */
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "2. iterations=%d\n", tip->iterations);
#endif

	/* For each input file, initialize a thread to handle its replay */
	nfiles = slist_len(&input_files_replay);
	__slist_for_each(p, &input_files_replay) {
		if (warmupflag)
		{
			__slist_for_each(p2, &input_files_warmup) {	//len = 1 for now
				tip_init(slist_entry(p, struct thr_info, head), 
						slist_entry(p2, struct thr_info, head));
			}
		}
		else
			tip_init(slist_entry(p, struct thr_info, head), NULL);
	}

#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "3. iterations=%d\n", tip->iterations);
#endif

	/* Signaling ready for replay (useful if there are multiple threads) */
	wait_replays_ready();
#if defined(REPLAYDIRECT_DEBUG_SS)
	fprintf(stdout, "4. iterations=%d\n", tip->iterations);
#endif

	/* The same input file can be replayed multiple number of times */
    for (i = 0; i < def_iterations; i++) {
		rgenesis = gettime();	/* Noting the start-time of replay */
        start_iter();			/* Signals to start the replay iteration */
        if (verbose)
            fprintf(stdout, "I");
        wait_iters_done();		/* Wait for signal from set_iter_done
									so that it can get ready for next iter */
    }

	/* Waiting for replay thread to exit by set_replay_done() */
    wait_replays_done();

#ifdef ASYNCIO
	/* Async I/O requests to be "reclaimed" also after they are "submitted",
	 * so here waiting for the reclaiming thread to finish 
	 */
    wait_reclaims_done();
#endif

	if (V2PmapFile != NULL && runtimemap)
	{
		/* If we are here, we did not read up any V2P map, but
		 * need to write V2P map to writemapp FILE
		 */
	    writemapp = fopen(V2PmapFile, "w");
	    if (writemapp == NULL)
			fatal(NULL, ERR_SYSCALL,
				"Failed to open file %s for V2P map write\n", V2PmapFile);

        if (write_input_v2p_mapping())
             fatal(NULL, ERR_USERCALL, "error in write_input_v2p_mapping\n");
    }
	free_v2pmaps();		 /* Free V2P maps and voltab */

	if (freplayflag)
	{
		free_v2fmaps();		 /* Free V2F maps */	
		free_fixedmap();	/* Free F2PV maps and chunktab */
		if (currReusableFixedIDUList)
			freeUList(currReusableFixedIDUList);
		if (newReusableFixedIDUList)
			freeUList(newReusableFixedIDUList);
	}
	else if (preplayflag)
	{
		free_v2cmaps();		 /* Free V2C maps */	
		free_chunkmap();	/* Free C2PV maps and chunktab */
		if (currReusableChunkIDUList)
			freeUList(currReusableChunkIDUList);
		if (newReusableChunkIDUList)
			freeUList(newReusableChunkIDUList);
	}
	if (ioreplayflag)
	{
		contentcache_exit();
		free_p2dmaps();		 /* Free P2D  maps */	
		free_dedupmap();	/* Free D2PV maps and deduptab */
		if (currReusableDedupIDUList)
			freeUList(currReusableDedupIDUList);
		if (newReusableDedupIDUList)
			freeUList(newReusableDedupIDUList);
	}

	if (v2pfp)
		fclose(v2pfp);
	if (voltfp)
		fclose(voltfp);
	if (freplayflag)
	{
		if (v2ffp)
			fclose(v2ffp);
		if (f2pvfp)
			fclose(f2pvfp);
	}
	else if (preplayflag)
	{
		if (v2cfp)
			fclose(v2cfp);
		if (c2pvfp)
			fclose(c2pvfp);
	}
	/* Uncomment this if map dumping for IODEDUP is being done 
	else if (ioreplayflag)
	{
		if (p2dfp)
			fclose(p2dfp);
		if (d2pvfp)
			fclose(d2pvfp);
	}
	*/

	if (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_COLLECTFORMAT_VMBUNCHREPLAY ||
			DISKSIM_VANILLA_COLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY ||
			DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY)
	{
		free(idevnm);
	}
	else
	{
		vm_rem_input_file(iip);	
	}
	if (warmupflag)
	{
		vm_rem_input_file(wip);
	}

#ifdef PRO_STATS

	if (preplayflag)
	{
    	/* Following asserts should hold on above variables :-
	     * -- totalreadreq + totalwritereq == totalreq
	     * -- totalblkread + totalblkwrite == totalblk
	     * -- pro_blkread + fallback_blkread + chunking_blkread == totalblkread
	     * -- fallback_blkread <= origblkread
	     * -- origblkwrite == totalblkwrite (since we do not optimize writes)
	     */
		assert(ptotalreadreq + ptotalwritereq == ptotalreq);
		assert(ptotalblkread + ptotalblkwrite + pro_zeroblksread == ptotalblk);
		assert(pro_blkread+pro_fallback_blkread+chunking_blkread+pro_zeroblksread == ptotalblkread);
		assert(pro_fallback_blkread <= porigblkread);
		assert(porigblkwrite == ptotalblkwrite);
	}
	else if (freplayflag)
	{
	    /* Following asserts should hold on above variables :-
    	 * -- ctotalreadreq + ctotalwritereq == ctotalreq
	     * -- ctotalblkread + ctotalblkwrite == ctotalblk
	     */
		assert(ctotalreadreq + ctotalwritereq == ctotalreq);
		assert(ctotalblkread + ctotalblkwrite + con_zeroblksread == ctotalblk);
	}
	else if (sreplayflag)
	{
	    /* Following asserts should hold on above variables :-
    	 * -- stotalreadreq + stotalwritereq == stotalreq
	     * -- stotalblkread + stotalblkwrite == stotalblk
	     */
		assert(stotalreadreq + stotalwritereq == stotalreq);
		assert(stotalblkread + stotalblkwrite == stotalblk);
	}
	else if (ioreplayflag)
	{
		assert(iototalreadreq + iototalwritereq == iototalreq);
		assert(iototalblkread + iototalblkwrite + io_zeroblksread == iototalblk);
	}
#endif		

#ifndef TESTVMREPLAY	/* will be defined only in pdd_testvmreplay */	
	if (freplayflag || preplayflag || ioreplayflag)
		sync_disk_exit();
#endif

    if (verbose)
        fprintf(stdout, "\n");

	/* Remove the input trace file */
	rem_input_files();	

	/* Exit gracefully */
	exit(0);
}

