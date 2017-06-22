#include <stdio.h>
#include <fcntl.h>
#include <sys/param.h>
#include <asm/types.h>
#include <string.h>
#include <assert.h>
#include "mapdump.h"
#include "debug.h"
#include "debugg.h"
#include "pdd_config.h"
#include "unused.h"
#include "vmbunching_structs.h"
#include "vector16.h"
#include "vector32.h"
#include "v2p-map.h"
#include "v2c-map.h"
#include "c2pv-map.h"
#include "voltab.h"
#include "file-handling.h"
#include "mapdump.h"
#include "v2p-mapdump.h"
#include "chunktab.h"

extern vector16 * v2pmaps;     /* Global V2P mapping information vector */
extern vector16 * v2cmaps;     /* Global V2C mapping information vector */
extern struct voltab voltab;
extern vector32 * chunkmap_by_cid;
extern struct chunktab chunktab;
void humanreadable_mapping();


extern int mapdumpflag;

char *default_v2cdump = "v2cdump.txt";
char *default_c2pvdump = "c2pvdump.txt";
char v2cdump[MAXPATHLEN];
char c2pvdump[MAXPATHLEN];
FILE *v2cfp=NULL;
FILE *c2pvfp=NULL;

/* Extern'ed from v2p-mapdump.c */
extern char *default_v2pdump;
extern char *default_voltabdump;
extern char v2pdump[MAXPATHLEN];
extern char voltabdump[MAXPATHLEN];

void readinput_map_filenames()
{
	char ch = 'd';

	printf("Default dumpfiles are %s, %s, %s and %s. ",
            default_v2pdump, default_voltabdump, default_v2cdump,
            default_c2pvdump);
    while (ch != 'y' && ch != 'n')
    {
        printf("Are these filenames okay? (y/n): ");
        ch = getchar();
    }
    if (ch == 'n')
    {
        printf("Enter filename for mapping v2p dump: ");
        if (scanf("%s", v2pdump) == 0)
            fatal(NULL, ERR_SYSCALL, "scanf failed\n");
        printf("Enter filename for mapping voltab dump: ");
        if (scanf("%s", voltabdump) == 0)
            fatal(NULL, ERR_SYSCALL, "scanf failed\n");
        printf("Enter filename for mapping v2c dump: ");
        if (scanf("%s", v2cdump) == 0)
            fatal(NULL, ERR_SYSCALL, "scanf failed\n");
        printf("Enter filename for mapping c2pv dump: ");
        if (scanf("%s", c2pvdump) == 0)
            fatal(NULL, ERR_SYSCALL, "scanf failed\n");
    }
    else
    {
        strcpy(v2pdump, default_v2pdump);
        strcpy(voltabdump, default_voltabdump);
        strcpy(v2cdump, default_v2cdump);
        strcpy(c2pvdump, default_c2pvdump);
    }
	return;
}

int dump_v2c(void)
{
	__u16 i=0, num_ventries;
    int k, num_chunkid;
	__u32 j, num_v2c;
    vector32 *ptri;
    v2c_datum *ptrj;

#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!v2cfp)
		open_FILE(v2cfp, v2cdump, "w"); /* open file for v2cmaps */

	/* Get count of entries for v2cfp */
	num_ventries = vector16_size(v2cmaps);

	/* Write num_ventries to v2cfp */
	generic_dumpFILE(&num_ventries, __u16, 1, v2cfp);

	/* For each volume---
	 * 1. Write num_v2c to v2cfp 
	 * 2. Write all v2c of the volume to v2cfp. For this:-
	 * 		a. bhashkey
	 * 		b. volID
	 * 		c. blockID
	 * 		d. start_offset_into_chunk
	 * 		e. end_offset_into_chunk
	 * 		h. cdirty
	 * 		f. num_chunkid
	 * 		g. set of num_chunkid chunkIDs for chunkIDUList
	 */
	while (i < num_ventries)
	{
		ptri = (vector32*) vector16_get(v2cmaps, i);
		num_v2c = vector32_size(ptri);
		generic_dumpFILE(&num_v2c, __u32, 1, v2cfp);	/* step 1 above */

		for (j = 0; j < vector32_size(ptri); j++)
		{
			unsigned char cdirty;
			ptrj = (v2c_datum*) vector32_get(ptri, j);
			//generic_dumpFILE(ptrj->bhashkey, unsigned char, 
			//				HASHLEN + MAGIC_SIZE, v2cfp);	/* a */
			//generic_dumpFILE(&ptrj->volID, __u16, 1, v2cfp);/*b*/
			//generic_dumpFILE(&ptrj->blockID, __u32, 1, v2cfp);
			generic_dumpFILE(&ptrj->start_offset_into_chunk, chunk_size_t,
							1, v2cfp);
			generic_dumpFILE(&ptrj->end_offset_into_chunk, chunk_size_t,
							1, v2cfp);
			cdirty = (unsigned char)ptrj->cdirty;
			generic_dumpFILE(&cdirty, unsigned char, 1, v2cfp);	/* h */	
			num_chunkid = ulistLen(ptrj->chunkIDUList);
			generic_dumpFILE(&num_chunkid, int, 1, v2cfp);	/* f */
			for (k = 0; k < num_chunkid; k++)
			{
				chunk_id_t chunkID;
				chunkID = *(chunk_id_t*)getIndexedNode(ptrj->chunkIDUList, k);
				generic_dumpFILE(&chunkID, chunk_id_t, 1, v2cfp);	/* g */
			}
		}
		i++;
	}
	return 0;
}

int readup_v2cdump(void)
{
	__u16 i=0, num_ventries;
    int k, num_chunkid;
	__u32 j, num_v2c;
    v2c_datum *ptrj;
	int bytes;

#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!v2cfp)
	    open_FILE(v2cfp, v2cdump, "r"); /* open file for v2cmaps */

    /* Get count of entries for v2cfp */
	generic_readupFILE(&num_ventries, __u16, 1, v2cfp);

    /* Initialize c2vmaps of num_ventries size */
	vector16_resize(v2cmaps, num_ventries);

    /* For each volume---
     * 1. Read num_v2c from v2cfp
     * 2. Read all v2c of the volume from v2cfp. For each volume:-
     *      a. bhashkey
     *      b. volID
     *      c. blockID
     *      d. start_offset_into_chunk
     *      e. end_offset_into_chunk
	 * 		h. cdirty
     *      f. num_chunkid
     *      g. read set of num_chunkid chunkIDs, add to chunkIDUList
	 * 3. After constructing v2c, add to v2cmaps
     */
    while (i < num_ventries)
    {
		generic_readupFILE(&num_v2c, __u32, 1, v2cfp);    /* step 1 above */

		for (j = 0; j < num_v2c; j++)
		{
			unsigned char cdirty;
			ptrj = calloc(1, sizeof(v2c_datum));
			ptrj->chunkIDUList = NULL;
			//generic_readupFILE(ptrj->bhashkey, unsigned char,
            //                HASHLEN + MAGIC_SIZE, v2cfp);   /* a */
			//generic_readupFILE(&ptrj->volID, __u16, 1, v2cfp);/*b*/
			//generic_readupFILE(&ptrj->blockID, __u32, 1, v2cfp);
			//ptrj->volID = i;
			//ptrj->blockID = j;
			generic_readupFILE(&ptrj->start_offset_into_chunk, chunk_size_t,
                            1, v2cfp);
			generic_readupFILE(&ptrj->end_offset_into_chunk, chunk_size_t,
                            1, v2cfp);
			generic_readupFILE(&cdirty, unsigned char, 1, v2cfp);	/* 4 */	
			ptrj->cdirty = cdirty;
			generic_readupFILE(&num_chunkid, int, 1, v2cfp);  /* f */

			for (k = 0; k < num_chunkid; k++)
            {
				chunk_id_t chunkID;
				generic_readupFILE(&chunkID, chunk_id_t, 1, v2cfp);   /* g */
				ptrj->chunkIDUList = addtoUList(ptrj->chunkIDUList, &chunkID,
                    sizeof(chunk_id_t));
			}
			v2cmaps_set(v2cmaps, i, j, ptrj);
		}
		i++;
	}

	return 0;	
}

int dump_c2pv(void)
{
    chunk_id_t i=0, num_centries;
    int num_c2v;
    c2pv_datum *c2pv;
	C2V_tuple_t * c2v;

#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!c2pvfp)
	    open_FILE(c2pvfp, c2pvdump, "w"); /* open file for v2cmaps */

    /* Get count of entries for v2cfp */
    num_centries = vector32_size(chunkmap_by_cid);

    /* Write num_ventries to v2cfp */
    generic_dumpFILE(&num_centries, chunk_id_t, 1, c2pvfp);

	/* For each non-NULL chunkID (i = 0 to num_centries-1) data:-
	 * 1. write chunkID
	 * 2. write chashkey
	 * 3. write clen
	 * 5. write cforced
	 * 6. get num of c2vmaps for this chunkID --- num_c2v
	 * 7. for each c2vmaps list (j = 0 to num_c2v-1)
	 * 		a. write dedupfetch
	 * 		b. write volID
	 * 		c. write start_vblk_id
	 * 		d. write start_offset_into_vblk
	 */
	while (i < num_centries)
	{
		unsigned char cforced; //, //cdirty;
		c2pv = getChunkMap(i);
		if (c2pv == NULL)
		{
            if (i==0)
                fprintf(stdout, "Perhaps no zero block/chunk found so far, fine\n");
            else
                fprintf(stdout, "This is unexpected if before replay starts,"
                            " else nothing to worry.\n");
            i++;
            continue;
		}
		generic_dumpFILE(&c2pv->chunkID, chunk_id_t, 1, c2pvfp); /* 1 */
		generic_dumpFILE(c2pv->chashkey, unsigned char, 
							HASHLEN + MAGIC_SIZE, c2pvfp);	/* 2 */
		generic_dumpFILE(&c2pv->clen, chunk_size_t, 1, c2pvfp); /* 3 */
		cforced = (unsigned char)c2pv->cforced;
		generic_dumpFILE(&cforced, unsigned char, 1, c2pvfp);	/* 5 */	

		num_c2v = slist_len(&c2pv->c2vmaps);
    	generic_dumpFILE(&num_c2v, int, 1, c2pvfp);

		struct slist_head *p;
		__slist_for_each(p, &c2pv->c2vmaps)	/* 7 */
		{
			unsigned char dedupfetch;
			c2v = slist_entry(p, struct C2V_tuple_t, head);
			dedupfetch = (unsigned char)c2v->dedupfetch;
			generic_dumpFILE(&dedupfetch, unsigned char, 1, c2pvfp); /* a*/
			generic_dumpFILE(&c2v->volID, __u16, 1, c2pvfp);/*b*/
			generic_dumpFILE(&c2v->start_vblk_id, __u32, 1, c2pvfp);	/* c */
			generic_dumpFILE(&c2v->start_offset_into_vblk, __u16, 1, c2pvfp);
		}
		i++;
	}
		
	return 0;
}

int readup_c2pvdump(void)
{
    chunk_id_t i=0, num_centries;
    int j=0, num_c2v;
    c2pv_datum *c2pv;
	C2V_tuple_t * c2v;
	int bytes;

#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!c2pvfp)
	    open_FILE(c2pvfp, c2pvdump, "r"); /* open file for v2cmaps */

    /* Get count of entries for v2cfp */
    generic_readupFILE(&num_centries, chunk_id_t, 1, c2pvfp);

	/* Initialize chunkmap_by_cid and chunktab */
	vector32_resize(chunkmap_by_cid, num_centries);

	/* while chunkID (i < num_centries) :-
	 * 1. read chunkID, add to c2pv
	 * 2. read chashkey, add to c2pv;
	 * 3. read clen, add to c2pv
	 * 5. read cforced
	 * 6. read num of c2vmaps for this chunkID --- num_c2v
	 * 7. for each c2vmaps list (j = 0 to num_c2v-1)
	 * 		a. read dedupfetch, add to c2v
	 * 		b. read volID, add to c2v
	 * 		c. read start_vblk_id, add to c2v
	 * 		d. read start_offset_into_vblk, add to c2v
	 * 		e. add c2v to c2vmaps
	 * while (i < chunkID), middle chunks missing -- already NULL initialized
	 * 8. add c2pv to chunktab
	 * 9. i = chunkID + 1;
	 */
	while (i < num_centries)
	{
		unsigned char cforced; //,cdirty
		c2pv = (c2pv_datum*) calloc (1, sizeof(c2pv_datum));
		INIT_LIST_HEAD(&c2pv->c2vmaps);
		generic_readupFILE(&c2pv->chunkID, chunk_id_t, 1, c2pvfp); /* 1 */
		if (c2pv->chunkID > i)
			i = c2pv->chunkID;
		generic_readupFILE(c2pv->chashkey, unsigned char, 
							HASHLEN + MAGIC_SIZE, c2pvfp);	/* 2 */
		generic_readupFILE(&c2pv->clen, chunk_size_t, 1, c2pvfp); /* 3 */
		generic_readupFILE(&cforced, unsigned char, 1, c2pvfp);	/* 5 */	
		c2pv->cforced = cforced;

    	generic_readupFILE(&num_c2v, int, 1, c2pvfp);
		j = 0;
		while (j < num_c2v)	/* 7 */
		{
			unsigned char dedupfetch;
			c2v = calloc (1, sizeof(C2V_tuple_t));
			generic_readupFILE(&dedupfetch, unsigned char, 1, c2pvfp);/*a*/
			c2v->dedupfetch = dedupfetch;
			generic_readupFILE(&c2v->volID, __u16, 1, c2pvfp); /*b*/
			generic_readupFILE(&c2v->start_vblk_id, __u32, 1, c2pvfp);	/* c */
			generic_readupFILE(&c2v->start_offset_into_vblk, __u16, 1, c2pvfp);
			add_c2v_tuple_to_map(c2v, c2pv);	/* e */
			j++;
		}

		/* Add chunk info to chunktab */
		if (hashtab_insert(chunktab.table, c2pv->chashkey, c2pv)) /* 8 */
			RET_ERR("hashtab_insert into chunktab failed for c2pvdump\n");

		/* Add chunk info to chunkmap_by_cid */
		setChunkMap(c2pv->chunkID, c2pv);

		i++;
	}

	return 0;
}

void* mapreadup_routine(void *arg)
{
#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	UNUSED(arg);

	if (readup_v2pdump())
		VOID_ERR("readup_v2pdump failed\n");

	if (readup_v2cdump())
		VOID_ERR("readup_v2cdump failed\n");

	if (readup_c2pvdump())
		VOID_ERR("readup_c2pvdump failed\n");

#if defined(PRODUMPING_TEST) || defined(REPLAYDIRECT_DEBUG_SS)
	humanreadable_mapping();
#endif
	return NULL;
}


int mapdump_routine(void)
{
#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#if defined(PRODUMPING_TEST) || defined(REPLAYDIRECT_DEBUG_SS)
	humanreadable_mapping();
#endif

	if (dump_v2p())
		RET_ERR("dump_v2p failed\n");

	if (dump_v2c())
		RET_ERR("dump_v2c failed\n");

	if (dump_c2pv())
		RET_ERR("dump_c2pv failed\n");

	return 0;
}
