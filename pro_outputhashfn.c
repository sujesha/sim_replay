#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <asm/types.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include "pro_outputhashfn.h"

FILE * fhashptr = NULL;

void outputhashfn_init(char *type)
{
	char *progname = "replay";
    char outputhashfn[356];
	int fd;
	fprintf(stdout, "In %s\n", __FUNCTION__);

    strcpy(outputhashfn, "hashrabin_output_");
    strcat(outputhashfn, progname);
    strcat(outputhashfn, "_4K_");
	strcat(outputhashfn, type);
    strcat(outputhashfn, ".txt");
    fhashptr = fopen(outputhashfn, "w");  /* output file, appended by diskname, time and date. Used to output hashes  */
	if (fhashptr == NULL)
		printf("Output file open failed: %s\n", outputhashfn);
	else
    	printf("Opened output file.\n");

	/* FADVISE so that cache can be released immediately for this file */
	fd = fileno(fhashptr);
	posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
}

void outputhashfn_exit()
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
    if (fhashptr != NULL)
        fclose(fhashptr);
}

