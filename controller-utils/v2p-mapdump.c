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
#include "vector64.h"
#include "v2p-map.h"
#include "voltab.h"
#include "file-handling.h"
#include "v2p-mapdump.h"

int mapdumpflag = 0;

extern vector16 * v2pmaps;     /* Global V2P mapping information vector */
extern struct voltab voltab;

char *default_v2pdump = "v2pdump.txt";
char *default_voltabdump = "voltabdump.txt";
char v2pdump[MAXPATHLEN];
char voltabdump[MAXPATHLEN];
FILE *voltfp=NULL, *v2pfp=NULL;

int readup_v2pdump(void)
{
    int i, num_voltabentries, num_v2pentries;
    struct vol_datum *v = NULL;
    struct vm_info *vm = NULL;
    int bytes;

#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    /* open file for voltab */
    open_FILE(voltfp, voltabdump, "r");

    /* Readup number of entries from voltfp */
    generic_readupFILE(&num_voltabentries, int, 1, voltfp);

    /* For each line in voltabdump, add entry to voltab */
    i = 0;
    while (i < num_voltabentries)
    {
        v = calloc(1, sizeof(struct vol_datum));
        generic_readupFILE(v, struct vol_datum, 1, voltfp);
        if (hashtab_insert(voltab.table, (unsigned char*) v->vmname, v))
            RET_ERR("hashtab_insert into voltab failed for %s\n", v->vmname);
        i++;
    }

    /* open file for v2pmaps */
    open_FILE(v2pfp, v2pdump, "r");

    /* Readup number of entries from v2pfp */
    generic_readupFILE(&num_v2pentries, int, 1, v2pfp);

    /* For each line in v2pdump, add entry to v2pmaps */
    i = 0;
    while (i < num_v2pentries)
    {
        vm = calloc(1, sizeof(struct vm_info));
        generic_readupFILE(vm, struct vm_info, 1, v2pfp);
        vector16_set(v2pmaps, vm->volID, (void*)vm);
        i++;
    }

    return 0;
}

int dump_v2p(void)
{
    int i, num_v2pentries;
    struct vol_datum *v = NULL;
    struct vm_info *vm = NULL;

#ifdef PRODUMPING_TEST
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

	if (!voltfp)
    	open_FILE(voltfp, voltabdump, "w"); /* open file for voltab */

	if (!v2pfp)
	    open_FILE(v2pfp, v2pdump, "w"); /* open file for v2pmaps */

    /* Get count of entries in voltab = v2pmaps */
    num_v2pentries = vector16_size(v2pmaps);

    /* Write count to voltfp and v2pfp */
    generic_dumpFILE(&num_v2pentries, int, 1, voltfp);
    generic_dumpFILE(&num_v2pentries, int, 1, v2pfp);

    /* For each v in v2pmaps :-
     * 1. write v2pmap to v2pdump file
     * 2. write corresponding voltab entry to voltabdump
     */
    i = 0;
    while (i < num_v2pentries)
    {
        vm = (struct vm_info*) vector16_get(v2pmaps, i);
		if (!vm)
		{
			fprintf(stderr, "no v2pmaps for VM ID %d\n", i);
			return -1;
		}
		else
		{
			fprintf(stdout, "%s %u %u %u\n", vm->vmname, vm->volID, 
							vm->vBlkID_cap, vm->pBlkID_base);
	        generic_dumpFILE(vm, struct vm_info, 1, v2pfp);
		}

        v = hashtab_search(voltab.table, (unsigned char*) vm->vmname);
		if (!v)
		{
			fprintf(stderr, "no voltab entry for vmname=%s\n", vm->vmname);
			return -1;
		}
		else
	        generic_dumpFILE(v, struct vol_datum, 1, voltfp);

        i++;
    }
    return 0;
}

