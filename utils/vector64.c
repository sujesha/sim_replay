/* Borrowed from 
 * https://gist.github.com/EmilHernvall/953968
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/types.h>
#include "vector64.h"
 
void vector64_init(vector64 *v)
{
	v->data = NULL;
	v->size = 0;
}

void vector64_resize(vector64 *v, __u64 newsize)
{
	__u64 i;
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

/* Return size/capacity of the vector64 */
__u64 vector64_size(vector64 *v)
{
	return v->size;
}

/* vector64_set: set the element at index i */
void vector64_set(vector64 *v, __u64 index, void *e)
{
	__u32 i;

	/* If index is out of bounds, grow the vector64 and set as requested */
	if (index >= v->size) {
		v->data = realloc(v->data, sizeof(void*) * (index + 1));
		for (i=v->size; i<(index+1); i++)
			v->data[i] = NULL;

		v->size = index + 1;
	}

   	if (v->data[index] != NULL && v->data[index] != e)
		free(v->data[index]);
	v->data[index] = e;
}
 
/* vector64_get: get the element at index i */
void *vector64_get(vector64 *v, __u64 index)
{
	if (index >= v->size) {
		//fprintf(stderr, "element doesnt exist at index %llu\n", index);
		return NULL;
	}
 
	return v->data[index];
}
 
/* vector64_delete: delete the element at index i */
void vector64_delete(vector64 *v, __u64 index)
{
	if (index >= v->size) {
		fprintf(stderr, "vector64_delete:index %llu>=v->size %llu\n", 
						index, v->size);
		return;
	}
	printf("1\n");
	if (v->data[index] == NULL) {
		fprintf(stderr, "Nothing to free at %llu\n", index);
		return;
	}

	free(v->data[index]);
	v->data[index] = NULL;
}
 
void vector64_free(vector64 *v)
{
	free(v->data);
}

#if 0
//uncomment this for testing 1D vector64 and "make"
int main(void)
{
	int i;
	char *ptr;
	vector64 v;
	vector64_init(&v);
 
	vector64_set(&v, 0, "emil");
	vector64_set(&v, 1, "hannes");
	vector64_set(&v, 2, "lydia");
	vector64_set(&v, 3, "olle");
	vector64_set(&v, 4, "erik");
	printf("first round:\n");
	for (i = 0; i < vector64_size(&v); i++) {
		printf("%s\n", (char*) vector64_get(&v, i));
	}
 
	vector64_delete(&v, 1);
	vector64_delete(&v, 3);
	printf("second round:\n");
	for (i = 0; i < vector64_size(&v); i++) {
		if ((ptr = (char*) vector64_get(&v, i)) != NULL)
		printf("%s\n", ptr);
	}
 
	vector64_free(&v);
 
	return 0;
}
#endif
