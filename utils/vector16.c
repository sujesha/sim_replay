/* Borrowed from https://gist.github.com/EmilHernvall/953968
 * and modified for 2D vector.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <asm/types.h>
#include "vector16.h"
 
void vector16_init(vector16 *v)
{
	v->data = NULL;
	v->size = 0;
}

void vector16_resize(vector16 *v, __u16 newsize)
{
    __u16 i;
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

/* Return size/capacity of the vector16 */
__u16 vector16_size(vector16 *v)
{
	return v->size;
}

/* vector16_set: set the element at index i */
void vector16_set(vector16 *v, __u16 index, void *e)
{
	int i;
	void *tmp;

	/* If index is out of bounds, grow the vector16 and set as requested */
	if (index >= v->size) {
		v->data = realloc(v->data, sizeof(unsigned char*) * (index + 1));
		for (i=v->size; i<(index+1); i++)
			v->data[i] = NULL;

		v->size = index + 1;
	}

   	if (index > 0 && v->data[index] != e)
	{
		tmp = v->data[index];
		v->data[index] = NULL;
		if (tmp != NULL)
			free(tmp);
	}
	v->data[index] = e;
}
 
/** vector16_get -- get the element at index index
 * 
 * @v[in]: Input vector
 * @index[in]: index of element requested
 * @return: element of vector v at index index
 */
void *vector16_get(vector16 *v, __u16 index)
{
	if (index >= v->size) {
		return NULL;
	}
 
	return v->data[index];
}
 
/* vector16_delete: delete the element at index i */
void vector16_delete(vector16 *v, __u16 index)
{
	if (index >= v->size) {
		fprintf(stderr, "vector16_delete:index %u>=v->size %u\n", 
						index, v->size);
		return;
	}
	printf("1\n");
	if (v->data[index] == NULL) {
		fprintf(stderr, "Nothing to free at %u\n", index);
		return;
	}

	v->data[index] = NULL;
}
 
void vector16_free(vector16 *v)
{
	free(v->data);
}

#if 0
//uncomment this for testing 1D vector16 and "make"
int main(void)
{
	int i;
	char *ptr;
	vector16 v;
	vector16_init(&v);
 
	vector16_set(&v, 0, "emil");
	vector16_set(&v, 1, "hannes");
	vector16_set(&v, 2, "lydia");
	vector16_set(&v, 3, "olle");
	vector16_set(&v, 4, "erik");
	printf("first round:\n");
	for (i = 0; i < vector16_size(&v); i++) {
		printf("%s\n", (char*) vector16_get(&v, i));
	}
 
	vector16_delete(&v, 1);
	vector16_delete(&v, 3);
	printf("second round:\n");
	for (i = 0; i < vector16_size(&v); i++) {
		if ((ptr = (char*) vector16_get(&v, i)) != NULL)
		printf("%s\n", ptr);
	}
 
	vector16_free(&v);
 
	return 0;
}
#endif
