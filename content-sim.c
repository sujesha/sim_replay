/* This file is built similar to the v2f-map.c file.
 * It is intended to hold the md5 strings where disk is simulated for PROVIDED.
 * Main content is simmd5str 2D vector which can be looked up to get the 
 * MD5 string input for every block number.
 * ABANDONED because holding all MD5 content would need GBs of memory!!
 * Opting for file input instead.
 */
#include <stdio.h>
#include <fcntl.h>
#include <assert.h>
#include <string.h>
#include <asm/types.h>
#include "vector16.h"
#include "v2p-map.h"
#include "debug.h"
#include "unused.h"

vector16 * simmd5str;	/* Global simulated content store */
extern __u16 numVolID;
int simmd5str_alive = 0;

extern int disksimflag;
extern int preplayflag;

void create_simmd5str_space()
{
	if (simmd5str_alive == 1)
	{
		fprintf(stdout, "simmd5str already non-NULL\n");
		return;
	}
	simmd5str_alive = 1;

	simmd5str = calloc(1, sizeof(vector16));
	vector16_init(simmd5str);

	return;
}

void free_simmd5str(void)
{
	__u16 i;		/* For iterating over volID */
	__u16 j;
    vector32 *ptri;
	__u8 *ptrj;	

	if (simmd5str_alive == 0)
	{
		fprintf(stdout, "simmd5str being freed already\n");
		return;
	}
	simmd5str_alive = 0;

    for (i = 0; i < vector16_size(simmd5str); i++)    /* Iterating over volID */
    {
        if ((ptri = vector16_get(simmd5str, i)) != NULL)
        {
            for (j = 0; j < vector32_size(ptri); j++)/* Iterating over vblkID */
            {
                if ((ptrj = (__u8*) vector32_get(ptri, j)) != NULL)
                    free(ptrj);     /* Freeing the md5str of vblk */
            }
            free(ptri);     /* Freeing the vector per volume */
        }
    }
    free(simmd5str->data);
    free(simmd5str);
    fprintf(stdout, "done now\n");
}	
