#ifndef _LRU_CACHE_H_
#define _LRU_CACHE_H_

//void add_to_cache(char *key, char *value);
void add_to_cache(char *key, char *value, int len, int updatehits,
		char *newleaderkey, int *bcachefoundout);
char* find_in_cache(char *key);
void free_lru_cache();
void print_lrucache_stat(void);

#endif /* _LRU_CACHE_H_ */
