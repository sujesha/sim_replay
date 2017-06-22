/* Borrowed from 
 * https://gist.github.com/EmilHernvall/953968
 */

#ifndef _VECTOR16_H__
#define _VECTOR16_H__
 
typedef struct vector16_t {
	void** data;			/* data pointers */
	__u16 size;				/* Size of the malloc'ed array */
} vector16;
 
void vector16_resize(vector16 *v, __u16 newsize);
void vector16_init(vector16*);
void vector16_set(vector16*, __u16, void*);
__u16 vector16_size(vector16 *v);
void *vector16_get(vector16*, __u16);
void vector16_free(vector16*);
void vector16_delete(vector16 *v, __u16 index);
 
#endif /* _VECTOR16_H__ */
