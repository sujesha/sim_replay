
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <error.h>
#include <assert.h>
#include "trace-struct.h"
#include "vmbunching_structs.h"
#include "debugg.h"
#include "parse-generic.h"
#include "utils.h"
#include "pdd_config.h"
#include "vmbunching-generic.h"

int sectorflag = 0;	//if 0 indicates, blk numbers are present in input trace

//extern struct ifile_info *iip;
//extern char ifull_name[MAXPATHLEN];
extern char *idir;            // Input directory base
extern int verbose;             // Boolean: output stats

/**
 * __add_input_file - Allocate and initialize per-input file structure
 * @file_name: Fully qualifed input file name
 */
void _vm_add_input_file(char *file_name, struct ifile_info **iipp)
{
    fprintf(stdout, "In %s\n", __FUNCTION__);
    (*iipp) = malloc(sizeof(struct ifile_info));
	if ((*iipp) == NULL)
		fprintf(stderr, "Unable to malloc (*iipp)\n");

	assert((*iipp) != NULL);
    (*iipp)->tpkts = 0;
    (*iipp)->genesis = 0;
    (*iipp)->file_name = strdup(file_name);
    (*iipp)->ifd = open(file_name, O_RDONLY);
#if defined(VMBUNCHDIRECT_TEST) || defined(BOOTVMBUNCH)
	(*iipp)->ifp = fdopen(*iip->ifd, "r");
#endif
    if ((*iipp)->ifd < 0) {
        fatal(file_name, ERR_ARGS, "Unable to open\n");
        /*NOTREACHED*/
    }
#ifdef VMBUNCH_DEBUG_SS
	fprintf(stdout, "Opened input file %s\n", file_name);
#endif
}


/**
 * add_input_file - Set up the input file name
 */
void vm_add_input_file(char *ifile, char *ifull_name, struct ifile_info **iipp)
{
    sprintf(ifull_name, "%s/%s", idir, ifile);
    //if (access(ifullname, R_OK) != 0)
    //	return;

	if (access(ifull_name, R_OK) != 0) {
		fatal(ifull_name, ERR_ARGS,
			  "Invalid input file specified\n");
		/*NOTREACHED*/
    }
    _vm_add_input_file(ifull_name, iipp);

}

/**
 * vm_rem_input_file - Release resources associated with an input file
 * @iip: Per-input file information
 */
void vm_rem_input_file(struct ifile_info *iip)
{
    close(iip->ifd);
    free(iip->file_name);
    free(iip);
}

int verify_replayready(struct record_info *r)
{
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    if (strcmp(r->event, "OFR") && 
		strcmp(r->event, "CFR") && 
		strcmp(r->event, "CFNW") && 
		strcmp(r->event, "OFNW") && 
		strcmp(r->event, "OFDW"))
	{
		/* Not replay-ready, if is none of above events */
		return 0;
	}
	
	if (!strcmp(r->event, "OFDW") && !r->contentflag)
	{
		/* Not replay-ready if pre-processing not done yet */
		fprintf(stdout, "Do pre-processing for OFDW events so that"
						" content is available\n");
		return 0;
	}

	/* Yes, replay-ready */
	return 1;
}


/**         
 * next_io - Retrieve next I/O trace from input stream
 * @iip: Per-input file information
 * @spec: IO specifier for trace
 *          
 * Returns 0 on end of file, 1 if valid data returned.
 */ 
int next_io(struct vmreq_spec *spec, struct ifile_info *iipp)
{       
    int ret = 0;
	struct record_info *r = malloc(sizeof(struct record_info));
	if (r == NULL)
		fprintf(stderr, "Unable to malloc for r\n");
	memset(r, 0, sizeof(struct record_info));

	assert(r != NULL);
#ifdef VMBUNCH_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	ret = next_record(iipp->ifd, &r);
	if (ret == FILE_EOF)
	{
		fprintf(stdout, "reached end-of-file\n");
		free(r);	//was allocated in above malloc()
		return ret;
	}	
	else if (ret)
    {
	    fprintf(stderr, "next_record error\n");
		free(r);	//was allocated in above malloc()
        return ret;
	}

	/* Record retrieved above already has endianness handled
	 * in pddparse module. If not so, handle it here.
	 * Below check is for :-
	 * 1. Replay can only happen for I/O requests - OFR, OFNW, OFDW, CFR
	 * 2. For OFDW, ensure that contentflag == 1 i.e. content is present, or
	 * 		=> preprocessing is done.
	 * 3. CFR & CFNW records for c=>pdd converted traces with contentflag=0
	 */
	if (!verify_replayready(r))
	{
		fprintf(stderr, "Event %s is not ready for replay\n", r->event);
		return -1;
	}

    spec->time = r->ptime;
    strcpy(spec->vmname, r->hostname);
	/* Since piotrace reports sector_t whereas we want to deal with 
	 * blockID, hence divide by 8 here to get spec->blockID
	 */
	if (sectorflag)
    	spec->block = r->blockID >> 3;
	else
		spec->block = r->blockID;
    spec->bytes = r->nbytes;
	//fprintf(stdout, "spec->bytes=%u\n", spec->bytes);

#ifdef VMBUNCH_DEBUG_SS
	fprintf(stdout, "Next_record done for vmname = %s\n", spec->vmname);
#endif
    iipp->tpkts++;

#if 0
    /* If event is none of I/O events, then read next event */
    if (strcmp(event, "OFR") && strcmp(event, "OFNW") && strcmp(event, "OFDW"))
	{
		fprintf(stdout, "found event %s, so read again\n", event);
        goto again;
	}
#endif

	if (!strcmp(r->event, "OFR"))
	{
#ifdef VMBUNCH_DEBUG_SS
		fprintf(stdout, "found read request\n");
#endif
		spec->rw = 1;		/* read */
		spec->content = NULL;
	}
	else if (!strcmp(r->event, "CFR"))
	{
		spec->rw = 1;		/* read */
		copycontent(&spec->content, (__u8 *)r->dataorkey, MD5HASHLEN_STR-1);
	}
	else if (!strcmp(r->event, "CFNW"))
	{
		spec->rw = 0;		/* write */
		copycontent(&spec->content, (__u8 *)r->dataorkey, MD5HASHLEN_STR-1);
	}
	else
	{
#ifdef VMBUNCH_DEBUG_SS
		fprintf(stdout, "found write request\n");
#endif
		spec->rw = 0;		/* write */
		assert(r->dataorkey != NULL);
		assert(r->nbytes != 0);
//		r->dataorkey[r->nbytes] = '\0';
		copycontent(&spec->content, (__u8 *)r->dataorkey, r->nbytes);
	}

	/* r has served its purpose, so free it here */
	rfree(r);

#if 0
    if (verbose > 1)
        fprintf(stdout, "%s%10llu+%10llu (%d) @ %10llx\n",
						spec->vmname,
            (long long unsigned)spec->block,
            (long long unsigned)spec->bytes / 512LLU,
            spec->rw, (long long unsigned)spec->time);
#endif

    if (iipp->genesis == 0) {            /*S: Might happen for first event */
        iipp->genesis = spec->time;

#if 0
        if (verbose > 1)
            //fprintf(stderr, "\tSetting new genesis: %llx\n",
            fprintf(stdout, "\tSetting new genesis: %llu\n",
                (long long unsigned)iipp->genesis);
#endif
    }
    else if (iipp->genesis > spec->time)
        fatal(NULL, ERR_SYSCALL,
            "Time inversion? %llu ... %llu\n",
            (long long unsigned )iipp->genesis,
            (long long unsigned )spec->time);

    return 0;
}

