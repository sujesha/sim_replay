#ifndef _PDD_CONFIG_H_
#define _PDD_CONFIG_H_


/* Defining default compile-time configuration (over-ridden by Makefile) */
#define NUMBITS_SECTSIZE (9)
#define SECT_LEN (1<<NUMBITS_SECTSIZE)
#define NUMBITS_4K	(12)
#define K4_SIZE (1 << NUMBITS_4K)
#define SECT_TO_4K_SHIFTNUM (NUMBITS_4K - NUMBITS_SECTSIZE)

#ifndef BLKSIZE
	#define NUMBITS_BLKSIZE (NUMBITS_4K)
	#define BLKSIZE (1 << NUMBITS_BLKSIZE)
#endif

#ifndef MINCHUNKSIZE
	#define NUMBITS_MINCHUNKSIZE NUMBITS_SECTSIZE
	#define MINCHUNKSIZE (1 << NUMBITS_MINCHUNKSIZE)
#endif

#ifndef MAXCHUNKSIZE
	#define NUMBITS_MAXCHUNKSIZE (16)
	#define MAXCHUNKSIZE (1U << NUMBITS_MAXCHUNKSIZE)
	#define MAXCHUNKSIZE_IN_BLKS (1U<< (NUMBITS_MAXCHUNKSIZE - NUMBITS_BLKSIZE))
#endif

#ifndef HOSTNAME
	#define HOSTNAME "PROVM"
#endif

#define BLKTAB_SIZE (1<<20)
#define CHUNKTAB_SIZE (1<<20)

/* 
 * Defining modules according to configuration 
 */
#define xstringify(X) stringify(X)
#define stringify(X) #X

/* Hashing-related module configuration */
#ifdef MD5_HASH
	#define HASHLEN MD5_SIZE
	#define getHash(args...) md5(args)
#endif
#ifdef SHA_HASH
	#define HASHLEN SHA_SIZE
	#define getHash(args...) sha(args)
#endif
#ifndef HASHLEN
	#define HASHLEN MD5_SIZE
	#define getHash(args...) md5(args)
#endif
#define HASHLEN_STR (HASHLEN*2 + 1)

/*
 * pcollect parameters 
 */
#define MD5HASHLEN 16
#define MD5HASHLEN_STR 33

/* Use hashMagic in addition to deal with hash-collisions */
#if 0
#ifndef NOHASHMAGIC
 	#define NOHASHMAGIC 1
 	#define ENABLE_HASHMAGIC 0
#else
 	#if NOHASHMAGIC
 		#define ENABLE_HASHMAGIC 0
 	#else
 		#define ENABLE_HASHMAGIC 1
 	#endif
#endif

#if ENABLE_HASHMAGIC
    #define MAGIC_SIZE 4
#else
    #define MAGIC_SIZE 0
#endif
#endif
#define MAGIC_SIZE 0

/* Chunking-related module configuration */
#define initChunking(args...) initRabin(args)
#define invokeChunking(args...) invokeRabin(args)
#define exitChunking(args...) exitRabin(args)

#if 0
#ifndef PRO_STATS
	#define PRO_STATS
#endif
#endif

#ifdef __cplusplus
#define MY_EXTERN_C extern "C"
#else
#define MY_EXTERN_C extern
#endif

#define DUMMY_ID 1	


#endif	/* _PDD_CONFIG_H_ */

