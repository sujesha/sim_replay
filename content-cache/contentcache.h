#ifndef _CONTENTCACHE_H_
#define _CONTENTCACHE_H_

#include "serveio-utils.h"
#include "pdd_config.h"
#include "md5.h"
#include "arc.h"

/* This is the object we're managing. It has a name (md5/sha1)
 * and some data. This data will be loaded when ARC instruct
 * us to do so. */
struct object {
    unsigned char dhashkey[HASHLEN];
    struct __arc_object entry;
    
#if 1   
    __u32 ioblkID;  //to count dedup hits in content-cache only
#endif  
    void *content; 
};  
//struct moved here from contentcache.c

void contentcache_init(void);
#if 1	
void contentcache_add(__u8* dhashkey, __u8* content, unsigned int len,__u32 ioblk, int updatehits);
__u8* contentcache_lookup(__u8* dhashkey, unsigned int len, __u32 iodedupID,
		__u32 d2pv_ioblkID);
#else
void contentcache_add(__u8* dhashkey, __u8* content, unsigned int len);
__u8* contentcache_lookup(__u8* dhashkey, unsigned int len);
#endif
int overwrite_in_contentcache(struct preq_spec *preql, int updatehits);
void contentcache_exit();

#endif /* _CONTENTCACHE_H_ */
