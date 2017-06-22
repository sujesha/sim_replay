
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include "trace-struct.h"
#include "vmbunching_structs.h"
#include "debugg.h"
#include "utils.h"
#include "pdd_config.h"
#include "md5.h"
#ifdef SIM_REPLAY
	#include "sha.h"
#endif
#include "vmfile-handling.h"

#ifdef VMBUNCHDIRECT_TEST
	#include "vmbunchdirect_test_stub.h"	//stub of next_io
#endif

#ifdef BOOTVMBUNCH 
	#include "bootvmbunch_stub.h"
#endif

extern int disksimflag;
extern int collectformat;

extern char pddversion[];
extern int pddver_mjr;
extern int pddver_mnr;
extern int pddver_sub;

__u64 max_bunch_tm = (10 * 1000 * 1000); // 10 milliseconds
__u64 max_pkts_per_bunch = 8;        // Default # of pkts per bunch
int verbose;             // Boolean: output stats

struct io_stream* stream_open(struct ifile_info *iip);
void stream_add_io(struct io_stream *stream, struct vmreq_spec *spec);
void stream_close(struct io_stream *stream);
inline __u64 mk_pddversion(int mjr, int mnr, int sub);

/**
 * write_file_hdr - Seek to and write btrecord file header
 * @stream: Output file information
 * @hdr: Header to write
 */
void write_file_hdr(struct io_stream *stream, struct vm_file_hdr *hdr)
{
    hdr->version = mk_pddversion(pddver_mjr, pddver_mnr, pddver_sub);

#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    if (verbose) {
        fprintf(stdout, "\t%s: %llx %llx %llx %llx\n",
            stream->file_name,
            (long long unsigned)hdr->version,
            (long long unsigned)hdr->genesis,
            (long long unsigned)hdr->nbunches,
            (long long unsigned)hdr->total_pkts);
    }

    fseek(stream->ofp, 0, SEEK_SET);
    if (fwrite(hdr, sizeof(*hdr), 1, stream->ofp) != 1) {
        fatal(stream->file_name, ERR_SYSCALL, "Hdr write failed\n");
        /*NOTREACHED*/
    }
}

/**
 * vm_bunch_create - Allocate & initialize an vm_bunch
 * @io_stream: IO stream being added to
 * @pre_stall: Amount of time that this bunch should be delayed by
 * @start_time: Records current start 
 */
void vm_bunch_create(struct io_stream *stream, __u64 start_time)
{
    struct vm_bunch *cur = malloc(sizeof(*cur));
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	if (cur == NULL)
		fprintf(stderr, "Unable to malloc cur\n");

	assert(cur != NULL);
    memset(cur, 0, sizeof(*cur));

    cur->hdr.npkts = 0;
    cur->hdr.time_stamp = stream->start_time = start_time;

    stream->cur = cur;
}

/**
 * bunch_done - Returns true if current bunch is either full, or next IO is late
 * @stream: Output stream information
 * @spec: IO trace specification
 */
int bunch_done(struct io_stream *stream, struct vmreq_spec *spec)
{
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    if (stream->cur->hdr.npkts >= max_pkts_per_bunch)
        return 1;

    if ((spec->time - stream->start_time) > max_bunch_tm)
        return 1;

    return 0;
}

/**
 * bunch_output_hdr - Output bunch header
 */
void bunch_output_hdr(struct io_stream *stream)
{
    struct vm_bunch_hdr *hdrp = &stream->cur->hdr;
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    assert(0 < hdrp->npkts && hdrp->npkts <= BT_MAX_PKTS);
    if (fwrite(hdrp, sizeof(struct vm_bunch_hdr), 1, stream->ofp) != 1) {
        fatal(stream->file_name, ERR_SYSCALL, "fwrite(hdr) failed\n");
        /*NOTREACHED*/
    }

    if (verbose) {
        __u64 off = hdrp->time_stamp - stream->iip->genesis;

        assert(stream->vfp);
        fprintf(stream->vfp, "------------------\n");
        fprintf(stream->vfp, "%4llu.%09llu %3llu\n",
            (unsigned long long)off / (1000 * 1000 * 1000),
            (unsigned long long)off % (1000 * 1000 * 1000),
            (unsigned long long)hdrp->npkts);
        fprintf(stream->vfp, "------------------\n");
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
        fprintf(stdout, "------------------\n");
        fprintf(stdout, "%4llu.%09llu %3llu\n",
            (unsigned long long)off / (1000 * 1000 * 1000),
            (unsigned long long)off % (1000 * 1000 * 1000),
            (unsigned long long)hdrp->npkts);
        fprintf(stdout, "------------------\n");
#endif
    }
}

/**
 * bunch_output_pkt - Output IO packets
 */
void bunch_output_pkts(struct io_stream *stream)
{
    struct vm_pkt *p = stream->cur->pkts;
	struct vm_pkt_frame iof;
    size_t npkts = stream->cur->hdr.npkts;
	__u8 *c;
	size_t i = 0;

#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    assert(0 < npkts && npkts <= BT_MAX_PKTS);
#if 0	
    if (fwrite(p, sizeof(struct vm_pkt), npkts, stream->ofp) != npkts) {
        fatal(stream->file_name, ERR_SYSCALL, "fwrite(pkts) failed\n");
        /*NOTREACHED*/
    }
#endif

#if 0	
    if (verbose) {
   	    size_t i;

    	p = stream->cur->pkts;
       	assert(stream->vfp);
        for (i = 0; i < npkts; i++, p++)
		{
       	    fprintf(stream->vfp, "\t%1d %s\t%10llu\t%10u\n",
           	    p->rw, p->vmname,
               	(unsigned long long)p->block,
                (size_t)p->nbytes / BLKSIZE);
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
            fprintf(stdout, "\t%1d %s\t%10llu\t%10u\n",
                p->rw, p->vmname,
                (unsigned long long)p->block,
                (size_t)p->nbytes / BLKSIZE);
#endif
		}
    }
#endif

	i = 0;
   	p = stream->cur->pkts;
	while(i < npkts)
	{
		assert(p->rw == 0 || p->rw == 1);
		memset(iof.vmname, 0, HOSTNAME_LEN);
		strcpy(iof.vmname, p->vmname);
		iof.block = p->block;
		iof.nbytes = p->nbytes;
		iof.rw = p->rw;
		assert(iof.rw == 0 || iof.rw == 1);

		if (fwrite(&iof, sizeof(struct vm_pkt_frame), 1, stream->ofp) != 1)
			fatal(stream->file_name, ERR_SYSCALL, "fwrite(1) failed\n");
		c = p->content;
		if (disksimflag || collectformat)	/* both read and writes */
		{
			p->content[MD5HASHLEN_STR-1] = '\n';
			assert(p->content != NULL);
			if (fwrite(p->content,sizeof(char), HASHLEN_STR,
								stream->ofp) != HASHLEN_STR)
				fatal(stream->file_name, ERR_SYSCALL, 
						"2 -- fwrite(p->nbytes) fail\n");
			free(c);	//to prevent memory leak
		}
		else if (!p->rw)		/* write */
		{
			p->content[p->nbytes] = '\n';
			assert(p->content != NULL && p->nbytes != 0);
			if (fwrite(p->content,sizeof(char),p->nbytes+1,
								stream->ofp) != p->nbytes)
				fatal(stream->file_name, ERR_SYSCALL, 
						"1 -- fwrite(p->nbytes) fail\n");
			free(c);	//to prevent memory leak
		}
		p++;
		i++;
	}

}

/**
 * vm_bunch_add - Add an IO to the current bunch of IOs
 * @stream: Per-output file stream information
 * @spec: IO trace specification
 *
 * Returns update bunch information
 */
void vm_bunch_add(struct io_stream *stream, struct vmreq_spec *spec)
{
    struct vm_bunch *cur; //= stream->cur;
//    struct vm_pkt iop;

#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    assert(stream != NULL);
    assert(spec != NULL);

	cur = stream->cur;
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)	
    fprintf(stdout, "In %s: %s%10llu+%10llu (%d) @ %10llx\n", __FUNCTION__,
			spec->vmname,
            (long long unsigned)spec->block,
            (long long unsigned)spec->bytes / BLKSIZE,
            spec->rw, (long long unsigned)spec->time);
//		fprintf(stdout, "And content = %s\n", spec->content);
#endif	

#if 0	
	assert(strcmp(spec->vmname, ""));
	memset(iop.vmname, 0, HOSTNAME_LEN);
	strcpy(iop.vmname, spec->vmname);
	iop.block = spec->block;
    iop.nbytes = spec->bytes;
    iop.rw = spec->rw;
	assert(iop.rw == 0 || iop.rw == 1);
	iop.content = NULL;

	if (spec->rw == 0)	/* write request */
	{
		assert(spec->content != NULL);
		copycontent(&iop.content, spec->content, spec->bytes);
		free(spec->content);
	}
	else
		iop.content = NULL;
#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)	
        fprintf(stdout, "iop in %s: %s%10llu+%10llu (%d) @spec %10llu\n", 
						__FUNCTION__, iop.vmname,
            (long long unsigned)iop.block,
            (long long unsigned)iop.nbytes / BLKSIZE,
            iop.rw, (long long unsigned)spec->time);
	//	fprintf(stdout, "And content = %s\n", iop.content);
#endif		
#endif	
    assert(cur != NULL);
    assert(cur->hdr.npkts < BT_MAX_PKTS);
    assert(stream->last_time == 0 || stream->last_time <= spec->time);

#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
	fprintf(stdout, "npkts = %llu\n", cur->hdr.npkts);				
#endif

    ///////////////////cur->pkts[cur->hdr.npkts] = iop;  // Struct copy
	assert(strcmp(spec->vmname, ""));
	strcpy(cur->pkts[cur->hdr.npkts].vmname, spec->vmname);
	cur->pkts[cur->hdr.npkts].block = spec->block;
	cur->pkts[cur->hdr.npkts].nbytes = spec->bytes;
	cur->pkts[cur->hdr.npkts].rw = spec->rw;
	if (spec->content != NULL)
	{
		if (disksimflag || collectformat)
		{
			cur->pkts[cur->hdr.npkts].content = malloc(MD5HASHLEN_STR);
			memcpy(cur->pkts[cur->hdr.npkts].content, spec->content, MD5HASHLEN_STR-1);
		}
		else
		{
			cur->pkts[cur->hdr.npkts].content = malloc(spec->bytes);
			memcpy(cur->pkts[cur->hdr.npkts].content, spec->content, spec->bytes);
		}
		free(spec->content);	//to prevent memory leak
	}
	else
		cur->pkts[cur->hdr.npkts].content = NULL;

	cur->hdr.npkts++;

#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)	
        fprintf(stdout, "cur in %s: %s%10llu+%10llu (%d) @spec %10llu\n", 
						__FUNCTION__, cur->pkts[cur->hdr.npkts-1].vmname,
            (long long unsigned)cur->pkts[cur->hdr.npkts-1].block,
            (long long unsigned)cur->pkts[cur->hdr.npkts-1].nbytes / BLKSIZE,
            cur->pkts[cur->hdr.npkts-1].rw, (long long unsigned)spec->time);
		if (!cur->pkts[cur->hdr.npkts-1].rw)
		{
			fprintf(stdout, "And content = ");
			puts((char*)cur->pkts[cur->hdr.npkts-1].content);
		}
#endif		
    stream->last_time = spec->time;
}

/**
 * process - Process one input file to an output file
 * @iip: Per-input file information
 */
void process(struct ifile_info *iip)
{
    struct vmreq_spec spec;
    struct io_stream *stream;
	unsigned long long total = 0;

#if defined(BOOTVMBUNCH_DEBUG_SS) || defined(BOOTVMBUNCH_DEBUG_SS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    stream = (struct io_stream*) stream_open(iip);
	spec.content = NULL;
    while (!next_io(&spec, iip))
	{
        stream_add_io(stream, &spec);
		total++;
		if ((total & 0x1FFF) == 0)
		{
			fprintf(stdout, ".");	//to show progress after 131072 entries
			fflush(stdout);
		}
	}
    stream_close(stream);

    vm_rem_input_file(iip);
}

