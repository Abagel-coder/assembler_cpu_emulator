#include "bpred.h"

#include <stdlib.h>
#include <string.h>

static uint32_t pc_index(uint32_t pc) { return pc >> 2; }

static void sat_update(uint8_t *c, int taken) {
    if (taken) { if (*c < 3) (*c)++; }
    else       { if (*c > 0) (*c)--; }
}

void bp_init(BPredictor *bp, BpKind kind, int idx_bits, int ghist_bits) {
    memset(bp, 0, sizeof *bp);
    bp->kind = kind;
    bp->idx_bits = idx_bits;
    bp->mask = (1u << idx_bits) - 1u;
    bp->ghist_bits = ghist_bits;

    size_t entries = (size_t)1 << idx_bits;
    bp->table = malloc(entries);
    /* 2-bit counters start weakly taken (2); 1-bit start not-taken (0) */
    uint8_t init = (kind == BP_BIMODAL1 || kind == BP_STATIC_NT || kind == BP_NONE)
                   ? 0 : 2;
    memset(bp->table, init, entries);

    if (kind == BP_TOURNAMENT) {
        bp->gtable = malloc(entries);
        memset(bp->gtable, 2, entries);
        bp->chooser = malloc(entries);
        memset(bp->chooser, 1, entries);   /* weakly prefer the bimodal half */
    }

    bp->btb_bits = idx_bits;
    bp->btb_mask = bp->mask;
    bp->btb_tag    = calloc(entries, sizeof(uint32_t));
    bp->btb_target = calloc(entries, sizeof(uint32_t));
    bp->btb_valid  = calloc(entries, 1);
}

void bp_free(BPredictor *bp) {
    free(bp->table);
    free(bp->gtable);
    free(bp->chooser);
    free(bp->btb_tag);
    free(bp->btb_target);
    free(bp->btb_valid);
}

static uint32_t ghist_val(const BPredictor *bp) {
    return bp->ghist & ((1u << bp->ghist_bits) - 1u);
}

static uint32_t bimodal_idx(const BPredictor *bp, uint32_t pc) {
    return pc_index(pc) & bp->mask;
}

static uint32_t gshare_idx(const BPredictor *bp, uint32_t pc) {
    return (pc_index(pc) ^ ghist_val(bp)) & bp->mask;
}

static int bimodal_pred(const BPredictor *bp, uint32_t pc) {
    return bp->table[bimodal_idx(bp, pc)] >= 2;
}

static int gshare_pred(const BPredictor *bp, uint32_t pc) {
    return bp->gtable[gshare_idx(bp, pc)] >= 2;
}

static int tournament_use_gshare(const BPredictor *bp) {
    return bp->chooser[ghist_val(bp) & bp->mask] >= 2;
}

int bp_predict(const BPredictor *bp, uint32_t pc) {
    switch (bp->kind) {
        case BP_STATIC_NT:
        case BP_NONE:
            return 0;
        case BP_BIMODAL1:
            return bp->table[bimodal_idx(bp, pc)] != 0;
        case BP_BIMODAL2:
            return bp->table[bimodal_idx(bp, pc)] >= 2;
        case BP_GSHARE:
            return bp->table[gshare_idx(bp, pc)] >= 2;
        case BP_TOURNAMENT:
            return tournament_use_gshare(bp) ? gshare_pred(bp, pc)
                                             : bimodal_pred(bp, pc);
    }
    return 0;
}

void bp_update(BPredictor *bp, uint32_t pc, int taken) {
    switch (bp->kind) {
        case BP_STATIC_NT:
        case BP_NONE:
            break;
        case BP_BIMODAL1:
            bp->table[bimodal_idx(bp, pc)] = (uint8_t)(taken ? 1 : 0);
            break;
        case BP_BIMODAL2:
            sat_update(&bp->table[bimodal_idx(bp, pc)], taken);
            break;
        case BP_GSHARE:
            sat_update(&bp->table[gshare_idx(bp, pc)], taken);
            bp->ghist = (bp->ghist << 1) | (taken ? 1u : 0u);
            break;
        case BP_TOURNAMENT: {
            int bc = (bimodal_pred(bp, pc) == taken);
            int gc = (gshare_pred(bp, pc) == taken);
            if (bc != gc) {
                uint32_t ci = ghist_val(bp) & bp->mask;
                sat_update(&bp->chooser[ci], gc);   /* toward whichever was right */
            }
            sat_update(&bp->table[bimodal_idx(bp, pc)], taken);
            sat_update(&bp->gtable[gshare_idx(bp, pc)], taken);
            bp->ghist = (bp->ghist << 1) | (taken ? 1u : 0u);
            break;
        }
    }
}

int btb_lookup(const BPredictor *bp, uint32_t pc, uint32_t *target) {
    uint32_t i = pc_index(pc) & bp->btb_mask;
    if (bp->btb_valid[i] && bp->btb_tag[i] == pc) {
        *target = bp->btb_target[i];
        return 1;
    }
    return 0;
}

void btb_update(BPredictor *bp, uint32_t pc, uint32_t target) {
    uint32_t i = pc_index(pc) & bp->btb_mask;
    bp->btb_valid[i]  = 1;
    bp->btb_tag[i]    = pc;
    bp->btb_target[i] = target;
}

const char *bp_name(BpKind kind) {
    switch (kind) {
        case BP_NONE:       return "none";
        case BP_STATIC_NT:  return "static-NT";
        case BP_BIMODAL1:   return "bimodal-1bit";
        case BP_BIMODAL2:   return "bimodal-2bit";
        case BP_GSHARE:     return "gshare";
        case BP_TOURNAMENT: return "tournament";
    }
    return "?";
}
