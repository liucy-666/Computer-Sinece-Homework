///////////////////////////////////////////////////////////////////////
////  Copyright 2020 by mars.                                        //
///////////////////////////////////////////////////////////////////////

#include <stdint.h>
#include <string.h>

#include "common.h"

#ifndef UINT8
#define UINT8 uint8_t
#endif

#ifndef INT8
#define INT8 int8_t
#endif

#define TAKEN 'T'
#define NOT_TAKEN 'N'

#define NUM_TAG_TABLES 12

#define BIMODAL_SIZE 65536
#define BIMODAL_MASK (BIMODAL_SIZE - 1)

#define LOCAL_HISTORY_SIZE 8192
#define LOCAL_HISTORY_MASK (LOCAL_HISTORY_SIZE - 1)
#define LOCAL_HISTORY_BITS 12
#define LOCAL_HISTORY_LENGTH_MASK ((1u << LOCAL_HISTORY_BITS) - 1u)
#define LOCAL_PHT_SIZE 65536
#define LOCAL_PHT_MASK (LOCAL_PHT_SIZE - 1)

#define GLOBAL_BASE_SIZE 65536
#define GLOBAL_BASE_MASK (GLOBAL_BASE_SIZE - 1)
#define GLOBAL_BASE_HISTORY_BITS 16
#define GLOBAL_BASE_HISTORY_MASK ((1u << GLOBAL_BASE_HISTORY_BITS) - 1u)

#define LOOP_SET_COUNT 1024
#define LOOP_SET_MASK (LOOP_SET_COUNT - 1)
#define LOOP_WAYS 4
#define LOOP_TAG_BITS 16
#define LOOP_TAG_MASK ((1u << LOOP_TAG_BITS) - 1u)
#define LOOP_CONF_MAX 7
#define LOOP_AGE_MAX 7

#define ALT_SELECTOR_SIZE 1024
#define ALT_SELECTOR_MASK (ALT_SELECTOR_SIZE - 1)

#define CHOOSER_SIZE 8192
#define CHOOSER_MASK (CHOOSER_SIZE - 1)

#define SC_GLOBAL_TABLES 6
#define SC_LOCAL_TABLES 2
#define SC_TABLE_SIZE 4096
#define SC_TABLE_MASK (SC_TABLE_SIZE - 1)
#define SC_INDEX_BITS 12
#define SC_CTR_MIN (-8)
#define SC_CTR_MAX 7
#define SC_THRESHOLD_MIN 6
#define SC_THRESHOLD_MAX 31

#define TAG_TABLE_SIZE 32768
#define TAG_TABLE_MASK (TAG_TABLE_SIZE - 1)
#define TAG_INDEX_BITS 15
#define TAG_BITS 16
#define TAG_MASK ((1u << TAG_BITS) - 1u)

#define GHIST_SIZE 2048
#define GHIST_MASK (GHIST_SIZE - 1)
#define PATH_HIST_BITS 29
#define PATH_HIST_MASK ((1u << PATH_HIST_BITS) - 1u)

#define BIMODAL_CTR_MAX 3
#define CHOOSER_CTR_MAX 3
#define LOCAL_CTR_MAX 3
#define TAGE_CTR_MIN (-4)
#define TAGE_CTR_MAX 3
#define U_MAX 3
#define USE_ALT_MIN (-8)
#define USE_ALT_MAX 7

#define U_RESET_PERIOD (1u << 18)

typedef struct {
    UINT16 tag;
    INT8 ctr;
    UINT8 u;
    UINT8 valid;
} TageEntry;

typedef struct {
    UINT32 comp;
    int orig_length;
    int comp_length;
} FoldedHistory;

typedef struct {
    UINT16 tag;
    UINT16 iter;
    UINT16 current_iter;
    UINT8 conf;
    UINT8 age;
    UINT8 dir;
    UINT8 valid;
} LoopEntry;

static const int hist_lengths[NUM_TAG_TABLES] = { 4, 8, 13, 23, 40, 70, 121, 210, 364, 631, 1094, 1896 };
static const int sc_hist_lengths[SC_GLOBAL_TABLES] = { 0, 5, 16, 44, 130, 384 };

static UINT8 bimodal[BIMODAL_SIZE];
static UINT8 chooser[CHOOSER_SIZE];
static UINT16 local_history[LOCAL_HISTORY_SIZE];
static UINT8 local_pht[LOCAL_PHT_SIZE];
static UINT8 global_base[GLOBAL_BASE_SIZE];
static LoopEntry loop_table[LOOP_SET_COUNT][LOOP_WAYS];
static INT8 sc_global[SC_GLOBAL_TABLES][SC_TABLE_SIZE];
static INT8 sc_local[SC_LOCAL_TABLES][SC_TABLE_SIZE];
static INT8 alt_selector[NUM_TAG_TABLES][ALT_SELECTOR_SIZE];
static TageEntry tage[NUM_TAG_TABLES][TAG_TABLE_SIZE];

static UINT8 ghist[GHIST_SIZE];
static UINT32 ghist_ptr;
static UINT32 path_history;
static UINT32 recent_ghr;
static FoldedHistory index_fold[NUM_TAG_TABLES];
static FoldedHistory tag_fold0[NUM_TAG_TABLES];
static FoldedHistory tag_fold1[NUM_TAG_TABLES];
static FoldedHistory sc_fold[SC_GLOBAL_TABLES];

static int use_alt_on_na[NUM_TAG_TABLES];
static int sc_threshold;
static UINT32 alloc_seed;
static UINT32 branch_tick;

static int provider_bank;
static int alt_bank;
static UINT32 provider_index;
static char provider_pred;
static char alt_pred;
static char final_pred;
static char base_pred;
static UINT32 base_index;
static UINT32 local_index;
static UINT32 local_pht_index;
static UINT32 chooser_index;
static UINT32 global_base_index;
static UINT32 provider_alt_selector_index;
static UINT32 loop_set_index;
static UINT16 loop_tag;
static int sc_sum;
static char sc_pred;
static char local_pred;
static char global_base_pred;
static char loop_pred;
static int provider_is_weak;
static int loop_hit;
static int loop_way;
static int loop_use;
static int sc_use;

static UINT32 last_indices[NUM_TAG_TABLES];
static UINT16 last_tags[NUM_TAG_TABLES];
static UINT32 last_sc_global_indices[SC_GLOBAL_TABLES];
static UINT32 last_sc_local_indices[SC_LOCAL_TABLES];

static inline UINT32 rotate32(UINT32 value, unsigned shift)
{
    shift &= 31u;
    if (shift == 0u) {
        return value;
    }
    return (value << shift) | (value >> (32u - shift));
}

static inline UINT8 sat_inc_u8(UINT8 value, UINT8 max)
{
    return (value < max) ? (UINT8)(value + 1u) : value;
}

static inline UINT8 sat_dec_u8(UINT8 value)
{
    return (value > 0u) ? (UINT8)(value - 1u) : value;
}

static inline INT8 sat_inc_i8(INT8 value, INT8 max)
{
    return (value < max) ? (INT8)(value + 1) : value;
}

static inline INT8 sat_dec_i8(INT8 value, INT8 min)
{
    return (value > min) ? (INT8)(value - 1) : value;
}

static inline int sat_inc_int(int value, int max)
{
    return (value < max) ? (value + 1) : value;
}

static inline int sat_dec_int(int value, int min)
{
    return (value > min) ? (value - 1) : value;
}

static inline char ctr_to_dir_i8(INT8 ctr)
{
    return (ctr >= 0) ? TAKEN : NOT_TAKEN;
}

static inline char ctr_to_dir_u8(UINT8 ctr)
{
    return (ctr >= 2u) ? TAKEN : NOT_TAKEN;
}

static inline int ctr_is_weak_u8(UINT8 ctr)
{
    return (ctr == 1u || ctr == 2u);
}

static inline int ctr_is_weak(INT8 ctr)
{
    return (ctr == 0 || ctr == -1);
}

static inline void init_folded(FoldedHistory *folded, int orig_length, int comp_length)
{
    folded->comp = 0u;
    folded->orig_length = orig_length;
    folded->comp_length = comp_length;
}

static inline void update_folded(FoldedHistory *folded, UINT8 newest_bit)
{
    UINT32 mask = (1u << folded->comp_length) - 1u;
    UINT32 outgoing_bit = ghist[(ghist_ptr + (UINT32)folded->orig_length) & GHIST_MASK];
    UINT32 comp = (folded->comp << 1) | newest_bit;

    comp ^= outgoing_bit << (folded->orig_length % folded->comp_length);
    comp ^= (comp >> folded->comp_length);
    folded->comp = comp & mask;
}

static inline UINT32 get_base_index(UINT64 pc)
{
    return (UINT32)((pc ^ (pc >> 2) ^ (pc >> 11)) & BIMODAL_MASK);
}

static inline UINT32 get_local_index(UINT64 pc)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= (UINT32)(pc >> 14);
    mix ^= (UINT32)(pc >> 25);
    return mix & LOCAL_HISTORY_MASK;
}

static inline UINT32 get_local_pht_index(UINT64 pc, UINT16 history)
{
    UINT32 mix = (UINT32)pc;

    mix ^= (UINT32)(pc >> 4);
    mix ^= (UINT32)(pc >> 15);
    mix ^= ((UINT32)history << 5);
    mix ^= ((UINT32)history >> 2);
    mix += rotate32((UINT32)history, 11);
    return mix & LOCAL_PHT_MASK;
}

static inline UINT32 get_chooser_index(UINT64 pc)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= recent_ghr;
    mix ^= rotate32(recent_ghr, 7);
    mix ^= (UINT32)(pc >> 13);
    return mix & CHOOSER_MASK;
}

static inline UINT32 get_global_base_index(UINT64 pc)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= recent_ghr;
    mix ^= rotate32(recent_ghr, 9);
    mix ^= (UINT32)(pc >> 17);
    mix += rotate32((UINT32)pc, 5);
    return mix & GLOBAL_BASE_MASK;
}

static inline UINT32 get_alt_selector_index(UINT64 pc, int bank)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= rotate32(recent_ghr, (unsigned)(bank + 3));
    mix ^= rotate32(path_history, (unsigned)(bank + 1));
    mix ^= (UINT32)(pc >> (bank + 7));
    return mix & ALT_SELECTOR_MASK;
}

static inline UINT32 get_loop_set_index(UINT64 pc)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= rotate32(recent_ghr, 5);
    mix ^= (UINT32)(pc >> 13);
    return mix & LOOP_SET_MASK;
}

static inline UINT16 get_loop_tag(UINT64 pc)
{
    UINT32 mix = (UINT32)pc;

    mix ^= (UINT32)(pc >> 5);
    mix ^= rotate32(path_history, 3);
    mix ^= rotate32(recent_ghr, 11);
    return (UINT16)(mix & LOOP_TAG_MASK);
}

static inline UINT32 get_sc_global_index(UINT64 pc, int bank)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= rotate32(path_history, (unsigned)(bank + 3));
    mix ^= rotate32(recent_ghr, (unsigned)(bank + 7));
    mix ^= (UINT32)(pc >> (bank + 11));
    if (sc_hist_lengths[bank] > 0) {
        mix ^= sc_fold[bank].comp;
    }
    return mix & SC_TABLE_MASK;
}

static inline UINT32 get_sc_local_index(UINT64 pc, UINT16 history, int bank)
{
    UINT32 mix = (UINT32)(pc >> 2);

    mix ^= ((UINT32)history << (bank + 2));
    mix ^= rotate32((UINT32)history, (unsigned)(bank * 5 + 3));
    mix ^= rotate32(recent_ghr, (unsigned)(bank * 3 + 9));
    mix ^= (UINT32)(pc >> (bank + 13));
    return mix & SC_TABLE_MASK;
}

static inline UINT32 get_tage_index(UINT64 pc, int bank)
{
    UINT32 mix = (UINT32)pc;

    mix ^= (UINT32)(pc >> 3);
    mix ^= (UINT32)(pc >> (TAG_INDEX_BITS + (bank & 3)));
    mix ^= index_fold[bank].comp;
    mix ^= recent_ghr;
    mix ^= rotate32(recent_ghr, (unsigned)((bank << 1) + 1));
    mix ^= rotate32(path_history, (unsigned)(bank + 1));
    return mix & TAG_TABLE_MASK;
}

static inline UINT16 get_tage_tag(UINT64 pc, int bank)
{
    UINT32 mix = (UINT32)pc;

    mix ^= (UINT32)(pc >> 5);
    mix ^= recent_ghr;
    mix ^= rotate32(recent_ghr, (unsigned)(bank + 9));
    mix ^= rotate32(path_history, (unsigned)(bank + 5));
    mix ^= tag_fold0[bank].comp;
    mix ^= (tag_fold1[bank].comp << 1);
    return (UINT16)(mix & TAG_MASK);
}

static inline UINT32 next_alloc_seed(void)
{
    alloc_seed = alloc_seed * 1103515245u + 12345u;
    return alloc_seed;
}

static void reset_usefulness(void)
{
    int bank;
    UINT32 i;

    for (bank = 0; bank < NUM_TAG_TABLES; bank++) {
        for (i = 0; i < TAG_TABLE_SIZE; i++) {
            tage[bank][i].u >>= 1;
        }
    }
}

static void update_histories(UINT64 pc, char resolveDir)
{
    UINT8 bit = (resolveDir == TAKEN) ? 1u : 0u;
    int bank;

    ghist_ptr = (ghist_ptr - 1u) & GHIST_MASK;
    ghist[ghist_ptr] = bit;

    for (bank = 0; bank < NUM_TAG_TABLES; bank++) {
        update_folded(&index_fold[bank], bit);
        update_folded(&tag_fold0[bank], bit);
        update_folded(&tag_fold1[bank], bit);
    }
    for (bank = 1; bank < SC_GLOBAL_TABLES; bank++) {
        update_folded(&sc_fold[bank], bit);
    }

    path_history = ((path_history << 5) ^ (UINT32)(pc >> 2) ^ bit) & PATH_HIST_MASK;
    recent_ghr = ((recent_ghr << 1) | bit) & GLOBAL_BASE_HISTORY_MASK;
}

void PREDICTOR_init(void)
{
    int bank;
    UINT32 i;

    memset(tage, 0, sizeof(tage));
    memset(ghist, 0, sizeof(ghist));
    memset(loop_table, 0, sizeof(loop_table));
    memset(sc_global, 0, sizeof(sc_global));
    memset(sc_local, 0, sizeof(sc_local));
    memset(alt_selector, 0, sizeof(alt_selector));

    for (i = 0; i < BIMODAL_SIZE; i++) {
        bimodal[i] = 2u;
    }
    for (i = 0; i < LOCAL_HISTORY_SIZE; i++) {
        local_history[i] = 0u;
    }
    for (i = 0; i < CHOOSER_SIZE; i++) {
        chooser[i] = 1u;
    }
    for (i = 0; i < LOCAL_PHT_SIZE; i++) {
        local_pht[i] = 1u;
    }
    for (i = 0; i < GLOBAL_BASE_SIZE; i++) {
        global_base[i] = 1u;
    }

    ghist_ptr = 0u;
    path_history = 0u;
    recent_ghr = 0u;
    memset(use_alt_on_na, 0, sizeof(use_alt_on_na));
    sc_threshold = 22;
    alloc_seed = 1u;
    branch_tick = 0u;

    provider_bank = -1;
    alt_bank = -1;
    provider_index = 0u;
    provider_pred = NOT_TAKEN;
    alt_pred = NOT_TAKEN;
    final_pred = NOT_TAKEN;
    base_pred = NOT_TAKEN;
    base_index = 0u;
    local_index = 0u;
    local_pht_index = 0u;
    chooser_index = 0u;
    global_base_index = 0u;
    provider_alt_selector_index = 0u;
    loop_set_index = 0u;
    loop_tag = 0u;
    sc_sum = 0;
    sc_pred = NOT_TAKEN;
    local_pred = NOT_TAKEN;
    global_base_pred = NOT_TAKEN;
    loop_pred = NOT_TAKEN;
    provider_is_weak = 0;
    loop_hit = 0;
    loop_way = -1;
    loop_use = 0;
    sc_use = 0;

    for (bank = 0; bank < NUM_TAG_TABLES; bank++) {
        init_folded(&index_fold[bank], hist_lengths[bank], TAG_INDEX_BITS);
        init_folded(&tag_fold0[bank], hist_lengths[bank], TAG_BITS);
        init_folded(&tag_fold1[bank], hist_lengths[bank], TAG_BITS - 1);
    }
    for (bank = 1; bank < SC_GLOBAL_TABLES; bank++) {
        init_folded(&sc_fold[bank], sc_hist_lengths[bank], SC_INDEX_BITS);
    }
}

char GetPrediction(UINT64 PC)
{
    int bank;
    UINT8 bimodal_pred;
    UINT8 local_ctr;
    UINT8 global_ctr;
    UINT8 bimodal_ctr;
    UINT8 meta;

    base_index = get_base_index(PC);
    local_index = get_local_index(PC);
    local_pht_index = get_local_pht_index(PC, local_history[local_index]);
    chooser_index = get_chooser_index(PC);
    global_base_index = get_global_base_index(PC);
    loop_set_index = get_loop_set_index(PC);
    loop_tag = get_loop_tag(PC);

    bimodal_ctr = bimodal[base_index];
    local_ctr = local_pht[local_pht_index];
    global_ctr = global_base[global_base_index];
    bimodal_pred = ctr_to_dir_u8(bimodal_ctr);
    local_pred = ctr_to_dir_u8(local_ctr);
    global_base_pred = ctr_to_dir_u8(global_ctr);
    meta = chooser[chooser_index];

    if (local_pred == global_base_pred) {
        base_pred = local_pred;
    } else if (!ctr_is_weak_u8(local_ctr) && ctr_is_weak_u8(global_ctr)) {
        base_pred = local_pred;
    } else if (!ctr_is_weak_u8(global_ctr) && ctr_is_weak_u8(local_ctr)) {
        base_pred = global_base_pred;
    } else if (!ctr_is_weak_u8(bimodal_ctr) && bimodal_pred == local_pred) {
        base_pred = local_pred;
    } else if (!ctr_is_weak_u8(bimodal_ctr) && bimodal_pred == global_base_pred) {
        base_pred = global_base_pred;
    } else {
        base_pred = (meta >= 2u) ? local_pred : global_base_pred;
    }

    provider_bank = -1;
    alt_bank = -1;
    provider_pred = base_pred;
    alt_pred = base_pred;
    provider_is_weak = 0;
    loop_hit = 0;
    loop_way = -1;
    loop_use = 0;
    loop_pred = base_pred;
    sc_sum = 0;
    sc_pred = final_pred;
    sc_use = 0;

    for (bank = 0; bank < NUM_TAG_TABLES; bank++) {
        last_indices[bank] = get_tage_index(PC, bank);
        last_tags[bank] = get_tage_tag(PC, bank);
    }
    for (bank = 0; bank < SC_GLOBAL_TABLES; bank++) {
        last_sc_global_indices[bank] = get_sc_global_index(PC, bank);
        sc_sum += sc_global[bank][last_sc_global_indices[bank]];
    }
    for (bank = 0; bank < SC_LOCAL_TABLES; bank++) {
        last_sc_local_indices[bank] = get_sc_local_index(PC, local_history[local_index], bank);
        sc_sum += sc_local[bank][last_sc_local_indices[bank]];
    }

    for (bank = NUM_TAG_TABLES - 1; bank >= 0; bank--) {
        TageEntry *entry = &tage[bank][last_indices[bank]];

        if (entry->valid && entry->tag == last_tags[bank]) {
            if (provider_bank < 0) {
                provider_bank = bank;
                provider_index = last_indices[bank];
                provider_alt_selector_index = get_alt_selector_index(PC, bank);
                provider_pred = ctr_to_dir_i8(entry->ctr);
                provider_is_weak = ctr_is_weak(entry->ctr);
            } else if (alt_bank < 0) {
                alt_bank = bank;
                alt_pred = ctr_to_dir_i8(entry->ctr);
                break;
            }
        }
    }

    if (provider_bank >= 0) {
        TageEntry *provider = &tage[provider_bank][provider_index];

        if (provider_is_weak &&
            (provider->u == 0u ||
             use_alt_on_na[provider_bank] > 0 ||
             alt_selector[provider_bank][provider_alt_selector_index] > 0)) {
            final_pred = alt_pred;
        } else {
            final_pred = provider_pred;
        }
    } else {
        final_pred = base_pred;
    }

    for (bank = 0; bank < LOOP_WAYS; bank++) {
        LoopEntry *entry = &loop_table[loop_set_index][bank];

        if (entry->valid && entry->tag == loop_tag) {
            loop_hit = 1;
            loop_way = bank;
            loop_pred = (entry->iter != 0u && entry->current_iter == entry->iter)
                ? (entry->dir == TAKEN ? NOT_TAKEN : TAKEN)
                : (char)entry->dir;
            if (entry->conf >= 3u && entry->age >= 1u) {
                final_pred = loop_pred;
                loop_use = 1;
            }
            break;
        }
    }

    sc_sum += (final_pred == TAKEN) ? 18 : -18;

    if (provider_bank >= 0) {
        TageEntry *provider = &tage[provider_bank][provider_index];
        sc_sum += provider->ctr * 3;
        sc_sum += (int)provider->u;
        if (provider_is_weak) {
            sc_sum -= (final_pred == TAKEN) ? 2 : -2;
        }
    }

    sc_pred = (sc_sum >= 0) ? TAKEN : NOT_TAKEN;
    if (sc_pred != final_pred) {
        int abs_sum = (sc_sum >= 0) ? sc_sum : -sc_sum;
        int override_threshold = sc_threshold;

        if (provider_bank < 0 || provider_is_weak) {
            override_threshold -= 6;
        } else if (provider_bank >= NUM_TAG_TABLES - 3) {
            override_threshold += 2;
        }
        if (loop_use) {
            override_threshold += 2;
        }
        if (override_threshold < SC_THRESHOLD_MIN) {
            override_threshold = SC_THRESHOLD_MIN;
        } else if (override_threshold > SC_THRESHOLD_MAX) {
            override_threshold = SC_THRESHOLD_MAX;
        }

        if (abs_sum >= override_threshold) {
            final_pred = sc_pred;
            sc_use = 1;
        }
    }

    return final_pred;
}

void UpdatePredictor(UINT64 PC, OpType opType, char resolveDir, char predDir, UINT64 branchTarget)
{
    int bank;
    int way;
    int alloc_banks[NUM_TAG_TABLES];
    int alloc_count = 0;
    int start_bank;
    int abs_sc_sum;
    char global_pred;

    (void)PC;
    (void)opType;
    (void)branchTarget;

    global_pred = ctr_to_dir_u8(global_base[global_base_index]);
    abs_sc_sum = (sc_sum >= 0) ? sc_sum : -sc_sum;

    if (provider_bank >= 0 && provider_is_weak && provider_pred != alt_pred) {
        if (alt_pred == resolveDir && provider_pred != resolveDir) {
            use_alt_on_na[provider_bank] = sat_inc_int(use_alt_on_na[provider_bank], USE_ALT_MAX);
            alt_selector[provider_bank][provider_alt_selector_index] =
                sat_inc_i8(alt_selector[provider_bank][provider_alt_selector_index], 7);
        } else if (provider_pred == resolveDir && alt_pred != resolveDir) {
            use_alt_on_na[provider_bank] = sat_dec_int(use_alt_on_na[provider_bank], USE_ALT_MIN);
            alt_selector[provider_bank][provider_alt_selector_index] =
                sat_dec_i8(alt_selector[provider_bank][provider_alt_selector_index], -8);
        }
    }

    if (resolveDir == TAKEN) {
        bimodal[base_index] = sat_inc_u8(bimodal[base_index], BIMODAL_CTR_MAX);
        local_pht[local_pht_index] = sat_inc_u8(local_pht[local_pht_index], LOCAL_CTR_MAX);
        global_base[global_base_index] = sat_inc_u8(global_base[global_base_index], LOCAL_CTR_MAX);
    } else {
        bimodal[base_index] = sat_dec_u8(bimodal[base_index]);
        local_pht[local_pht_index] = sat_dec_u8(local_pht[local_pht_index]);
        global_base[global_base_index] = sat_dec_u8(global_base[global_base_index]);
    }

    if (global_pred != local_pred) {
        if (local_pred == resolveDir) {
            chooser[chooser_index] = sat_inc_u8(chooser[chooser_index], CHOOSER_CTR_MAX);
        } else if (global_pred == resolveDir) {
            chooser[chooser_index] = sat_dec_u8(chooser[chooser_index]);
        }
    }

    if (sc_pred != resolveDir || abs_sc_sum < (sc_threshold + 4)) {
        for (bank = 0; bank < SC_GLOBAL_TABLES; bank++) {
            INT8 *ctr = &sc_global[bank][last_sc_global_indices[bank]];

            if (resolveDir == TAKEN) {
                *ctr = sat_inc_i8(*ctr, SC_CTR_MAX);
            } else {
                *ctr = sat_dec_i8(*ctr, SC_CTR_MIN);
            }
        }
        for (bank = 0; bank < SC_LOCAL_TABLES; bank++) {
            INT8 *ctr = &sc_local[bank][last_sc_local_indices[bank]];

            if (resolveDir == TAKEN) {
                *ctr = sat_inc_i8(*ctr, SC_CTR_MAX);
            } else {
                *ctr = sat_dec_i8(*ctr, SC_CTR_MIN);
            }
        }
    }

    if (sc_use) {
        if (sc_pred == resolveDir) {
            sc_threshold = sat_dec_int(sc_threshold, SC_THRESHOLD_MIN);
        } else {
            sc_threshold = sat_inc_int(sc_threshold, SC_THRESHOLD_MAX);
        }
    }

    local_history[local_index] =
        (UINT16)(((UINT32)local_history[local_index] << 1) | (resolveDir == TAKEN ? 1u : 0u));
    local_history[local_index] &= LOCAL_HISTORY_LENGTH_MASK;

    if (provider_bank >= 0) {
        TageEntry *provider = &tage[provider_bank][provider_index];

        if (resolveDir == TAKEN) {
            provider->ctr = sat_inc_i8(provider->ctr, TAGE_CTR_MAX);
        } else {
            provider->ctr = sat_dec_i8(provider->ctr, TAGE_CTR_MIN);
        }

        if (provider_pred != alt_pred) {
            if (provider_pred == resolveDir) {
                provider->u = sat_inc_u8(provider->u, U_MAX);
            } else {
                provider->u = sat_dec_u8(provider->u);
            }
        }
    }

    if (loop_hit) {
        LoopEntry *entry = &loop_table[loop_set_index][loop_way];

        if (loop_use) {
            if (loop_pred == resolveDir) {
                entry->age = sat_inc_u8(entry->age, LOOP_AGE_MAX);
            } else {
                entry->age = sat_dec_u8(entry->age);
                entry->conf = sat_dec_u8(entry->conf);
            }
        }

        if (resolveDir == (char)entry->dir) {
            if (entry->current_iter != 0xffffu) {
                entry->current_iter++;
            }
            if (entry->iter != 0u && entry->current_iter > entry->iter) {
                entry->iter = 0u;
                entry->conf = 0u;
                entry->age = sat_dec_u8(entry->age);
            }
        } else {
            if (entry->current_iter > 0u) {
                if (entry->iter == entry->current_iter) {
                    entry->conf = sat_inc_u8(entry->conf, LOOP_CONF_MAX);
                } else {
                    entry->iter = entry->current_iter;
                    entry->conf = 0u;
                    entry->age = sat_dec_u8(entry->age);
                }
            } else {
                entry->conf = 0u;
                entry->age = sat_dec_u8(entry->age);
            }
            entry->current_iter = 0u;
        }
    } else if (predDir != resolveDir) {
        int replace_way = -1;
        UINT8 best_score = 255u;

        for (way = 0; way < LOOP_WAYS; way++) {
            LoopEntry *entry = &loop_table[loop_set_index][way];
            UINT8 score = (UINT8)((entry->age << 3) | entry->conf);

            if (!entry->valid) {
                replace_way = way;
                break;
            }
            if (score < best_score) {
                best_score = score;
                replace_way = way;
            }
        }

        if (replace_way >= 0) {
            LoopEntry *entry = &loop_table[loop_set_index][replace_way];

            entry->valid = 1u;
            entry->tag = loop_tag;
            entry->iter = 0u;
            entry->current_iter = 0u;
            entry->conf = 0u;
            entry->age = 0u;
            entry->dir = (UINT8)predDir;
        }
    }

    if (final_pred != resolveDir) {
        start_bank = provider_bank + 1;
        if (start_bank < 0) {
            start_bank = 0;
        }

        for (bank = start_bank; bank < NUM_TAG_TABLES; bank++) {
            TageEntry *entry = &tage[bank][last_indices[bank]];

            if (!entry->valid || entry->u == 0u) {
                alloc_banks[alloc_count++] = bank;
            }
        }

        if (alloc_count == 0) {
            for (bank = start_bank; bank < NUM_TAG_TABLES; bank++) {
                TageEntry *entry = &tage[bank][last_indices[bank]];
                entry->u = sat_dec_u8(entry->u);
            }
        } else {
            int max_alloc = ((provider_bank < 0 || provider_is_weak) && alloc_count > 1) ? 2 : 1;
            int allocated = 0;
            int k;

            next_alloc_seed();
            for (k = alloc_count - 1; k >= 0 && allocated < max_alloc; k--) {
                int candidate_bank = alloc_banks[k];
                TageEntry *entry = &tage[candidate_bank][last_indices[candidate_bank]];

                if (entry->valid && entry->u != 0u) {
                    if ((alloc_seed & 3u) == 0u) {
                        entry->u = sat_dec_u8(entry->u);
                    }
                    alloc_seed = rotate32(alloc_seed, 3) ^ (UINT32)candidate_bank;
                    continue;
                }

                entry->valid = 1u;
                entry->tag = last_tags[candidate_bank];
                entry->u = 0u;
                entry->ctr = (resolveDir == TAKEN) ? 0 : -1;
                allocated++;
            }
        }
    }

    branch_tick++;
    if ((branch_tick & (U_RESET_PERIOD - 1u)) == 0u) {
        reset_usefulness();
    }

    update_histories(PC, resolveDir);
}

void PREDICTOR_free(void)
{
}
