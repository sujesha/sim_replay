#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>          /* FILE */
#include <malloc.h>
#include <string.h>
#include <locale.h>
#include <libgen.h>
#include <errno.h>
#include <byteswap.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <assert.h>
#include "parse-generic.h"
#include "endianness.h"
#include "debug.h"
#include "common.h"
#include "trace-struct.h"
#ifndef TEST_PDDPARSE
	#include "utils.h"
#endif


volatile int done = 0;
FILE *ofp = NULL;
int ifd;
char *output_name = NULL;
char *input_dir = NULL;

int as_is_flag = 0;
int count;
extern int data_is_native;
void fixdata(char **dataorkey, char *buf, __u32 bytes_done, __u32 len);


struct trace_event_element* tee_alloc(void);
void tee_free(struct trace_event_element *tee);
int parseSF(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
void parseSZ(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
void parseOFR(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
void parseCFR(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
void parseDFR(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
int parseOFW(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
int parseCFW(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r);
char* strtok_singlechar(char *ptr, char sep, char **rest);

/* Returns the number of bytes read */
int read_data(int fd, void *buffer, int bytes)
{
    int ret, bytes_left;
    void *p;

    /* here, block == *fdblock */
    bytes_left = bytes;
    p = buffer;
    while (bytes_left > 0)
    {
        /* Read bytes_left from fd into p */
        ret = read(fd, p, bytes_left);
        if (!ret)
		{
			fprintf(stderr, "Nothing read\n");
            return FILE_EOF;
		}
        else if (ret < 0)
        {
            /*
             * never do partial reads. we can return if we
             * didn't read anything and we should not block,
             * otherwise wait for data
             */
            if (bytes_left == bytes)
			{
				fprintf(stderr, "Nothing read with ret < 0\n");
                return -1;
			}
#if defined(SCANR_DEBUG_SS) || defined(DIRECTRECREATE_DEBUG_SS)
			fprintf(stderr, "read nothing, so sleep some, try again\n");
#endif			
            /* sleep 10us and try to read again */
            usleep(10);
            continue;
        }
        else
        {
#if defined(DEBUG_SS) || defined(SCANR_DEBUG_SS) || defined(DIRECTRECREATE_DEBUG_SS)
			fprintf(stderr, "some bytes %d were read\n", ret);
#endif
            /* some bytes were read, try to read again if remaining */
            p += ret;
            bytes_left -= ret;
        }
    }

	assert(bytes_left == 0);

    /* finished reading bytes_left */
#if defined(DEBUG_SS) || defined(SCANR_DEBUG_SS) || defined(DIRECTRECREATE_DEBUG_SS)
	fprintf(stdout, "finished reading bytes_left = %d\n", (int)(p - buffer));
#endif
    return (int)(p-buffer);
}

/* Interpret the event and parse it accordingly, since different events
 * have different record formats.
 * Return 0 for success & +ve value to show that more data to be
 * read as part of same record --- due to preprocessing!
 */
int interpret_event(char *buf, __u32 elt_len, struct record_info *r)
{
    char *rest; // to point to the rest of the string after token extraction.
    char *token; // to point to the actual token returned.
    char *ptr = buf;
	int ret = 0;
#if defined(DEBUG_SS) || defined(SCANR_DEBUG_SS)
	fprintf(stdout, "In %s, elt_len = %u, ptr = ", 
						__FUNCTION__, elt_len);
	puts(ptr);
#endif

//    token = strtok_r(ptr, " ", &rest);
	token = strtok_r(ptr, " ", &rest);
    ptr = rest;

	r->elt_len = elt_len;
	if (!strcmp(token, "SFN") || !strcmp(token, "SFD"))
	{
		r->event = strdup(token);
		ret = parseSF(token, elt_len, &ptr, &rest, r);
	}
    else if (!strcmp(token, "SZ"))
	{
		r->event = strdup(token);
        parseSZ(token, elt_len, &ptr, &rest, r);
	}
    else if (!strcmp(token, "DFR"))	//read trace with content
	{
		r->event = strdup(token);
        parseDFR(token, elt_len, &ptr, &rest, r);
	}
    else if (!strcmp(token, "CFR"))	//read trace with hash like collect.ko
	{
		r->event = strdup(token);
        parseCFR(token, elt_len, &ptr, &rest, r);
	}
    else if (!strcmp(token, "OFR"))	//read trace without hash
	{
		r->event = strdup(token);
        parseOFR(token, elt_len, &ptr, &rest, r);
	}
    else if (!strcmp(token, "CFNW"))
	{
		r->event = strdup(token);
        ret = parseCFW(token, elt_len, &ptr, &rest, r);
	}
    else if (!strcmp(token, "OFNW") || !strcmp(token, "OFDW"))
	{
		r->event = strdup(token);
        ret = parseOFW(token, elt_len, &ptr, &rest, r);
	}
    else
        RET_ERR("Unknown event %s\n", token);

	if (as_is_flag == 0)
	{
		/* Postive ret suggests more data to be read for this record */
    	return ret;
	}
	else
		return 0;
}

void fix_event(char *buf, __u32 bytes_done, struct record_info *r)
{
#ifdef SCANR_DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
	__u32 data_len = r->nbytes - bytes_done + 1;
	fixdata(&r->dataorkey, buf, bytes_done, data_len);
}

inline __u32 get_len(struct trace_event_element *tee)
{
    if (data_is_native)
        return tee->elt_len;

    return __bswap_32(tee->elt_len);
}

inline __u32 get_magic(struct trace_event_element *tee)
{
    if (data_is_native)
        return tee->magic;

    return __bswap_32(tee->magic);
}

/* We need to dump the record in exactly the same way that pdatadump does */
int record_dump(struct record_info *r, int processed)
{
	__u32 size = 0, elt_len;
	struct trace_event_element *t;
	int contentflag = 0;
	unsigned char relaybuf[PRINT_HEADER+10] = {};
	char newline[2];
	newline[0] = '\n';
	newline[1] = '\0';

#ifdef PDDMERGE_DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif


	t = (struct trace_event_element*) relaybuf;
	t->magic = (__u32) ENDIAN_MAGIC | ENDIAN_VERSION;

    if (!strcmp(r->event, "SFN"))
    {
		contentflag = 1;
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %d ",
					"%s %lld %s %u %u %s %d ",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag
                    );
    }
	else if (!strcmp(r->event, "SFD"))
    {
		if (processed)
		{
			strcpy(r->event, "SFN");
			r->contentflag = 1;
		}
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %d ", //no \n
					"%s %lld %s %u %u %s %d ", //no \n
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag
                    );
    }
    else if (!strcmp(r->event, "SZ"))
    {
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s",
					"%s %lld %s %u %u %s",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
					"zero"
                    );
    }
    else if (!strcmp(r->event, "DFR"))	//with content
    {
		contentflag = 1;
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %u %u %u",
					"%s %lld %s %u %u %s %u %u %u ",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
    else if (!strcmp(r->event, "CFR"))	//with hash
    {
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %u %u %u",
					"%s %lld %s %u %u %s %u %u %u ",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
    else if (!strcmp(r->event, "OFR"))	//without hash
    {
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %u %u %u",
					"%s %lld %s %u %u %s %u %u %u",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
    else if (!strcmp(r->event, "CFNW"))
    {
		contentflag = 0;
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %d %s %u %u %u ",
					"%s %lld %s %u %u %s %d %s %u %u %u ",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
    else if (!strcmp(r->event, "OFNW"))
    {
		contentflag = 1;
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %d %s %u %u %u ",
					"%s %lld %s %u %u %s %d %s %u %u %u ",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
    else if (!strcmp(r->event, "OFDW"))
    {
		if (processed)
		{
			strcpy(r->event, "OFNW");
			r->contentflag = 1;
		}
        size = snprintf((char*)(relaybuf + sizeof(struct trace_event_element)),
					PRINT_HEADER,
					//"%s %lld %s " PRI_SECT " %u %s %d %s %u %u %u ", //no \n
					"%s %lld %s %u %u %s %d %s %u %u %u ", //no \n
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    (__u32)r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
	else
	{
		fprintf(stderr, "%s event unexpected\n", r->event);
		return -1;
	}

	assert(size > 0);
	if (size >= PRINT_HEADER)
	{
		VOID_ERR("buf truncated to PRINT_HEADER, instead of %u\n", size);
		return -2;
	}

	if (processed)
	{
    	if (contentflag)
        	elt_len = size + r->nbytes + 1;
	    else if (r->dataorkey)
	        elt_len = size + r->nbytes + 1;
		else
	        elt_len = size + 1;
	}
	else
	{
    	if (contentflag)
        	elt_len = size + r->nbytes + 1;
	    else if (r->dataorkey)
	        elt_len = size + strlen(r->dataorkey) + 1;
		else
	        elt_len = size + 1;
	}

	if (!processed && elt_len != r->elt_len)
	{
#ifndef FIX_INCONSISTENT
#ifndef CREATEPDDFROMC
		fprintf(stdout, "%s: elt_len calculated %d != obtained %d\n", 
			r->event, elt_len, r->elt_len);
#endif
#endif
		//return -3;
	}
	if (processed && elt_len != r->elt_len && r->dataorkey &&
			r->elt_len != (elt_len - r->nbytes + (r->elt_len - size - 1)))
	{
		fprintf(stdout, "%s: processed record length %u doesnt match %u\n", 
			r->event, (elt_len - r->nbytes + (r->elt_len - size - 1)), 
			r->elt_len);
		return -4;
	}
	if (processed)
		t->elt_len = elt_len;
	else if (strlen(r->dataorkey) == 32)
		t->elt_len = elt_len;
	else
		t->elt_len = r->elt_len;

	fwrite(relaybuf, sizeof(struct trace_event_element)+size, 1, ofp);

	if (contentflag)
		fwrite(r->dataorkey, sizeof(char), r->nbytes, ofp);
	else if (!processed && r->dataorkey)
		fwrite(r->dataorkey, sizeof(char), strlen(r->dataorkey), ofp);
	else if (!strcmp(r->event, "CFR") && r->dataorkey)
		fwrite(r->dataorkey, sizeof(char), strlen(r->dataorkey), ofp);
	else if (!strcmp(r->event, "CFNW") && r->dataorkey)
		fwrite(r->dataorkey, sizeof(char), strlen(r->dataorkey), ofp);
	else if (r->dataorkey)
		fwrite(r->dataorkey, sizeof(char), r->nbytes, ofp);

	fwrite(newline, sizeof(char), 1, ofp);	/* new line per entry */

#if 0
	if (t->elt_len != r->elt_len)
		return -3;
	if (processed && elt_len != r->elt_len && r->dataorkey &&
			r->elt_len != (elt_len - r->nbytes + (r->elt_len - size - 1)))
		return -4;
#endif

	return 0;
}

void record_print(struct record_info *r)
{
#ifdef DEBUG_SS
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    if (!strcmp(r->event, "SFN") || !strcmp(r->event, "SFD"))
    {
        fprintf(ofp, "%s %lld %s %u %u %s %d %s\n",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag,
                    r->dataorkey
                    );
    }
    else if (!strcmp(r->event, "SZ"))
    {
        fprintf(ofp, "%s %lld %s %u %u %s\n",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    r->blockID,
					r->nbytes,
					"zero"
                    );
    }
    else if (!strcmp(r->event, "CFR"))
    {
        fprintf(ofp, "%s %lld %s %u %u %s %u %u %u %s\n",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    r->blockID,
					r->nbytes,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor,
					r->dataorkey
                    );
    }
    else if (!strcmp(r->event, "OFR"))
    {
        fprintf(ofp, "%s %lld %s %u %u %s %u %u %u\n",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    r->blockID,
					r->nbytes,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor
                    );
    }
    else if (!strcmp(r->event, "OFNW") || !strcmp(r->event, "OFDW"))
    {
        fprintf(ofp, "%s %lld %s %u %u %s %d %s %u %u %u %s\n",
                    r->event,
                    r->ptime,
                    r->hostname, /* distinguish different VMs */
                    r->blockID,
					r->nbytes,
                    r->digstr,
                    r->contentflag,
                    r->processname,
                    r->pid,
                    r->major,
                    r->minor,
                    r->dataorkey
                    );
    }
	else
	{
		fprintf(stderr, "%s event unexpected\n", r->event);
	}
}

void rinternal_free(struct record_info *rec, int num)
{
	struct record_info *r;
	int i;
	for(i=0; i<num; i++)
	{
		r = rec + i;
    	if (r->digstr)
	    {   
#ifdef DEBUG_SS
	        fprintf(stderr, "Freeing r->digstr %s\n", r->digstr); 
#endif      
	        free(r->digstr);
	    }   
	    if (r->dataorkey)
	    {
#ifdef DEBUG_SS
    	    fprintf(stderr, "Freeing r->dataorkey %s\n", r->dataorkey);
#endif
	        free(r->dataorkey);
	    }
    	if (r->event)
	    {
#ifdef DEBUG_SS
	        fprintf(stderr, "Freeing r->event %s\n", r->event);
#endif
	        free(r->event);
	    }
	    if (r->hostname)
	    {
#ifdef DEBUG_SS
    	    fprintf(stderr, "Freeing r->hostname %s\n", r->hostname);
#endif
	        free(r->hostname);
	    }
	}
}

void rfree(struct record_info *r)
{
	if (r->event)
	{
#ifdef DEBUG_SS
		fprintf(stderr, "Freeing r->event %s\n", r->event);
#endif
    	free(r->event);
	}
	if (r->hostname)
	{
#ifdef DEBUG_SS
		fprintf(stderr, "Freeing r->hostname %s\n", r->hostname);
#endif
	    free(r->hostname);
	}
	if (r->digstr)
	{
#ifdef DEBUG_SS
		fprintf(stderr, "Freeing r->digstr %s\n", r->digstr);
#endif
	    free(r->digstr);
	}
#if 0
	if (r->processname)
	{
#ifdef DEBUG_SS
		fprintf(stderr, "Freeing r->processname%s\n", r->processname);
#endif
	    free(r->processname);
	}
#endif
	if (r->dataorkey)
	{
#ifdef DEBUG_SS
		fprintf(stderr, "Freeing r->dataorkey %s\n", r->dataorkey);
#endif
	    free(r->dataorkey);
	}
	if (r)
	{
#ifdef DEBUG_SS
		fprintf(stderr, "Freeing r\n");
#endif
	    free(r);
	}
}

/* Returns 0 upon success */
int next_record(int ifd, struct record_info **record)
{
    char *buf;
    __u32 elt_len;
    __u32 magic;
	int ret = 0;
    struct trace_event_element *t;
	struct record_info *r = *record;

	assert(r != NULL);
	t = (struct trace_event_element *) tee_alloc();

	ret = read_data(ifd, t, sizeof(struct trace_event_element));
	if (ret == FILE_EOF)
	{
		tee_free(t);
		fprintf(stdout, "Reached end-of-input\n");
		return ret;
	}
	if (ret != sizeof(struct trace_event_element))
	{
		tee_free(t);
		fprintf(stderr, "Error in first read_data\n");
		return -1;
	}
	
	if (data_is_native == -1 && check_data_endianness(t))
	{
		fprintf(stderr, "Endianness test failed\n");
		tee_free(t);
		return -1;
	}
#ifdef DEBUG_SS
	fprintf(stderr, "Endianness test passed \n");
#endif
	magic = get_magic(t);
	if ((magic & 0xffffff00) != ENDIAN_MAGIC)
	{
		/* Check whether we have reached the entry which represents the
		 * number of bytes in the file --- this is output from the kernel
		 * module.
		 */
		char *tempstr;
		long val;
		size_t n;
		long tempoff=lseek(ifd, -sizeof(struct trace_event_element), SEEK_CUR);
		FILE *ifp = fdopen(ifd, "r");
		tee_free(t);
		ret = getline(&tempstr, &n, ifp);
		if (ret < 0)
			return -1;
		val = atol(tempstr);
		if (val != tempoff)
			fprintf(stderr, "val(%ld) not equal to tempoff(%ld), tempstr=%s\n", 
					val, tempoff, tempstr);
		else
			return FILE_EOF;

		/* If we reached here, we indeed encountered some error */
		fprintf(stderr, "Bad magic %x\n", magic);
		//tee_free(t);
		return -1;
	
	}
	if (verify_trace(t->magic))
	{
		fprintf(stderr, "verify_trace() failed\n");
		ret = -1;
		tee_free(t);
		return -1;
	}
	
	elt_len = get_len(t);
#ifdef SCANR_DEBUG_SS
	fprintf(stdout, "elt_len = %u\n", elt_len);
#endif
	if (elt_len)
	{
		buf = (char *)malloc(elt_len);
		ret = read_data(ifd, buf, elt_len);
		buf[elt_len-1] = '\0';	//replacing \n with \0
		if (ret != (int)elt_len)
		{
			fprintf(stderr, "Error in second read_data\n");
			tee_free(t);
			free(buf);
			t = NULL;
			return -1;
		}
#ifdef SCANR_DEBUG_SS
		fprintf(stdout, "second read_data done elt_len = %d\n", elt_len);
#endif
		ret = interpret_event(buf, elt_len, r);
		if (ret)
		{
			if (ret > 0)	/* only OFDW and SFD will reach */
			{
				__u32 bytes_done = ret;
				if (r->contentflag != 1)
				{
					fprintf(stderr, "PLEASE PREPROCESS FIRST!! EXITING.\n");
		            tee_free(t);
		            free(buf);
		            t = NULL;
					return -1;
				}
				free(buf);
				buf = (char *)malloc(r->nbytes - bytes_done + 1);
#if defined(DEBUG_SS) || defined(SCANR_DEBUG_SS)
				fprintf(stdout, "r->nbytes = %u,", r->nbytes);
				fprintf(stdout, " bytes_done = %u\n", bytes_done);
#endif
				ret = read_data(ifd, buf, (r->nbytes - bytes_done + 1));
			    if (ret != (int)(r->nbytes - bytes_done + 1))
		        {
            		fprintf(stderr, "Error in third read_data\n");
		            tee_free(t);
		            free(buf);
		            t = NULL;
		            return -1;
        		}
#ifdef SCANR_DEBUG_SS
				fprintf(stdout, "third read_data done = %d, buf = ", 
						ret);
				puts(buf);
#endif
				fix_event(buf, bytes_done, r);
			}
			else
			{
				fprintf(stderr, "error in interpret_event\n");
				tee_free(t);
				free(buf);
				t = NULL;
				return -1;
			}
		}
		if (r->nbytes == 0)
			fprintf(stdout, "record# %u fail with blkID=%u, ts=%llu => skip\n", 
							count, r->blockID, r->ptime);
		free(buf);
	}
	tee_free(t);
	
	return 0;
}
