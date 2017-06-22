
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <asm/types.h>
#include "vector16.h"
#include "v2p-map.h"
#include "debug.h"

FILE *writemapp = NULL;

extern vector16 * v2pmaps;     /* Global V2P mapping information vector */
extern FILE *mapp;             /* FILE pointer for V2P map info */

int write_default_input_v2p_mapping(FILE *wmapp)
{
    struct vm_info * vm;
    char *linebuf;
	int i, num_v2pentries;
#ifdef RECREATE_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(wmapp != NULL);
#endif

    if (v2pmaps == NULL)
    {
        printf("No v2pmaps, so no to-do in write_default_input_v2p_mapping\n");
        return 0;
    }

    linebuf = calloc(81, sizeof(char));

    /* Get count of entries in v2pmaps */
    num_v2pentries = vector16_size(v2pmaps);

    i = 0;
    while (i < num_v2pentries)
    {
        vm = (struct vm_info*) vector16_get(v2pmaps, i);
		sprintf(linebuf, "%s %u %u\n", vm->vmname, vm->vBlkID_cap, 
				vm->pBlkID_base);
		fprintf(wmapp, linebuf, strlen(linebuf));
#if defined(RECREATE_DEBUG_SS) || defined(SIMREPLAY_DEBUG_SS)
		assert(vm != NULL);
        fprintf(stdout, "%s: Wrote line %s\n", __FUNCTION__, linebuf);
#endif

        i++;
    }
    free(linebuf);

    return 0;
}

/* write_input_v2p_mapping reads up the Virt-to-Phys mapping.
 * @mapp[input] file pointer of file containing the mapping
 * @return status
 */
int write_input_v2p_mapping()
{
#ifdef RECREATE_DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
    assert(mapp == NULL && writemapp != NULL);
#endif

    /* We assume a default 1:1 mapping */
    if (write_default_input_v2p_mapping(writemapp))
        RET_ERR("write_default_input_v2p_mapping failed\n");

    return 0;
}

