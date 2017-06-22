/* Used in 2 phases by PROVIDED module 
 * 1. Scans disk to build mapping tables?
 * 2. Take action for read/write requests
 */

#include <asm/types.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
//#include "serveio.h"
#include "debug.h"
#include "pdd_config.h"
#include "serveio-utils.h"
#include "v2p-map.h"
#include "unused.h"

int disksimflag = 0;	/* No actual disk read/write, i.e. read req have content */
int syncfd = -1;
__u64 disk_hits = 0;
__u64 disk_hits_r = 0;
__u64 disk_hits_w = 0;

extern int write_enabled;       // Boolean: Enable writing
extern int read_enabled;       // Boolean: Enable reading

#if 0
//TODO: This fetch can be transparently from cache or disk, we are agnostic.
void do_async_read(struct pread_spec *output, char **fetchbuf, int nblks)
{
	int i;
	io_context_t ctx;

	while (i=0; i<nblks; i++)
	{

		//TODO: create pio_spec for (output+i) pread_spec
		//TODO: issue async i/o
	}
	
}
#endif

/* Open the device for hard-disk recreate, or PROVIDED scanning or,
 * for PROVIDED extra reads or for CONFIDED or IODEDUP
 *
 * read-only if write_enabled=0
 * write-only if read_enabled=0
 * read/write if write_enabled=1 and read_enabled=1
 */
int open_sync_device(char* path)
{
    int ofd;
#if defined(TESTVMREPLAY_DEBUG) || defined(RECREATE_DEBUG_SS) || defined(DIRECTRECREATE_DEBUG_SS)
    fprintf(stdout, "device = %s\n", path);
#endif

    if (write_enabled && read_enabled)
        ofd = open(path, O_RDWR);
    else if (read_enabled)
        ofd = open(path, O_RDONLY);
    else if (write_enabled)
        ofd = open(path, O_WRONLY);
	else 
		ofd = -1;

    if (ofd < 0)
    {
        RET_ERR("Failed device open\n");
    }
    return ofd;
}

void close_sync_device(int dp)
{
    close(dp);
}

/* return 0 if success */
int get_start_end_blk(__u16 volID, __u32 *start, __u32 *end)
{
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	getFirstvBlk(volID, start);
	if (getLastvBlk(volID, end))
		RET_ERR("volume ID %u has no info\n", volID);

	if (*start >= *end)
		RET_ERR("start blk %u should be lower than end blk %u\n", 
						*start, *end);
    return 0;

}

/* Used during write chunking as well as scanning by PROVIDED */
int _do_read(struct preq_spec *preql)
{
	int ret = 0;
	//savemem unsigned char buf[BLKSIZE];
	unsigned char *buf = malloc(BLKSIZE);

#ifdef DEBUG_SS
	assert(preql != NULL && preql->content == NULL);
	assert(preql->bytes == BLKSIZE);
#endif
	assert(syncfd != -1);
	preql->content = malloc(preql->bytes);
	if (preql->content == NULL)
		RET_ERR("malloc failed for preql->content\n");

	lseek(syncfd, (__u64)preql->ioblk * BLKSIZE, SEEK_SET);
	ret = read(syncfd, buf, BLKSIZE);
	if (ret < 0)
		RET_ERR("read error in _do_read\n");
	if (ret < (int)BLKSIZE)
		RET_ERR("fewer bytes read (%d) than requested (%d)\n", 
						ret, BLKSIZE);

	memcpy(preql->content, buf+preql->start, preql->bytes);

	free(buf);	//savemem
	return 0;
}

//FIXME: define this if needed
void _do_write(struct preq_spec *preql)
{
	UNUSED(preql);
}

int _do_bread(int dp, __u32 startblk, __u16 bytes, unsigned char *buf)
{
    int ret;

#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	lseek(dp, (__u64)startblk*BLKSIZE, SEEK_SET);        //may not be needed
//    UNUSED(start);

    ret = read(dp, buf, bytes);
    if (ret < 0)
        RET_ERR("read error in _do_bread\n");
    if (ret < (int)bytes)
        RET_ERR("fewer bytes read than requested in _do_bread\n");

#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s, buf = %s\n", __FUNCTION__, buf);
#endif
    return 0;
}

int _do_bwrite(int dp, __u32 start, __u16 bytes, unsigned char *buf)
{
    int ret;

    lseek(dp, (__u64)start*BLKSIZE, SEEK_SET);       // may not be needed
//    UNUSED(start);

#ifdef DEBUG_SS
    assert(buf != NULL && bytes > 0);
#endif

    ret = write(dp, buf, bytes);
    if (ret < 0)
    {
        perror("write");
        RET_ERR("write error in _do_bwrite\n");
    }
    if (ret < (int)bytes)
        RET_ERR("fewer bytes written than requested in _do_bwrite\n");

    return 0;
}

