#include "cache.h"

#include <stdlib.h>
#include <string.h>

void cache_init(Cache *c, CacheConfig cfg) {
    memset(c, 0, sizeof *c);
    c->cfg = cfg;
    c->nsets = cfg.size_bytes / (cfg.block_bytes * cfg.assoc);

    size_t lines = (size_t)c->nsets * cfg.assoc;
    c->valid = calloc(lines, 1);
    c->tag   = calloc(lines, sizeof(uint32_t));
    c->order = calloc(lines, sizeof(uint64_t));
}

void cache_free(Cache *c) {
    free(c->valid);
    free(c->tag);
    free(c->order);
}

static int pick_victim(Cache *c, int set_base) {
    int assoc = c->cfg.assoc;

    for (int w = 0; w < assoc; w++) {
        if (!c->valid[set_base + w]) return w;   /* empty way first */
    }
    if (c->cfg.repl == REPL_RANDOM) return rand() % assoc;

    /* LRU and FIFO both evict the smallest order value */
    int victim = 0;
    uint64_t best = c->order[set_base];
    for (int w = 1; w < assoc; w++) {
        if (c->order[set_base + w] < best) {
            best = c->order[set_base + w];
            victim = w;
        }
    }
    return victim;
}

int cache_access(Cache *c, uint32_t addr) {
    c->accesses++;

    uint32_t block = addr / (uint32_t)c->cfg.block_bytes;
    int set = (int)(block % (uint32_t)c->nsets);
    uint32_t tag = block / (uint32_t)c->nsets;
    int base = set * c->cfg.assoc;

    for (int w = 0; w < c->cfg.assoc; w++) {
        if (c->valid[base + w] && c->tag[base + w] == tag) {
            if (c->cfg.repl == REPL_LRU) c->order[base + w] = ++c->clock;
            return 1;
        }
    }

    c->misses++;
    int v = pick_victim(c, base);
    c->valid[base + v] = 1;
    c->tag[base + v] = tag;
    c->order[base + v] = ++c->clock;   /* insert order; LRU also bumps on hit */
    return 0;
}

double cache_amat(const Cache *c) {
    if (c->accesses == 0) return (double)c->cfg.hit_latency;
    double miss_rate = (double)c->misses / (double)c->accesses;
    return (double)c->cfg.hit_latency + miss_rate * (double)c->cfg.miss_penalty;
}
