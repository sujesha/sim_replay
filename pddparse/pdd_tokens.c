/* These are retrieval of individual tokens for records by pdatadump.ko
 * If there are any more tokens, add them here in this file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include "common.h"
#include <assert.h>
#include <asm/types.h>
#include <linux/types.h>
#include "trace-struct.h"
#include "debug.h"

extern int as_is_flag;

void getnexttoken(char **ptr, char *sep, char **rest, 
		char **token, __u32 *bytes_leftp);
//char *rest; // to point to the rest of the string after token extraction.
//char *ptr;


//unsigned long long getptime(char **ptr, char **rest, int *bytes_leftp)
void getptime(struct record_info *r, char **ptr, char **rest, 
					__u32 *bytes_leftp)
{
    char *ptimestr;
	char *str, *prevstr;
//    __u64 ptime;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    //getnexttoken(ptr, " ,()", rest, &ptimestr, bytes_leftp);
    getnexttoken(ptr, " ", rest, &ptimestr, bytes_leftp);
#ifdef DEBUG_SS
	fprintf(stdout, "After getnexttoken In %s %s\n", __FUNCTION__, ptimestr);
#endif
	str = strdup(ptimestr);
	prevstr = str;
    sscanf(str, "%llu", &(r->ptime));
#ifdef DEBUG_SS
	fprintf(stdout, "After sscanf In %s, ptime = %llu\n", 
				__FUNCTION__, r->ptime);
#endif

//    return r->ptime;
	if (prevstr)
		free(prevstr);		
//*** glibc detected *** ./merge: corrupted double-linked list: 0x0804e810 ***
}

void gethostname(char **hostname, char **ptr, char **rest, __u32 *bytes_leftp)
{
	char *newtoken = NULL;
#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &newtoken, bytes_leftp);
	*hostname = strdup(newtoken);
}

void getdigstr(char **digstr, char **ptr, char **rest, __u32 *bytes_leftp)
{
	char *newtoken = NULL;
#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &newtoken, bytes_leftp);
	*digstr = strdup(newtoken);
}

void getdata(char **dataorkey, char **ptr, __u32 bytes_left)
{
	*dataorkey = malloc(bytes_left);
#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
	fprintf(stdout, "bytes_left = %d\n", bytes_left);
#endif
	memcpy(*dataorkey, *ptr, bytes_left);

	//below assert will not work with preprocessed files
	//assert((*dataorkey)[bytes_left-1] == '\0');
	if (as_is_flag)
		(*dataorkey)[bytes_left-1] = '\0';
}

void fixdata(char **dataorkey, char *buf, __u32 bytes_done, __u32 len)
{
	*dataorkey = realloc(*dataorkey, bytes_done + len);	
#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	memcpy(*dataorkey+bytes_done, buf, len);
	//assert((*dataorkey)[bytes_done + len - 1] == '\n');
	(*dataorkey)[bytes_done + len - 1] = '\0';
}

int getcontentflag(char **ptr, char **rest, __u32 *bytes_leftp)
{
    char *contentflagstr;
    int contentflag;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &contentflagstr, bytes_leftp);
	if (contentflagstr == NULL)
		RET_ERR("%s: getnexttoken failed\n", __FUNCTION__);

	sscanf(contentflagstr, "%d", &contentflag);
    return contentflag;
}


unsigned int getblockID(char **ptr, char **rest, __u32 *bytes_leftp)
{
    char *blockIDstr;
    unsigned int blockID;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &blockIDstr, bytes_leftp);
    sscanf(blockIDstr, "%u", &blockID);

    return blockID;
}

__u32 getnbytes(char **ptr, char **rest, __u32 *bytes_leftp)
{
    char *nbytesstr;
    __u32 nbytes;

#ifdef DEBUG_SS
    fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &nbytesstr, bytes_leftp);
    sscanf(nbytesstr, "%u", &nbytes);

    return nbytes;
}

void getprocessname(char *processname, char **ptr, char **rest, __u32 *bytes_leftp)
{
	char *newtoken = NULL;
#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &newtoken, bytes_leftp);
	//*processname = (char*)malloc(strlen(newtoken)+1);
	strncpy(processname, newtoken, strlen(newtoken));
	processname[strlen(newtoken)] = '\0';
//			strdup(newtoken);
}

int getpid(char **ptr, char **rest, __u32 *bytes_leftp, struct record_info *r)
{
    char *pidstr;
    int pid, i;
	int foundnonnum = 0;
	char tempname[256] = "";
	//char *tempstr = NULL;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	do
	{
		foundnonnum = 0;

		/* Sometimes kernel module reports processname with space(s) in between.
	 	 * But, we know that the next token after processname is this pid, which
		 * should be numeric in value. If we find that pidstr[0] != [0-9],
		 * we make a simplistic assumption that this token is also a part of
	 	 * the processname itself.
		 */
    	getnexttoken(ptr, " ", rest, &pidstr, bytes_leftp);

		for(i=0; i< (int)strlen(pidstr); i++)
		{
			if (pidstr[i] < '0' || pidstr[i] > '9')
			{
				foundnonnum = 1;
				break;
			}
		}
		if (foundnonnum)
		{
			strcat(tempname, " ");
			strcat(tempname, pidstr);
		}
	}while(foundnonnum == 1);

	if (strcmp(tempname, ""))
	{
		fprintf(stdout, "Adjusting processname\n");
#if 0
		tempstr = strdup(r->processname);
		r->processname = realloc(r->processname, strlen(tempstr) + strlen(tempname) + 1);
		if (r->processname == NULL)
			fprintf(stderr, "couldnt realloc to processname\n");
		else
		{
			strncpy(r->processname, tempstr, strlen(tempstr));
			strncpy(r->processname, tempname, strlen(tempname));
			r->processname[strlen(tempstr) + strlen(tempname)] = '\0';
		}
		free(tempstr);	//work over, free
#endif
		strcat(r->processname, tempname);
	}
    sscanf(pidstr, "%d", &pid);

    return pid;
}

int getmajor(char **ptr, char **rest, __u32 *bytes_leftp)
{
    char *majorstr;
    int major;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &majorstr, bytes_leftp);
    sscanf(majorstr, "%d", &major);

    return major;
}

int getminor(char **ptr, char **rest, __u32 *bytes_leftp)
{
    char *minorstr;
    int minor;

#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    getnexttoken(ptr, " ", rest, &minorstr, bytes_leftp);
    sscanf(minorstr, "%d", &minor);

    return minor;
}
 
