/* This file has code that gets input for the provided module,
 * for example, the volume information (volID, startblk, endblk) and
 * for example, the V2P mappings for the virtual blocks of every volume
 */
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <asm/types.h>
#include "vector16.h"
#include "v2p-map.h"
#include "debug.h"
#include "voltab.h"
#include "sync-disk-interface.h"
#include "unused.h"
#include "replay-defines.h"

/* Use input option -m to provide the file containing the V2P map.
 * If none specified, mapping can be read up from default_V2PmapFile
 * Input of V2P mapping will fail if --
 * a) V2PmapFile (given or default) is empty or broken
 * b) Overlapping volume info (i.e. different VMs having overlapping blks)
 */

char *V2PmapFile = NULL;		/* input V2P mapping file */
//char *default_V2PmapFile = "v2p_map.txt";	/* default V2P mapping file */
char *default_V2PmapFile = NULL;
FILE *mapp;				/* FILE pointer for V2P map info */

vector16 * v2pmaps;		/* Global V2P mapping information vector */
__u16 numVolID = 0;	/* iterator for volumes */
int v2pmaps_alive = 0;

extern struct voltab voltab;	/* voltab.c */
extern int disksimflag;
extern int runtimemap;
extern int preplayflag;

__u16 capVolumes(void)
{
#if defined(PROCHUNKING_DEBUG_SSS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	return vector16_size(v2pmaps);
}

int get_volID(char *vmname)
{
	struct vol_datum *v = hashtab_search(voltab.table, (unsigned char*)vmname);
#if defined(SIMREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "get_volID vmname=%s\n", vmname);
#endif
	if (v == NULL && (DISKSIM_RUNTIMEMAP_COLLECTFORMAT_VMBUNCHREPLAY ||
				DISKSIM_RUNTIMEMAP_COLLECTFORMAT_PIOEVENTSREPLAY ||
		sreplayflag || DISKSIM_SCANNINGTRACE_COLLECTFORMAT_PIOEVENTSREPLAY
					|| DISKSIM_RUNTIMEMAP_NOCOLLECTFORMAT_PIOEVENTSREPLAY
					|| DISKSIM_VANILLA_NOCOLLECTFORMAT_PIOEVENTSREPLAY))
#if defined(RECREATE) || defined(DIRECTRECREATE) || defined(SIM_REPLAY)
	{
		struct vm_info *vm, *lastvm;

		/* If we are here, we are in recreate hard-disk step, and hence
		 * wish to allow the scantrace data to dictate the voltab entry
		 */
		v = (struct vol_datum*)calloc(1, sizeof(struct vol_datum));
	    if (v == NULL)
	        RET_ERR("vol_datum not allocated\n");

		strcpy(v->vmname, vmname);
		v->volID = numVolID++;
		if (hashtab_insert(voltab.table, (unsigned char*) v->vmname, v))
        	RET_ERR("hashtab_insert into voltab failed for %s\n", vmname);

#if defined(RECREATE_DEBUG_SS) || defined(DIRECTRECREATE_DEBUG_SS)
		fprintf(stdout, "voltab entry for %s, %u done\n", v->vmname, v->volID);
#endif
		/* We also need to make entry in v2pmaps */
		vm = calloc(1, sizeof(struct vm_info));
		vm->volID = v->volID;
		strcpy(vm->vmname, vmname);

		/* To deduce the next available pBlkID_base, fetch the previous
		 * vm_info and calculate 
		 */
		if (vm->volID == 0)
		{
			if (!disksimflag)
				vm->pBlkID_base = 50000;	//hard-coded
			else
				vm->pBlkID_base = 0;	//hard-coded
		}
		else
		{
            if (V2PmapFile == NULL || mapp == NULL)
            {
                /* If this is a multi-VM trace for replay, ensure that
                 * v2p map has been specified beforehand.
                 */
                RET_ERR("Because this is multi-VM replay, ensure that "
                        "-m option has been used to specify "
                        "V2P map, and also that the file is open for "
                        "read access\n");
            }

			lastvm = vector16_get(v2pmaps, vm->volID-1);
			vm->pBlkID_base = lastvm->pBlkID_base + lastvm->vBlkID_cap;
		}
		printf("%s: vm->pBlkID_base for VM %u = %u\n", __FUNCTION__,
		        vm->volID, vm->pBlkID_base);

		/* Initialize vBlkID_cap, Will eventually get updated in RECREATE */
		vm->vBlkID_cap = 0;

		vector16_set(v2pmaps, vm->volID, (void*)vm);
	}
	else if (v==NULL && !runtimemap)
		RET_ERR("vmname %s has no entry in voltab even though apriori!\n", vmname);
#else
	{
		/* If we are here, we are probably trying to do replay, in
		 * this case, the V2P map should already be up-to-date, else
		 * report error.
		 */
		RET_ERR("vmname %s has no entry in voltab\n", vmname);
	}
#endif
	return v->volID;
}

/* Check if input volID exists in the input seen so far */
int volIDexists(__u16 volidx)
{
	if (vector16_get(v2pmaps, volidx) != NULL)
		return 1;
	else
		return 0;
}

/* Get the blockID of the first vblk of the specified volume */
void getFirstvBlk(__u16 volID, __u32 *start)
{
	UNUSED(volID);
	*start = (__u32)0;
}

/* Get the blockID of the last vblk of the specified volume */
int getLastvBlk(__u16 volID, __u32 *end)
{
	struct vm_info *vm;
	if ((vm = vector16_get(v2pmaps, volID)) != NULL)
	{
		*end = (__u32) vm->vBlkID_cap - 1;
		return 0;
	}
	RET_ERR("No vm_info available for volID %u\n", volID);
}

#if 0
/* Constructor for the struct vm_info, so that it can be added to 
 * a C++ vector data structure
 */
struct vm_info make_vm_info(__u16 x, __u32 y, __u32 z) 
{
    struct vm_info myvminfo = {x, y, z};
    return myvminfo;
}
#endif

/* readnexttoken - Tokenizing and reading the next token
 * Returns 0 for success 
 */
int readnexttoken(char **ptr, char *sep, char **rest, char **token)
{
#if DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    *token = strtok_r(*ptr, sep, rest);
    if (*token == NULL)
	{
        RET_ERR(":%s: NULL token retrieved\n", __FUNCTION__);
	}
#if DEBUG_SS
    fprintf(stdout, "In %s, token = %s\n", __FUNCTION__, *token);
#endif

    *ptr = *rest;
	return 0;
}

/* Wrapper for readnexttoken() which expects to read VM name */
int read_vmname(struct vm_info *v, char **ptr, char **rest)
{
	char *vmnamestr;
#ifdef DEBUG_SS
	assert(*ptr != NULL && *rest != NULL);
#endif

#ifdef DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (readnexttoken(ptr, " ", rest, &vmnamestr))
		RET_ERR("readnexttoken() failed in %s\n", __FUNCTION__);

#ifdef DEBUG_SS
	fprintf(stdout, "After getnexttoken In %s %s\n", __FUNCTION__, vmnamestr);
#endif

	strcpy(v->vmname, vmnamestr);
	return 0;
}

/* We need to read vmname instead of reading volID 
 * and then convert vmname to volID and store into hashtable
 */
#if 0
/* Read token volID from input string.
 * Returns 0 for success 
 */
int read_volID(struct vm_info *v, char **ptr, char **rest)
{
	char *volIDstr;
	assert(*ptr != NULL && *rest != NULL);

#ifdef DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (readnexttoken(ptr, " ", rest, &volIDstr))
		RET_ERR("readnexttoken() failed in %s\n", __FUNCTION__);

#ifdef DEBUG_SS
    fprintf(stdout, "After getnexttoken In %s %s\n", __FUNCTION__, volIDstr);
#endif

	sscanf(volIDstr, "%u", v->volID);
	return 0;
}
#endif

/* Read token "vblks capacity" from input string, wrapper to readnexttoken
 * Returns 0 for success 
 */
int read_vBlkID_cap(struct vm_info *v, char **ptr, char **rest)
{
	char *vBlkID_capstr;
	unsigned long long cap;
#ifdef DEBUG_SS
	assert(*ptr != NULL && *rest != NULL);
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (readnexttoken(ptr, " ", rest, &vBlkID_capstr))
		RET_ERR("readnexttoken() failed in %s\n", __FUNCTION__);
#ifdef DEBUG_SS
    fprintf(stdout, "After getnexttoken In %s %s\n", __FUNCTION__, 
													vBlkID_capstr);
#endif

	sscanf(vBlkID_capstr, "%llu", &cap);
	v->vBlkID_cap = (__u32)cap;
	return 0;

}

/* Read token "pblk baseID" from input string, wrapper to readnexttoken
 * Returns 0 for success 
 */
int read_pBlkID_base(struct vm_info *v, char **ptr, char **rest)
{
	char *pBlkID_basestr;
	//__u32 base;

#ifdef DEBUG_SS
	assert(*ptr != NULL && *rest != NULL);
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (readnexttoken(ptr, " ", rest, &pBlkID_basestr))
		RET_ERR("readnexttoken() failed in %s\n", __FUNCTION__);

#ifdef DEBUG_SS
    fprintf(stdout, "After getnexttoken In %s %s\n", __FUNCTION__, 
													pBlkID_basestr);
#endif

//	sscanf(pBlkID_basestr, "%llu", &base);
//	v->pBlkID_base = (__u32)base;
	v->pBlkID_base = (__u32)atol(pBlkID_basestr);

	return 0;
}

/* Generic routine to test overlap between any two given ranges 
 * of numbers
 */
int testOverlap(__u32 x1, __u32 x2, __u32 y1, __u32 y2)
{
	if (x1 <= y2 && y1 <= x2)
		return 1;
	else
		return 0;
}

/* Tests the overlap between the pblk ranges of a VM pair */
int testOverlapVM(struct vm_info *ptri, struct vm_info *v)
{
	if (testOverlap(ptri->pBlkID_base, ptri->pBlkID_base + ptri->vBlkID_cap-1,
					v->pBlkID_base, v->pBlkID_base + v->vBlkID_cap - 1))
		return 1;
	else
		return 0;	
}

/* verify_vm_info checks the following:-
 * a) vmname should be a new one, no repetition
 * b) base phys address should be such that it doesnt overlap with
 * 		any other volumes input so far
 * c) If all checks succeed, assign next volID to this V2P map and add
 * 		vmname entry into voltab hashtable as well.
 * Returns 1 for successful verification, 0 for failure
 */
int verify_vm_info(struct vm_info *v)
{
#if RECREATE_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	struct vm_info *ptri;
	struct vol_datum *vdatum;
	__u16 i;

	if (hashtab_search(voltab.table, (unsigned char*) v->vmname))
	{
		VOID_ERR("volume for %s already exists\n", v->vmname);
		return 0;
	}

	for (i = 0; i < vector16_size(v2pmaps); i++)
    {
    	if ((ptri = (struct vm_info*) vector16_get(v2pmaps, i)) != NULL)
		{
			if (testOverlapVM(ptri, v))
			{
				VOID_ERR("overlapping v2p space not acceptable\n");
				return 0;
			}
		}
	}
#if RECREATE_DEBUG_SS
    fprintf(stdout, "verify success\n");
#endif

	/* Assigning the next volID for this vmname */
	v->volID = numVolID++;

	/* Create vdatum node to be inserted into voltab hashtable for vmnames */
	vdatum = calloc(1, sizeof(struct vol_datum));
	if (vdatum == NULL)
	{
		VOID_ERR("vdatum not allocated\n");
		return 0;
	}
	strcpy(vdatum->vmname, v->vmname);		/* copy vmname */
	vdatum->volID = v->volID;				/* copy volID */
	if (hashtab_insert(voltab.table, (unsigned char*) vdatum->vmname, vdatum))	
	{
		VOID_ERR("hashtab_insert into voltab failed for %s\n", vdatum->vmname);
		return 0;
	}
#if defined(RECREATE_DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS_DONE)
	assert(vector16_get(v2pmaps, v->volID) == NULL);
	fprintf(stdout, "Added %s, %u to voltab\n", v->vmname, v->volID);
    fprintf(stdout, "verify return success\n");
#endif

	/* Return success */
	return 1;
}

/* interpret_one_v2p_map interprets the given input line as
 * <volID vBlkID_cap pBlkID_base> and verifies the input.
 * Returns 0 for success
 */
int interpret_one_v2p_map(char **ptr, char **rest, struct vm_info *v)
{
#if RECREATE_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	
	read_vmname(v, ptr, rest);
	read_vBlkID_cap(v, ptr, rest);
	read_pBlkID_base(v, ptr, rest);

	if (verify_vm_info(v))
		return 0;	/* success */

	/* failure */
	RET_ERR("verify_vm_info() failed for vm_info %s, %u, %u, %u\n",
			v->vmname, v->volID, v->vBlkID_cap, v->pBlkID_base);
}

void create_v2pmaps(void)
{
#ifdef RECREATE_DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    if (v2pmaps_alive == 1)
    {
        fprintf(stdout, "v2pmaps already non-NULL\n");
        return;
    }
    v2pmaps_alive = 1;

	/* Initialize the vector for v2pmaps */
	v2pmaps = calloc(1, sizeof(vector16));
	if (v2pmaps == NULL)
		VOID_ERR("Couldnt allocate v2pmaps\n");
	vector16_init(v2pmaps);

    /* Initialize hashtable (voltab) for vmname */
    if (voltab_init(&voltab, VOLTAB_SIZE))
        VOID_ERR("voltab_init failed\n");

	return;
}


/* In the evaluation, we assume a default 1:1 mapping for 
 * virtual-to-physical blocks. So, default_read_input_v2p_mapping 
 * fills up the v2pmaps 2D vector with such a default mapping. 
 * For our implementation, this file contains a very simple mapping
 * information due to the default 1:1 mapping. On every line, there
 * will be 3 entities, 
 * 		a) vmname
 * 		b) numBlks in volume
 * 		c) base phys blk address for given volume
 * Each line in input file represents input info of one VM/volume.
 * vmname is mapped to volID, and uniqueness of vmname is checked 
 * via hash-table, voltab. So, before reading in the mapping file,
 * initialialize the hashtable, and for every VM entry, add entry
 * to hashtable.
 *
 * @mapp[input] file pointer of file containing the mapping 
 * @return status
 */
int read_default_input_v2p_mapping(FILE *mapp)
{
#if defined(RECREATE_DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS)
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	struct vm_info * vm;
	char *linebuf=NULL, *linebuf2=NULL, *rest, *ptr;
	size_t n;	/* size_t required by getline */

#ifdef DEBUG_SS	
	assert(mapp != NULL);
#endif

	while(getline(&linebuf, &n, mapp) > 0)
	{
		linebuf2 = linebuf;
		rest = linebuf;
#if defined(RECREATE_DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS)
		fprintf(stdout, "Read line %s\n", linebuf);
#endif
		if (linebuf[0] == '#')
		{
#if defined(RECREATE_DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS)
			fprintf(stdout, "Found comment line in file, ignore\n");
#endif
			free(linebuf);
			linebuf = NULL;
			continue;
		}

		vm = calloc(1, sizeof(struct vm_info));
		if (vm == NULL)
            RET_ERR("vm_info not allocated\n");
		memset(vm, 0, sizeof(struct vm_info));

		ptr = linebuf;
		if (interpret_one_v2p_map(&ptr, &rest, vm))
			RET_ERR("interpret_one_v2p_map failed\n");

		/* Adding entry to v2pmaps */
		vector16_set(v2pmaps, vm->volID, (void*)vm);
#if defined(RECREATE_DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS) || defined(PDD_REPLAY_DEBUG_SS_DONE)
		fprintf(stdout, "Added %s, %u, %u, %u to v2pmaps\n", 
				vm->vmname, vm->vBlkID_cap, vm->pBlkID_base, vm->volID);
#endif

		if (linebuf2 != NULL)
			free(linebuf2);
		linebuf = linebuf2 = NULL;
	}
	if (linebuf != NULL)
	{
		free(linebuf);
		return 0;
	}
		
	return 0;
}

/* read_input_v2p_mapping reads up the Virt-to-Phys mapping.
 * @mapp[input] file pointer of file containing the mapping 
 * @return status
 */
int read_input_v2p_mapping()
{
#if defined(RECREATE_DEBUG_SS) || defined(PDDREPLAY_DEBUG_SS_DONE)
	fprintf(stdout, "In %s\n", __FUNCTION__);
	assert(mapp != NULL);
#endif

	/* We assume a default 1:1 mapping */
	if (read_default_input_v2p_mapping(mapp))
		RET_ERR("read_default_input_v2p_mapping failed\n");

	fclose(mapp);
	mapp = NULL;

	return 0;
}

/* getVirttoPhysMap: Looks up the virt-to-phys mapping and returns pblk ID.
 * Returns 0 for success.
 */
int getVirttoPhysMap(__u16 volID, __u32 vBlkID, __u32 *pBlkID)
{
    struct vm_info *v = (struct vm_info*) vector16_get(v2pmaps, volID);
#if defined (PDDREPLAY_DEBUG_SS) || defined(PROCHUNKING_DEBUG_SSS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif 

#if defined(RECREATE) || defined(DIRECTRECREATE)
	__u16 i;
	struct vm_info *ptri;
    assert(volID == v->volID);
#endif

#if defined(RECREATE) || defined(DIRECTRECREATE)	
	/* If we are here, it implies that we are in recreate hard-disk program,
	 * so the scantrace should be allowed to govern the vBlkID_cap, instead
	 * of the value manually input through V2P map file. 
	 */
#ifdef RECREATE_DEBUG_SS
	fprintf(stdout, "Incrementing the vBlkID_cap because in recreatedisk\n");
#endif
	v->vBlkID_cap = vBlkID+1;

	for (i = 0; i < vector16_size(v2pmaps); i++)
    {
    	if ((ptri = (struct vm_info*) vector16_get(v2pmaps, i)) != NULL)
		{
			if (ptri != v && testOverlapVM(ptri, v))
				RET_ERR("overlapping v2p space not acceptable\n");
		}
	}
#else
    if (vBlkID >= v->vBlkID_cap && !disksimflag)
    {
        RET_ERR("vblk %u for volID %u is out-of-bounds %u\n",
                        vBlkID, volID, v->vBlkID_cap);
    }
#endif

    *pBlkID = v->pBlkID_base + vBlkID;
    return 0;
}

int updateVirttoPhysMap(__u16 volID, __u32 vBlkID)
{
    struct vm_info *v = (struct vm_info*) vector16_get(v2pmaps, volID);

	__u16 i;
	struct vm_info *ptri;

	if (vBlkID+1 <= v->vBlkID_cap)
		return 0;	//nothing to do

	v->vBlkID_cap = vBlkID+1;
	for (i = 0; i < vector16_size(v2pmaps); i++)
    {
    	if ((ptri = (struct vm_info*) vector16_get(v2pmaps, i)) != NULL)
		{
			if (ptri != v && testOverlapVM(ptri, v))
				RET_ERR("overlapping v2p space not acceptable\n");
		}
	}
	return 0;
}

int _pro_read_block(int dp, __u32 start, unsigned char *buf)
{
#if defined(PROCHUNKING_DEBUG_SSS)
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    if (_do_bread(dp, start, BLKSIZE, buf))
        RET_ERR("_do_bread gave error in _pro_read_block\n");

    return BLKSIZE;
}

int pro_read_block(int dp, __u16 volID, __u32 vBlkID,
                unsigned char *buf)
{
    int bytes_read = 0;
    __u32 iter;

    getVirttoPhysMap(volID, vBlkID, &iter);
#if defined(PROCHUNKING_DEBUG_SSS)
    fprintf(stdout, "In %s, pblkID = %llu\n", __FUNCTION__, iter);
#endif
    bytes_read = _pro_read_block(dp, iter, buf);

    return bytes_read;
}

void free_v2pmaps(void)
{
    int i;
    struct vm_info *ptri;
    fprintf(stdout, "In %s\n", __FUNCTION__);

	if (v2pmaps_alive == 0)
	{
		fprintf(stdout, "v2pmaps already being freed\n");
		return;
	}
	v2pmaps_alive = 0;
#if defined(PDDREPLAY_DEBUG_SS) || defined(PROMAPPING_DEBUG_SS)
    assert(v2pmaps != NULL);
#endif

    for (i = 0; i < vector16_size(v2pmaps); i++)
    {
        if ((ptri = (struct vm_info*) vector16_get(v2pmaps, i)) != NULL)
            free(ptri);     /* Freeing the vm_info node */
    }
	free(v2pmaps->data);
    free(v2pmaps);

	/* Free voltab */
	voltab_exit(&voltab);

	fprintf(stdout, "done now\n");
}


#if 0
not needed... just use getVirttoPhysMap()
int getPhysBlkID(__u16 volID, __u32 vBlkID, )
{
    __u32 pBlkID;
    if (getVirttoPhysMap(volID, vBlkID, &pBlkID))
        RET_ERR;

    return pBlkID;
}
#endif

