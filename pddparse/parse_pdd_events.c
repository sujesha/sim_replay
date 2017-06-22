/* Contains parse* functions for all events reported by pdatadump.ko
 * If there are any more events to be parsed, add in this file.
 */

#include <stdio.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include "common.h"
#include "endianness.h"
#include "debug.h"
#include "trace-struct.h"
#include <asm/types.h>
#include <linux/types.h>

extern int data_is_native;
extern FILE *ofp;
extern volatile int done;
extern int as_is_flag;

void getptime(struct record_info *r, char **ptr, char **rest, 
					__u32 *bytes_leftp);
void gethostname(char **hostname, char **ptr, char **rest, __u32 *bytes_leftp);
unsigned long long getblockID(char **ptr, char **rest, __u32 *bytes_leftp);
int getnbytes(char **ptr, char **rest, __u32 *bytes_leftp);
void getprocessname(char *processname, char **ptr, char **rest, __u32 *bytes_leftp);
int getpid(char **ptr, char **rest, __u32 *bytes_leftp, struct record_info*);
int getmajor(char **ptr, char **rest, __u32 *bytes_leftp);
int getminor(char **ptr, char **rest, __u32 *bytes_leftp);
void getdata(char **dataorkey, char **ptr, __u32 bytes_left);
void getdigstr(char **digstr, char **ptr, char **rest, __u32 *bytes_leftp);
int getcontentflag(char **ptr, char **rest, __u32 *bytes_leftp);



void handle_sigint(__attribute__((__unused__)) int sig)
{
    done = 1;
}

#if 0
char* strtok_singlechar(char *string, const char seps, char **context)
{
	char *head;
	char *tail;

	/* If we're starting up, initialize context */
	head = *context;
	if (head == NULL)
		return NULL;

	/* Did we hit the end? */
	if (*head == 0)
	{
		/*Nothing left */
		*context = NULL;
		return NULL;
	}

	/* skip over work */
	tail = head;
	while (*tail & !strchr(seps, 
	char *p = *rest;
#ifdef SCANR_DEBUG_SS
	fprintf(stdout, "sep = '%c'\n", sep);
#endif	
	while(p[0] != sep)
	{
#ifdef SCANR_DEBUG_SS
	fprintf(stdout, "'%c'\n", p[0]);
#endif	
		p++;
	}
	*p = '\0';
	*rest = p + 1;
	return retptr;
}
#endif

void getnexttoken(char **ptr, char *sep, char **rest, 
		char **token, __u32 *bytes_leftp)
{
#if 0
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif
    //*token = strtok_r(*ptr, sep, rest);
    *token = strtok_r(*ptr, sep, rest);
#if 0
	fprintf(stdout, "After strtok_r In %s\n", __FUNCTION__);
#endif
    if (*token == NULL)
        fprintf(stderr, "NULL token retrieved\n");
#if 0
	fprintf(stdout, "Before assignment in %s, token = %s\n", __FUNCTION__, *token);
#endif
	*bytes_leftp -= (*rest - *ptr);
	
    *ptr = *rest;
#if 0
	fprintf(stdout, "After assignment in %s\n", __FUNCTION__);
#endif
}

struct trace_event_element* tee_alloc()
{
    /* SSS: malloc and return */
    return malloc(sizeof(struct trace_event_element));
}

void tee_free(struct trace_event_element *tee)
{
        free(tee);
}

void parseDFR(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1;	/* extra byte for space char */
#ifdef SIMREPLAY_DEBUG_SS_DONE
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&r->hostname, ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);
    getprocessname(r->processname, ptr, rest, &bytes_left);
    r->pid = getpid(ptr, rest, &bytes_left, r);
    r->major = getmajor(ptr, rest, &bytes_left);
    r->minor = getminor(ptr, rest, &bytes_left);
    getdata(&r->dataorkey, ptr, bytes_left); //content is content
//	assert((r->dataorkey)[bytes_left-1] == '\n');
	(r->dataorkey)[bytes_left-1] = '\0';

	r->contentflag = 1;		
	r->digstr = NULL;							//empty
}

/* For read requests that contain the hex hash as well, similar to 
 * collect.ko module output by Koller, note that "content" may have
 * BLKSIZE #bytes content or len-of-hex-hash #bytes content, but
 * currently this modification is being done to support I/O dedup
 * online traces from 
 * http://sylab-srv.cs.fiu.edu/dokuwiki/doku.php?id=projects:iodedup:start
 * so content is the hex representation of actual hash, and digstr left empty.
 * FIXME: if we modify pdatadump itself to output this, then content
 * can be actual "read content" and "digstr" can be its hex hash.
 */
void parseCFR(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1;	/* extra byte for space char */
#if 0
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&r->hostname, ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);
    getprocessname(r->processname, ptr, rest, &bytes_left);
    r->pid = getpid(ptr, rest, &bytes_left, r);
    r->major = getmajor(ptr, rest, &bytes_left);
    r->minor = getminor(ptr, rest, &bytes_left);
    getdata(&r->dataorkey, ptr, bytes_left); //hash is content
//	assert((r->dataorkey)[bytes_left-1] == '\n');
	(r->dataorkey)[bytes_left-1] = '\0';

	r->contentflag = 0;		
	r->digstr = NULL;							//empty
}

void parseOFR(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1;	/* extra byte for space char */
#if 0
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&r->hostname, ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);
    getprocessname(r->processname, ptr, rest, &bytes_left);
    r->pid = getpid(ptr, rest, &bytes_left, r);
    r->major = getmajor(ptr, rest, &bytes_left);
    r->minor = getminor(ptr, rest, &bytes_left);

	r->contentflag = 0;
	r->digstr = NULL;
	r->dataorkey = NULL;
}

int parseCFW(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1;	/* extra byte for space char */
#if 0
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&(r->hostname), ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);
    getdigstr(&r->digstr, ptr, rest, &bytes_left);
    r->contentflag = getcontentflag(ptr, rest, &bytes_left);
	if (r->contentflag < 0)
	{
		RET_ERR("%s: getcontentflag error for blockID = %u\n", 
				__FUNCTION__, r->blockID);
	}
    getprocessname(r->processname, ptr, rest, &bytes_left);
    r->pid = getpid(ptr, rest, &bytes_left, r);
    r->major = getmajor(ptr, rest, &bytes_left);
    r->minor = getminor(ptr, rest, &bytes_left);
	getdata(&r->dataorkey, ptr, bytes_left);
//	assert((r->dataorkey)[bytes_left-1] == '\n');
	(r->dataorkey)[bytes_left-1] = '\0';
	return 0;
}

/* Returns number of bytes read, indicating that more are needed as 
 * part of this same record. However, ret=0 indicates that everything
 * was read, and nothing pending  */
int parseOFW(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1;	/* extra byte for space char */
#ifdef SIMREPLAY_DEBUG_SS_DONE
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&(r->hostname), ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);
    getdigstr(&r->digstr, ptr, rest, &bytes_left);
    r->contentflag = getcontentflag(ptr, rest, &bytes_left);
    getprocessname(r->processname, ptr, rest, &bytes_left);
    r->pid = getpid(ptr, rest, &bytes_left, r);
    r->major = getmajor(ptr, rest, &bytes_left);
    r->minor = getminor(ptr, rest, &bytes_left);
	getdata(&r->dataorkey, ptr, bytes_left);
#if 0
	if (!strcmp(event, "OFNW"))
		getdata(&r->dataorkey, ptr, bytes_left);
	else /* OFDW */
	{
		if (r->contentflag == 1) /* done timestamp replaced/preprocessed */
			getdata(&r->dataorkey, ptr, r->nbytes);
		else	/* not yet replaced/preprocessed */
			getdata(&r->dataorkey, ptr, bytes_left);
	}
#endif

	if (!as_is_flag && r->nbytes == (bytes_left - 1))
	{
		assert((r->dataorkey)[bytes_left-1] == '\0'); //\0 instead of \n
		(r->dataorkey)[bytes_left-1] = '\0';
		return 0;
	}
	if (as_is_flag)
		assert((r->dataorkey)[bytes_left-1] == '\0');

	/* more data to be read for this record */
	return bytes_left;	
}

/* Returns number of bytes read, indicating that more are needed as 
 * part of this same record. However, ret=0 indicates that everything
 * was read, and nothing pending  */
int parseSF(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1;	/* extra byte for space char */
#if 0
	fprintf(stdout, "In %s, ptr = %s\n", __FUNCTION__, *ptr);
	fprintf(stdout, "In %s, getptime() = %llu\n", __FUNCTION__,
		getptime(r, ptr, rest, &bytes_left));
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&r->hostname, ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);
    getdigstr(&r->digstr, ptr, rest, &bytes_left);
    r->contentflag = getcontentflag(ptr, rest, &bytes_left);
    getdata(&r->dataorkey, ptr, bytes_left);
#if 0
    if (!strcmp(event, "SFN"))
    else /* SFD */
    {
        if (r->contentflag == 1) /* done timestamp replaced/preprocessed */
            getdata(&r->dataorkey, ptr, r->nbytes);
        else    /* not yet replaced/preprocessed */
            getdata(&r->dataorkey, ptr, bytes_left);
    }
#endif

	//r->processname = NULL;
	strcpy(r->processname, "");
	r->pid = 0;
	r->major = 0;
	r->minor = 0;

	if (!as_is_flag && r->nbytes == (bytes_left - 1))
	{
		assert((r->dataorkey)[bytes_left-1] == '\0');	//\0 instead of \n
		(r->dataorkey)[bytes_left-1] = '\0';
		return 0;
	}
	if (as_is_flag)
		assert((r->dataorkey)[bytes_left-1] == '\0');

	/* more data to be read for this record */
	return bytes_left;	
}

void parseSZ(char *event, __u32 elt_len, char **ptr, char **rest, 
			struct record_info *r)
{
	__u32 bytes_left = elt_len - strlen(event) - 1; /* extra byte for space char */
#if 0
	fprintf(stdout, "In %s\n", __FUNCTION__);
#endif

    getptime(r, ptr, rest, &bytes_left);
    gethostname(&r->hostname, ptr, rest, &bytes_left);
    r->blockID = getblockID(ptr, rest, &bytes_left);
    r->nbytes = getnbytes(ptr, rest, &bytes_left);

	r->contentflag = 0;
	r->dataorkey = NULL;
	//r->processname = NULL;
	strcpy(r->processname, "");
	r->digstr = NULL;
}

void pdd_trace_to_cpu(struct trace_event_element *t)
{
    if (data_is_native)
        return;

    t->magic    = be32_to_cpu(t->magic);
    t->elt_len  = be32_to_cpu(t->elt_len);
}

