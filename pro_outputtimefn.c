#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <asm/types.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include "pro_outputtimefn.h"

FILE * ftimeptr = NULL;

void outputtimefn_init(char *type)
{
	char *progname = "replay";
    char outputtimefn[356];
	int fd;
	fprintf(stdout, "In %s\n", __FUNCTION__);

    strcpy(outputtimefn, "accesstime_output_");
    strcat(outputtimefn, progname);
    strcat(outputtimefn, "_4K_");
    strcat(outputtimefn, type);
    strcat(outputtimefn, ".txt");
    ftimeptr = fopen(outputtimefn, "w");  /* output file, appended by diskname, time and date. Used to output hashes  */
	if (ftimeptr == NULL)
		printf("Output file open failed: %s\n", outputtimefn);
	else
    	printf("Opened output file.\n");

	/* FADVISE so that cache can be released immediately for this file */
	fd = fileno(ftimeptr);
	posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
}

void outputtimefn_exit()
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
    if (ftimeptr != NULL)
        fclose(ftimeptr);
}

