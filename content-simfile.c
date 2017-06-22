#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <asm/types.h>
#include <assert.h>
#include <string.h>
#include <time.h>
#include "pdd_config.h"
#include "debug.h"
#include "content-gen.h"
#include "content-simfile.h"
#include "utils.h"
#include "md5.h"

FILE * simdiskptr = NULL;

extern int disksimflag;
extern int preplayflag;
extern int collectformat;

void simdiskfn_init(char *type)
{
	char *progname = "replay";
	char simdiskfn[356];
	char cmd[346];
	fprintf(stdout, "In %s\n", __FUNCTION__);

	assert(disksimflag); 
//	assert(disksimflag && (preplayflag || !collectformat));

    strcpy(simdiskfn, "simdisk_");
    strcat(simdiskfn, progname);
    strcat(simdiskfn, "_4K_");
    strcat(simdiskfn, type);
    strcat(simdiskfn, ".txt");
	strcpy(cmd, "touch ");
	strcat(cmd, simdiskfn);
	printf("cmd = %s\n", cmd);
	if (system(cmd))
	{
		VOID_ERR("touch %s so that fopen will succeed\n", simdiskfn);
	}
    simdiskptr = fopen(simdiskfn, "r+");  /* Will be first written during scan
										 * and then read during replay of
										 * PROVIDED.
										 */
    if (simdiskptr == NULL)
		printf("Simulate file open failed: %s\n", simdiskfn);
    else
	    printf("Simulate file opened.\n");

	/* Truncate the file in case it already contains something
	 * from previous runs.
	 */
	if (truncate(simdiskfn, 0))
		VOID_ERR("File truncate for %s failed\n", simdiskfn);
}

void simdiskfn_exit()
{
	fprintf(stdout, "In %s\n", __FUNCTION__);
    if (simdiskptr != NULL)
        fclose(simdiskptr);
}

int read_simdisk(__u32 simdisk_blkid, __u8 *simcontent)
{
	long n;
	long offset = (long)simdisk_blkid << 12;

	assert(simcontent != NULL);

	fseek(simdiskptr, offset, SEEK_SET);
	n = fread(simcontent, 1, BLKSIZE, simdiskptr);
	if (n > 0 && n == BLKSIZE)
	{
#if defined(SIMREPLAY_DEBUG_SS_DONE)			
		if (simdisk_blkid == 1529)
		{
			//savemem unsigned char debugkey[HASHLEN+1];
			unsigned char* debugkey = malloc(HASHLEN+1);
		   	getHashKey(simcontent, BLKSIZE, debugkey);
			debugkey[HASHLEN]='\0';
			printf("%s: simdisk_blkid=%u, md5key=%s\n", __FUNCTION__,
				simdisk_blkid, debugkey);
			free(debugkey);	//savemem
		}
#endif
		return 0;
	}
	else
	{
		RET_ERR("error in read_simdisk\n");
	}
}

int get_simcontent(__u32 ioblk, __u8 *simcontent, int generate)
{
	long n;
	long offset = ioblk << 5;
	unsigned char buf[MD5HASHLEN_STR-1];

#if defined(SIMREPLAY_DEBUG_SS_DONE)			
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	assert(simcontent != NULL);
	assert(simdiskptr != NULL);

	fseek(simdiskptr, offset, SEEK_SET);
	n = fread(buf, 1, MD5HASHLEN_STR-1, simdiskptr);
	if (n > 0 && n == MD5HASHLEN_STR-1)
	{
		if (generate)
			generate_BLK_content(simcontent, buf, MD5HASHLEN_STR-1, BLKSIZE);
		else
			memcpy(simcontent, buf, MD5HASHLEN_STR-1);
#if defined(SIMREPLAY_DEBUG_SS_DONE)			
		fprintf(stdout, "buf=%s\n", buf);
		fprintf(stdout, "simcontent=%s\n", simcontent);
#endif
		return 0;
	}
	else
	{
		RET_ERR("failure to read ioblk %u of simulated disk file in %s\n",
			ioblk, __FUNCTION__);
	}
}

int write_simdisk(__u32 simdisk_blkid, __u8 *buf)
{
	long n;
	long offset = (long)simdisk_blkid << 12;

	assert(buf != NULL);
#if defined(SIMREPLAY_DEBUG_SS_DONE)			
		if (simdisk_blkid == 1529)
		{
			//savemem unsigned char debugkey[HASHLEN+1];
			unsigned char* debugkey = malloc(HASHLEN+1);
		   	getHashKey(buf, BLKSIZE, debugkey);
			debugkey[HASHLEN]='\0';
			printf("%s: simdisk_blkid=%u, md5key=%s\n", __FUNCTION__,
				simdisk_blkid, debugkey);
			free(debugkey);	//savemem
		}
#endif

	fseek(simdiskptr, offset, SEEK_SET);
	n = fwrite(buf, 1,  BLKSIZE, simdiskptr);
	if (n > 0 && n == BLKSIZE)
	{
		return 0;
	}
	else
	{
		RET_ERR("failure to write in write_simdisk\n");
	}
}

int append_simcontent(__u32 ioblk, __u8 *buf)
{
	long n;
	long offset = ioblk << 5;

#if defined(SIMREPLAY_DEBUG_SS_DONE)
	char tempbuf[MD5HASHLEN_STR];
	memcpy(tempbuf, buf, MD5HASHLEN_STR-1);
	tempbuf[MD5HASHLEN_STR-1] = '\0';
	assert(buf != NULL);
	fprintf(stdout, "%s:At ioblk=%u, buf=%s\n", __FUNCTION__, ioblk, 
					tempbuf);
#endif

	fseek(simdiskptr, offset, SEEK_SET);
	n = fwrite(buf, 1,  MD5HASHLEN_STR-1, simdiskptr);
	if (n > 0 && n == MD5HASHLEN_STR-1)
	{
		return 0;
	}
	else
	{
		RET_ERR("failure to write to simulated disk file in "
				"append_simcontent\n");
	}
}
