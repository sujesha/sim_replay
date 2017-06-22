#include <stdio.h>
#include <fcntl.h>
#include <sys/param.h>
#include <asm/types.h>
#include <string.h>
#include <assert.h>
#include "debug.h"
#include "debugg.h"
#include "pdd_config.h"
#include "unused.h"
#include "vmbunching_structs.h"
#include "vector16.h"
#include "vector32.h"
#include "v2p-map.h"
#include "v2f-map.h"
#include "f2pv-map.h"
#include "voltab.h"
#include "file-handling.h"
#include "fmapdump.h"
#include "v2p-mapdump.h"
#include "fixedtab.h"

extern vector16 * v2pmaps;     /* Global V2P mapping information vector */
extern vector16 * v2fmaps;     /* Global V2C mapping information vector */
extern struct voltab voltab;
extern vector32 * fixedmap_by_fid;
extern struct fixedtab fixedtab;
#if defined(REPLAYDIRECT_DEBUG_SS) || defined(CONDUMPING_TEST)
void humanreadable_mappingF();
#endif


extern char *default_v2pdump;
extern char *default_voltabdump;
char *default_v2fdump = "v2fdump.txt";
char *default_f2pvdump = "f2pvdump.txt";
char v2pdump[MAXPATHLEN];
char voltabdump[MAXPATHLEN];
char v2fdump[MAXPATHLEN];
char f2pvdump[MAXPATHLEN];
FILE *v2ffp=NULL;
FILE *f2pvfp=NULL;

void readinput_map_filenamesF()
{
	char ch = 'd';

	printf("Default dumpfiles are %s, %s, %s and %s. ",
            default_v2pdump, default_voltabdump, default_v2fdump,
            default_f2pvdump);
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
        printf("Enter filename for mapping v2f dump: ");
        if (scanf("%s", v2fdump) == 0)
            fatal(NULL, ERR_SYSCALL, "scanf failed\n");
        printf("Enter filename for mapping f2pv dump: ");
        if (scanf("%s", f2pvdump) == 0)
            fatal(NULL, ERR_SYSCALL, "scanf failed\n");
    }
    else
    {
        strcpy(v2pdump, default_v2pdump);
        strcpy(voltabdump, default_voltabdump);
        strcpy(v2fdump, default_v2fdump);
        strcpy(f2pvdump, default_f2pvdump);
    }
	return;
}

int dump_v2f(void)
{
	__u16 i=0, num_ventries;
	__u32 j, num_v2f;
    vector32 *ptri;
    v2f_datum *ptrj;

#ifdef CONDUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!v2ffp)
		open_FILE(v2ffp, v2fdump, "w"); /* open file for v2fmaps */

	/* Get count of entries for v2ffp */
	num_ventries = vector16_size(v2fmaps);

	/* Write num_ventries to v2ffp */
	generic_dumpFILE(&num_ventries, __u16, 1, v2ffp);

	/* For each volume---
	 * 1. Write num_v2f to v2ffp 
	 * 2. Write all v2f of the volume to v2ffp. For this:-
	 * 		a. bhashkey
	 * 		b. volID
	 * 		c. blockID
	 * 		d. fixedID
	 * 		e. fdirty
	 */
	while (i < num_ventries)
	{
		unsigned char fdirty;
		ptri = (vector32*) vector16_get(v2fmaps, i);
		num_v2f = vector32_size(ptri);
		generic_dumpFILE(&num_v2f, __u32, 1, v2ffp);	/* step 1 above */

		for (j = 0; j < vector32_size(ptri); j++)
		{
			ptrj = (v2f_datum*) vector32_get(ptri, j);
			//generic_dumpFILE(ptrj->bhashkey, unsigned char, 
		//					HASHLEN + MAGIC_SIZE, v2ffp);	/* a */
			//generic_dumpFILE(&ptrj->volID, __u16, 1, v2ffp);/*b*/
			//generic_dumpFILE(&ptrj->blockID, __u32, 1, v2ffp);/*c*/
			generic_dumpFILE(&ptrj->fixedID, __u32, 1, v2ffp);	/*d*/
			fdirty = (unsigned char)ptrj->fdirty;
			generic_dumpFILE(&fdirty, unsigned char, 1, v2ffp);	/* 4 */	
		}
		i++;
	}
	return 0;
}

int readup_v2fdump(void)
{
    FILE *v2ffp;
	__u16 i=0, num_ventries;
	__u32 j, num_v2f;
    v2f_datum *ptrj;
	int bytes;

#ifdef CONDUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    /* open file for v2fmaps */
    open_FILE(v2ffp, v2fdump, "r");

    /* Get count of entries for v2ffp */
	generic_readupFILE(&num_ventries, __u16, 1, v2ffp);

    /* Initialize f2vmaps of num_ventries size */
	vector16_resize(v2fmaps, num_ventries);

    /* For each volume---
     * 1. Read num_v2f from v2ffp
     * 2. Read all v2f of the volume from v2ffp. For each volume:-
     *      a. bhashkey
     *      b. volID
     *      c. blockID
	 *      d. fixedID
	 * 		e. fdirty
	 * 3. After constructing v2f, add to v2fmaps
     */
    while (i < num_ventries)
    {
		generic_readupFILE(&num_v2f, __u32, 1, v2ffp);    /* step 1 above */

		for (j = 0; j < num_v2f; j++)
		{
			unsigned char fdirty;
			ptrj = calloc(1, sizeof(v2f_datum));
			//generic_readupFILE(ptrj->bhashkey, unsigned char,
             //               HASHLEN + MAGIC_SIZE, v2ffp);   /* a */
			//generic_readupFILE(&ptrj->volID, __u16, 1, v2ffp);/*b*/
			//generic_readupFILE(&ptrj->blockID, __u32, 1, v2ffp);
			//ptrj->volID = i;
			//ptrj->blockID = j;
			generic_readupFILE(&ptrj->fixedID, __u32, 1, v2ffp);
			generic_readupFILE(&fdirty, unsigned char, 1, v2ffp);	/* e */	
			ptrj->fdirty = fdirty;

			v2fmaps_set(v2fmaps, i, j, ptrj);
		}
		i++;
	}

	return 0;	
}

int dump_f2pv(void)
{
    fixed_id_t i=0, num_fentries;
    int num_f2v;
    f2pv_datum *f2pv;
	F2V_tuple_t * f2v;

#ifdef CONDUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!f2pvfp)
	    open_FILE(f2pvfp, f2pvdump, "w"); /* open file for v2fmaps */

    /* Get count of entries for v2ffp */
    num_fentries = vector32_size(fixedmap_by_fid);

    /* Write num_ventries to v2ffp */
    generic_dumpFILE(&num_fentries, fixed_id_t, 1, f2pvfp);

	/* For each non-NULL fixedID (i = 0 to num_fentries-1) data:-
	 * 1. write fixedID
	 * 2. write fhashkey
	 * 6. get num of f2vmaps for this fixedID --- num_f2v
	 * 7. for each f2vmaps list (j = 0 to num_f2v-1)
	 * 		a. write dedupfetch
	 * 		b. write volID
	 * 		c. write blockID
	 */
	while (i < num_fentries)
	{
		f2pv = getFixedMap(i);
		if (f2pv == NULL)
		{
            if (i==0)
                fprintf(stdout, "Perhaps no zero block found so far, fine\n");
            else
                fprintf(stdout, "This is unexpected if before replay starts,"
                            " else nothing to worry.\n");
            i++;
            continue;
		}
		generic_dumpFILE(&f2pv->fixedID, fixed_id_t, 1, f2pvfp); /* 1 */
		generic_dumpFILE(f2pv->fhashkey, unsigned char, 
							HASHLEN + MAGIC_SIZE, f2pvfp);	/* 2 */

		num_f2v = slist_len(&f2pv->f2vmaps);
    	generic_dumpFILE(&num_f2v, int, 1, f2pvfp);

		struct slist_head *p;
		__slist_for_each(p, &f2pv->f2vmaps)	/* 7 */
		{
			unsigned char dedupfetch;
			f2v = slist_entry(p, struct F2V_tuple_t, head);
 			dedupfetch = (unsigned char)f2v->dedupfetch;
			generic_dumpFILE(&dedupfetch, unsigned char, 1, f2pvfp); /* a*/
			generic_dumpFILE(&f2v->volID, __u16, 1, f2pvfp);/*b*/
			generic_dumpFILE(&f2v->blockID, __u32, 1, f2pvfp);	/* c */
		}
		i++;
	}
		
	return 0;
}

int readup_f2pvdump(void)
{
    FILE *f2pvfp;
    fixed_id_t i=0, num_fentries;
    int j=0, num_f2v;
    f2pv_datum *f2pv;
	F2V_tuple_t * f2v;
	int bytes;

#ifdef CONDUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    /* open file for v2fmaps */
    open_FILE(f2pvfp, f2pvdump, "r");

    /* Get count of entries for v2ffp */
    generic_readupFILE(&num_fentries, fixed_id_t, 1, f2pvfp);

	/* Initialize fixedmap_by_fid and fixedtab */
	vector32_resize(fixedmap_by_fid, num_fentries);

	/* while fixedID (i < num_fentries) :-
	 * 1. read fixedID, add to f2pv
	 * 2. read fhashkey, add to f2pv;
	 * 4. read num of f2vmaps for this fixedID --- num_f2v
	 * 7. for each f2vmaps list (j = 0 to num_f2v-1)
	 * 		a. read dedupfetch, add to f2v
	 * 		b. read volID, add to f2v
	 * 		c. read blockID, add to f2v
	 * 		d. add f2v to f2vmaps
	 * while (i < fixedID), middle chunks missing -- already NULL initialized
	 * 6. add f2pv to fixedtab
	 * 7. i = fixedID + 1;
	 */
	while (i < num_fentries)
	{
		f2pv = (f2pv_datum*) calloc (1, sizeof(f2pv_datum));
		INIT_LIST_HEAD(&f2pv->f2vmaps);
		generic_readupFILE(&f2pv->fixedID, fixed_id_t, 1, f2pvfp); /* 1 */
		if (f2pv->fixedID > i)
			i = f2pv->fixedID;
		generic_readupFILE(f2pv->fhashkey, unsigned char, 
							HASHLEN + MAGIC_SIZE, f2pvfp);	/* 2 */

    	generic_readupFILE(&num_f2v, int, 1, f2pvfp);	/*4*/
		j = 0;
		while (j < num_f2v)	/* 5 */
		{
			unsigned char dedupfetch; 
			f2v = (F2V_tuple_t*) calloc (1, sizeof(F2V_tuple_t));
			generic_readupFILE(&dedupfetch, unsigned char, 1, f2pvfp);/*a*/
			f2v->dedupfetch = dedupfetch;
			generic_readupFILE(&f2v->volID, __u16, 1, f2pvfp); /*b*/
			generic_readupFILE(&f2v->blockID, __u32, 1, f2pvfp);	/* c */
			add_f2v_tuple_to_map(f2v, f2pv);	/* d */
			j++;
		}

		/* Add fixed info to fixedtab */
		if (hashtab_insert(fixedtab.table, f2pv->fhashkey, f2pv)) /* 6 */
			RET_ERR("hashtab_insert into fixedtab failed for f2pvdump\n");

		/* Add chunk info to fixedmap_by_fid */
		setFixedMap(f2pv->fixedID, f2pv);

		i++;
	}

	return 0;
}

void* mapreadup_routineF(void *arg)
{
#ifdef CONDUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	UNUSED(arg);

	if (readup_v2pdump())
		VOID_ERR("readup_v2pdump failed\n");

	if (readup_v2fdump())
		VOID_ERR("readup_v2fdump failed\n");

	if (readup_f2pvdump())
		VOID_ERR("readup_f2pvdump failed\n");

#if defined(REPLAYDIRECT_DEBUG_SS) || defined(CONDUMPING_TEST)
	humanreadable_mappingF();
#endif
	return NULL;
}


int mapdump_routineF(void)
{
#ifdef CONDUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

#if defined(CONDUMPING_TEST) || defined(REPLAYDIRECT_DEBUG_SS)
	humanreadable_mappingF();
#endif

	if (dump_v2p())
		RET_ERR("dump_v2p failed\n");

	if (dump_v2f())
		RET_ERR("dump_v2f failed\n");

	if (dump_f2pv())
		RET_ERR("dump_f2pv failed\n");

	return 0;
}
