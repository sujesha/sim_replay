/* Borrowed from 
 * https://gist.github.com/EmilHernvall/953968
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/types.h>
#include "vector32.h"
#include "debug.h"

extern int warmupflag;

#ifdef SIMREPLAY_DEBUG_SS 
extern FILE * ftimeptr;
#include <time.h>
inline __u64 gettime(void);
#endif

void vector32_init(vector32 *v)
{
	v->data = NULL;
	v->size = 0;
}

void vector32_resize(vector32 *v, __u32 newsize)
{
	__u32 i;
	if (newsize > v->size) {
		v->data = realloc(v->data, sizeof(void*) * newsize);
		for (i=v->size; i<newsize; i++)
			v->data[i] = NULL;

		v->size = newsize;
	}
	else
	{
		v->data = realloc(v->data, sizeof(void*) * newsize);
		v->size = newsize;
	}
}

/* Return size/capacity of the vector32 */
__u32 vector32_size(vector32 *v)
{
	return v->size;
}

/* vector32_set: set the element at index i */
//void vector32_set(vector32 *v, __u32 index, void *e, unsigned char doubleit)
void vector32_set(vector32 *v, __u32 index, void *e)
{
	__u32 i;
#ifdef SIMREPLAY_DEBUG_SS 
	if (index==32791714)
		fprintf(stdout, "%s: index=%u\n", __FUNCTION__, index);
	unsigned long long stime=0, etime=0;

	stime = gettime();	/* START vector32_set realloc time */
#endif
	/* If index is out of bounds, grow the vector32 and set as requested */
	if (index >= v->size) {
		v->data = realloc(v->data, sizeof(void*) * (index + 1)*2);
		if (v->data == NULL)
			VOID_ERR("vector32_set ran out of memory \n");
			
		fprintf(stdout, "%s: reallocsize=%ld when index=%u\n", __FUNCTION__, 
				sizeof(void*) * (index + 1)*2, index);
		for (i=v->size; i<(index+1)*2; i++)
			v->data[i] = NULL;

		v->size = (index + 1)*2;
		fprintf(stdout, "%s: v->size=%u\n", __FUNCTION__, 
				v->size);
	}
#ifdef SIMREPLAY_DEBUG_SS 
	etime = gettime();	/* END vector32_set realloc time */
	ACCESSTIME_PRINT("iodedmap-map-updateblk-component-vector32_set time: %llu\n", etime - stime);
#endif

	//Don't do free of old elel
//   	if (v->data[index] != NULL && v->data[index] != e)
//		free(v->data[index]);
	v->data[index] = e;
}
 
/** vector32_get -- get the element at index index
 * 
 * @v[in]: Input vector
 * @index[in]: index of element requested
 * @return: element of vector v at index index
 */
void *vector32_get(vector32 *v, __u32 index)
{
	if (index >= v->size) {
		return NULL;
	}
 
	return v->data[index];
}
 
/* vector32_delete: delete the element at index i */
void vector32_delete(vector32 *v, __u32 index)
{
	if (index >= v->size) {
		fprintf(stderr, "vector32_delete:index %u>=v->size %u\n", 
						index, v->size);
		return;
	}
	printf("1\n");
	if (v->data[index] == NULL) {
		fprintf(stderr, "Nothing to free at %u\n", index);
		return;
	}

	free(v->data[index]);
	v->data[index] = NULL;
}
 
void vector32_free(vector32 *v)
{
	free(v->data);
}

#if 0
//uncomment this for testing 1D vector32 and "make"
int main(void)
{
	int i;
	char *ptr;
	vector32 v;
	vector32_init(&v);
 
	vector32_set(&v, 0, "emil");
	vector32_set(&v, 1, "hannes");
	vector32_set(&v, 2, "lydia");
	vector32_set(&v, 3, "olle");
	vector32_set(&v, 4, "erik");
	printf("first round:\n");
	for (i = 0; i < vector32_size(&v); i++) {
		printf("%s\n", (char*) vector32_get(&v, i));
	}
 
	vector32_delete(&v, 1);
	vector32_delete(&v, 3);
	printf("second round:\n");
	for (i = 0; i < vector32_size(&v); i++) {
		if ((ptr = (char*) vector32_get(&v, i)) != NULL)
		printf("%s\n", ptr);
	}
 
	vector32_free(&v);
 
	return 0;
}
#endif
