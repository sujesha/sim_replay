/* Borrowed from 
 * https://gist.github.com/EmilHernvall/953968
 */

#ifndef _VECTOR64_H__
#define _VECTOR64_H__
 
typedef struct vector64_t {
	void** data;			/* data pointers */
	__u64 size;				/* Size of the malloc'ed array */
} vector64;
 
void vector64_resize(vector64 *v, __u64 newsize);
void vector64_init(vector64*);
void vector64_set(vector64 *v, __u64 index, void *e);
__u64 vector64_size(vector64 *v);
void *vector64_get(vector64 *v, __u64 index);
void vector64_free(vector64*);
void vector64_delete(vector64 *v, __u64 index);
 
#endif /* _VECTOR64_H__ */
