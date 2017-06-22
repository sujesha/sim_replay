#ifndef _CHUNKING_H_
#define _CHUNKING_H_

#include <asm/types.h>
#include "c2pv-map.h"
#include "rabin.h"


/* During write-chunking, each block is of one of these conditions,
 * and needs to be handled accordingly.
 */
enum {
    NOT_LASTBLK = 1,			/* not last iter */
    NOPOSTCHUNK_LASTBLK = 2,		/* last iter but no postchunk, can
									only happen if nrxt-vblk-after-last
									is a zero block */
    POSTCHUNK_PARTIAL_LASTBLK = 4,	/* last iter and partial postchunk */
    POSTCHUNK_FULL_LASTBLK = 8,		/* last iter and full postchunk */
	PRECHUNK_PARTIAL_FIRSTBLK = 16,	/* first iter, partial blk */
	SCAN_FIRSTBLK = 32,
	IO_FIRSTBLK = 64,
	CHUNK_BY_ZEROBLK = 128,	/* chunk by itself because next blk is zero */
	FIRSTBLK = (PRECHUNK_PARTIAL_FIRSTBLK | SCAN_FIRSTBLK | IO_FIRSTBLK),
	JUST_UPDATE_NEXT_CHUNKOFFSETS = 256,
	ONLYBLK = 512,
};

/* These describe the relation between chunk and block boundaries 
 * for use in processBlock() and updateBlock()
 */
enum {
	NOCOINCIDE = 130,
	COINCIDE,
	FORCECOINCIDE,
	NOCOINCIDEFIRST,
	NOCOINCIDESECOND,
};

/* These describe the relation between chunk and block boundaries
 * for use in fixPrevBlock()
 */
enum {
	WASREADY = 140,
	WASNOTREADY,
	BEFOREBOUND,  /* calling fixPrevBlock() when first chunk encountered
					 for the block */
	AFTERBOUND,	  /* calling fixPrevBlock() after every block boundary */
};

/* To be used in the call to get_idx_into_vblklist() */
enum {
	PRESENT = 150,
	NOT_PRESENT,
};

#ifndef NONSPANNING_PROVIDE
int perfWriteChunking(unsigned char *buf, __u16 len, __u16 volID, __u32 blockID,
    struct chunk_t **prechunk, struct chunk_t **postchunk,
    struct chunkmap_t **seqnextp);
int createInitialChunkBuf(struct chunk_t **chunk, unsigned char *buf, __u16 len,
                        struct chunk_t **leftover, __u16 *bytes_left);
#endif

int resumeChunking(unsigned char *buf, __u16 len, __u16 volID,
        __u32 blockID, int initflag, int lastblk_flag,
        struct chunkmap_t **seqnextp, int rw_flag);
int resumeDynChunking(unsigned char *buf, __u16 len, __u16 volID, 
		__u32 blockID, int lastblk_flag, int rw_flag);
chunk_size_t inc_chunkoffset(chunk_size_t val);
chunk_size_t dec_chunkoffset(chunk_size_t val);
int initialize_globals_for_writechunking(int pre_len, 
		u64int prechunk_blockID, u64int blockID);
int updateDedupfetch(__u16 volID, __u32 blockID);


#endif /* _CHUNKING_H_ */
