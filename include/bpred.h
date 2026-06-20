#ifndef BPRED_H
#define BPRED_H

#include <stdint.h>

typedef enum {
    BP_NONE,        /* fixed penalty on every taken control (Phase A baseline) */
    BP_STATIC_NT,   /* always predict not-taken */
    BP_BIMODAL1,    /* 1-bit counters */
    BP_BIMODAL2,    /* 2-bit saturating counters */
    BP_GSHARE,      /* global history XOR pc index, 2-bit counters */
    BP_TOURNAMENT,  /* chooser selects between bimodal-2bit and gshare */
    BP_TOURNAMENT3  /* 3-way chooser over static / bimodal / gshare */
} BpKind;

typedef struct {
    BpKind   kind;
    int      idx_bits;
    uint32_t mask;
    uint8_t *table;       /* simple predictor, or the bimodal half of tournament */

    uint8_t *gtable;      /* gshare half of tournament */
    uint8_t *chooser;     /* tournament chooser (>=2 => trust gshare) */

    uint32_t ghist;
    int      ghist_bits;

    int       btb_bits;
    uint32_t  btb_mask;
    uint32_t *btb_tag;
    uint32_t *btb_target;
    uint8_t  *btb_valid;
} BPredictor;

void bp_init(BPredictor *bp, BpKind kind, int idx_bits, int ghist_bits);
void bp_free(BPredictor *bp);

int  bp_predict(const BPredictor *bp, uint32_t pc);          /* predicted taken? */
void bp_update(BPredictor *bp, uint32_t pc, int taken);

int  btb_lookup(const BPredictor *bp, uint32_t pc, uint32_t *target);
void btb_update(BPredictor *bp, uint32_t pc, uint32_t target);

const char *bp_name(BpKind kind);

#endif /* BPRED_H */
