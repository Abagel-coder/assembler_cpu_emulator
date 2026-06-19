#ifndef CACHE_H
#define CACHE_H

#include <stdint.h>

typedef enum { REPL_LRU, REPL_FIFO, REPL_RANDOM } ReplPolicy;

typedef struct {
    int        enabled;
    int        size_bytes;
    int        block_bytes;
    int        assoc;
    ReplPolicy repl;
    int        hit_latency;
    int        miss_penalty;
} CacheConfig;

typedef struct {
    CacheConfig cfg;
    int       nsets;
    uint8_t  *valid;
    uint32_t *tag;
    uint64_t *order;     /* LRU timestamp / FIFO insert order */
    uint64_t  clock;
    uint64_t  accesses;
    uint64_t  misses;
} Cache;

void   cache_init(Cache *c, CacheConfig cfg);
void   cache_free(Cache *c);
int    cache_access(Cache *c, uint32_t addr);   /* 1 = hit, 0 = miss */
double cache_amat(const Cache *c);

#endif /* CACHE_H */
