
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <error.h>
#include <assert.h>
#include "trace-struct.h"
#include "vmbunching_structs.h"
#include "debugg.h"
#include "utils.h"
#include "vmbunching-generic.h"

extern struct ifile_info *iip;
extern char *odir;            // Output directory base
extern char *obase;          // Output file base
extern int verbose;             // Boolean: output stats

/**
 * stream_flush - Flush current bunch of IOs out to the output stream
 * @stream: Per-output file stream information
 */
static void stream_flush(struct io_stream *stream)
{
    struct vm_bunch *cur = stream->cur;

    if (cur) {
        if (cur->hdr.npkts) {
#ifdef VMBUNCH_DEBUG_SS
			fprintf(stdout, "npkts = %llu\n", cur->hdr.npkts);				
#endif
            assert(cur->hdr.npkts <= BT_MAX_PKTS);
            bunch_output_hdr(stream);
            bunch_output_pkts(stream);

            stream->bunches++;
            stream->pkts += cur->hdr.npkts;
        }
        free(cur);
    }
}


/**
 * stream_add_io - Add an IO trace to the current stream
 * @stream: Output stream information
 * @spec: IO trace specification
 */
void stream_add_io(struct io_stream *stream, struct vmreq_spec *spec)
{
#ifdef VMBUNCH_DEBUG_SS			
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    if (stream->cur == NULL)
	{
#ifdef VMBUNCH_DEBUG_SS			
		fprintf(stdout, "stream->cur == NULL\n");
#endif
        vm_bunch_create(stream, spec->time);
	}
    else if (bunch_done(stream, spec)) {
#ifdef VMBUNCH_DEBUG_SS			
		fprintf(stdout, "bunch_done == 1\n");
#endif
        stream_flush(stream);
        vm_bunch_create(stream, spec->time);
    }

    vm_bunch_add(stream, spec);
}

/**
 * stream_open - Open output stream for specified input stream
 * @iip: Per-input file information
 */
struct io_stream* stream_open(struct ifile_info *iip)
{
    char ofile_name[MAXPATHLEN];
    struct io_stream *stream = malloc(sizeof(*stream));
	if (stream == NULL)
		fprintf(stderr, "Unable to malloc stream\n");
    struct vm_file_hdr vm_file_hdr = {
        .genesis = 0,
        .nbunches = 0,
        .total_pkts = 0
    };

	assert(stream != NULL);
    memset(stream, 0, sizeof(*stream));

    sprintf(ofile_name, "%s/%s", odir, obase);
    stream->ofp = fopen(ofile_name, "w");
    if (!stream->ofp) {
        fatal(ofile_name, ERR_SYSCALL, "Open failed\n");
        /*NOTREACHED*/
    }

    stream->iip = iip;
    stream->cur = NULL;
    stream->bunches = stream->pkts = 0;
    stream->last_time = 0;
    stream->file_name = strdup(ofile_name);

    write_file_hdr(stream, &vm_file_hdr);

    if (verbose) {
        char vfile_name[MAXPATHLEN];

        sprintf(vfile_name, "%s/%s.verbose", odir, obase);
        stream->vfp = fopen(vfile_name, "w");
        if (!stream->vfp) {
            fatal(vfile_name, ERR_SYSCALL, "Open failed\n");
            /*NOTREACHED*/
        }

        stream->vfn = strdup(vfile_name);
    }

    return stream;
}

/**
 * stream_close - Release resources associated with an output stream
 * @stream: Stream to release
 */
void stream_close(struct io_stream *stream)
{
    struct vm_file_hdr vm_file_hdr = {
        .genesis = stream->iip->genesis,
        .nbunches = stream->bunches,
        .total_pkts = stream->pkts
    };

    stream_flush(stream);
    write_file_hdr(stream, &vm_file_hdr);
    fclose(stream->ofp);

    if (verbose && stream->bunches) {
        fprintf(stdout,
            "%s: %llu pkts (tot), %llu pkts (replay), "			
                    "%llu bunches, %.1lf pkts/bunch\n",
            stream->iip->file_name,			
            (unsigned long long)stream->iip->tpkts,
            (unsigned long long)stream->pkts,
            (unsigned long long)stream->bunches,
            (double)(stream->pkts) / (double)(stream->bunches));

        fclose(stream->vfp);
        free(stream->vfn);
    }

    free(stream->file_name);
    free(stream);
}

#if 0 
void copycontent(__u8 **d, __u8 *s, size_t nbytes)
{
	*d = malloc(nbytes);	//free in bunch_output_pkts()
	if (d == NULL)
		fprintf(stderr, "malloc for d failed\n");
	assert(d != NULL);
	memcpy(*d, s, nbytes);
}
#endif

