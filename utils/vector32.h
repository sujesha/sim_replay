/* Borrowed from 
 * https://gist.github.com/EmilHernvall/953968
 */

#ifndef _VECTOR32_H__
#define _VECTOR32_H__
 
typedef struct vector32_t {
	void** data;			/* data pointers */
	__u32 size;				/* Size of the malloc'ed array */
} vector32;
 
void vector32_resize(vector32 *v, __u32 newsize);
void vector32_init(vector32*);
void vector32_set(vector32 *v, __u32 index, void *e);
__u32 vector32_size(vector32 *v);
void *vector32_get(vector32 *v, __u32 index);
void vector32_free(vector32*);
void vector32_delete(vector32 *v, __u32 index);
 
#endif /* _VECTOR32_H__ */
