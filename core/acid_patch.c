// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <string.h>
static uint32_t checksum(const uint8_t *data, size_t length) {
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < length; ++i) { hash ^= data[i]; hash *= 16777619u; }
    return hash;
}
static void put32(uint8_t *out, uint32_t v) {
    for (unsigned i = 0; i < 4; ++i) out[i] = v >> (i * 8);
}
static uint32_t get32(const uint8_t *in) {
    uint32_t v = 0;
    for (unsigned i = 0; i < 4; ++i) v |= (uint32_t)in[i] << (i * 8);
    return v;
}
static bool valid_memory(const AcidPatch *c, const AcidPattern *p) {
    for (unsigned i = 0; i < 16; ++i) {
        if (p->pitch[i] < c->low || p->pitch[i] > c->high ||
            p->base_pitch[i] < c->low || p->base_pitch[i] > c->high) return false;
        if (!(c->scale & (1u << (p->pitch[i] % 12))) ||
            !(c->scale & (1u << (p->base_pitch[i] % 12)))) return false;

    }
    for (unsigned i = 0; i < 8; ++i)
        if (p->octave[i] < -1 || p->octave[i] > 1 || p->base_octave[i] < -1 || p->base_octave[i] > 1)
            return false;
    return true;
}
static bool valid_pattern(const AcidPatch *c, const AcidPattern *p) {
    if (p->gate_priority >= 16 || p->root > 11 || !p->rng || !p->base_rng) return false;
    for (unsigned pc = 0; pc < 12; ++pc) if (!p->weights[pc]) return false;
    for (unsigned l = 0; l < ACID_LANES; ++l) {
        if (!p->length[l] || p->length[l] > acid_lane_capacity[l]) return false;
        static const uint8_t max[ACID_LANES] = {2, 1, 1, 3, 5, 2};
        for (unsigned s = 0; s < 16; ++s) if (p->steps[l][s] > max[l]) return false;
    }
    for (unsigned l = 0; l < 3; ++l) if (p->intensity[l] > 3) return false;
    return valid_memory(c, p);
}
static bool valid_output(const AcidPatch *p) {
    return p->channel >= 1 && p->channel <= 16 && p->velocity >= 1 && p->velocity <= 127 &&
           p->accent_velocity >= 1 && p->accent_velocity <= 127 && p->accent_cc >= -1 &&
           p->length_percent >= 1 && p->length_percent <= 100 && p->overlap_ms <= 500;
}
static bool valid_generator(const AcidPatch *p) {
    if (p->low < 12 || p->high > 115 || p->high < p->low + 11 ||
        p->green_chance > 100 || p->yellow_chance > 100) return false;
    unsigned sum = 0;
    for (unsigned i = 0; i < 5; ++i) sum += p->strategy[i];
    return sum && (p->strategy[0] || p->strategy[1] || p->strategy[2] || p->strategy[4]);
}
bool acid_patch_valid(const AcidPatch *p) {
    if (p->bpm < 30 || p->bpm > 300 || p->selected > 7 || !p->scale || p->scale > 0xfff) return false;
    if (!valid_output(p) || !valid_generator(p)) return false;
    for (unsigned i = 0; i < 8; ++i) if (!valid_pattern(p, &p->patterns[i])) return false;
    return true;
}
static size_t encode_pattern(const AcidPattern *p, uint8_t *d) {
    size_t at = 0;
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) {
        memcpy(d + at, p->steps[lane], acid_lane_capacity[lane]); at += acid_lane_capacity[lane];
    }
    memcpy(d + at, p->length, ACID_LANES); at += ACID_LANES;
    memcpy(d + at, p->intensity, 3); at += 3;
    memcpy(d + at, p->base_pitch, 16); at += 16;
    memcpy(d + at, p->pitch, 16); at += 16;
    for (unsigned i = 0; i < 8; ++i) d[at++] = p->base_octave[i] + 1;
    for (unsigned i = 0; i < 8; ++i) d[at++] = p->octave[i] + 1;
    d[at++] = p->anchors; d[at++] = p->anchors >> 8;
    d[at++] = p->root; memcpy(d + at, p->weights, 12); at += 12;
    d[at++] = p->pitch_lock;
    put32(d + at, p->base_rng); at += 4; put32(d + at, p->rng); at += 4;
    d[at++] = p->gate_priority;
    return at;
}
static bool decode_octaves(int8_t out[8], const uint8_t *d, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        if (d[i] > 2) return false;
        if (i < 8) out[i] = (int)d[i] - 1;
    }
    return true;
}
static bool decode_steps(AcidPattern *p, const uint8_t *d, bool legacy) {
    size_t at = 0;
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) {
        if (legacy && lane == ACID_CC) { memset(p->steps[lane], 1, 8); break; }
        memcpy(p->steps[lane], d + at, acid_lane_capacity[lane]);
        at += acid_lane_capacity[lane];
    }
    // The second old octave row is retired; still validate it before migration.
    if (legacy) for (unsigned i = 72; i < 80; ++i) if (d[i] > OCT_DOWN) return false;
    return true;
}
static bool decode_pattern(AcidPattern *p, const uint8_t *d, unsigned version, uint16_t *old_scale) {
    bool legacy = version == 1;
    if (version < 3) for (unsigned i = 0; i < 16; ++i) if (d[i] > GATE_ON) return false;
    if (!decode_steps(p, d, legacy)) return false;
    size_t at = 80;
    if (legacy) {
        for (unsigned i = 0; i < 5; ++i) if (d[at + i] > 2) return false;
        memcpy(p->length, acid_lane_capacity, ACID_LANES); at += 5;
    } else { memcpy(p->length, d + at, ACID_LANES); at += ACID_LANES; }
    memcpy(p->intensity, d + at, 3); at += 3;
    memcpy(p->base_pitch, d + at, 16); at += 16;
    memcpy(p->pitch, d + at, 16); at += 16;
    unsigned count = legacy ? 16 : 8;
    if (!decode_octaves(p->base_octave, d + at, count)) return false;
    at += count;
    if (!decode_octaves(p->octave, d + at, count)) return false;
    at += count;
    if (legacy) { *old_scale = d[at] | (uint16_t)d[at + 1] << 8; at += 2; }
    p->anchors = d[at] | (uint16_t)d[at + 1] << 8; at += 2;
    p->root = d[at++]; memcpy(p->weights, d + at, 12); at += 12;
    if (d[at] > (legacy ? 3 : 1)) return false;
    p->pitch_lock = d[at++] != 0; // Either old lock freezes both dimensions after migration.
    p->base_rng = get32(d + at); at += 4; p->rng = get32(d + at); at += 4;
    p->gate_priority = version >= 3 ? d[at] : 0;
    return true;
}
static bool migrate_scales(AcidPatch *p, const uint16_t old_scale[8]) {
    for (unsigned i = 0; i < ACID_PATTERNS; ++i) {
        p->scale = old_scale[i];
        if (!p->scale || p->scale > 0xfff || !valid_pattern(p, &p->patterns[i])) return false;
    }
    p->scale = old_scale[p->selected];
    for (unsigned i = 0; i < ACID_PATTERNS; ++i) {
        AcidPattern *pattern = &p->patterns[i];
        for (unsigned step = 0; step < 16; ++step) {
            pattern->pitch[step] = acid_quantize(p, pattern->pitch[step]);
            pattern->base_pitch[step] = acid_quantize(p, pattern->base_pitch[step]);
        }
    }
    return true;
}
void acid_patch_encode(const AcidPatch *p, uint8_t out[ACID_PATCH_BYTES]) {
    memset(out, 0, ACID_PATCH_BYTES); memcpy(out, "C1AC", 4); out[4] = 3;
    out[5] = p->selected; out[6] = p->bpm; out[7] = p->bpm >> 8;
    out[8] = p->channel; out[9] = p->velocity; out[10] = p->accent_velocity;
    out[11] = p->length_percent; out[12] = p->overlap_ms; out[13] = p->overlap_ms >> 8;
    out[14] = p->low; out[15] = p->high; out[16] = p->accent_cc + 1;
    out[17] = p->internal_clock | p->learning << 1;
    memcpy(out + 18, p->strategy, 5); out[23] = p->green_chance; out[24] = p->yellow_chance;
    out[25] = p->scale; out[26] = p->scale >> 8;
    size_t at = 32;
    for (unsigned i = 0; i < 8; ++i) at += encode_pattern(&p->patterns[i], out + at);
    put32(out + ACID_PATCH_BYTES - 4, checksum(out, ACID_PATCH_BYTES - 4));
}
bool acid_patch_decode(AcidPatch *p, const uint8_t *d, size_t n) {
    if (n != ACID_PATCH_BYTES || memcmp(d, "C1AC", 4) || (d[4] < 1 || d[4] > 3) || d[17] > 3 || d[16] > 128 ||
        get32(d + n - 4) != checksum(d, n - 4)) return false;
    AcidPatch decoded = {0};
    decoded.selected = d[5]; decoded.bpm = d[6] | (uint16_t)d[7] << 8;
    decoded.channel = d[8]; decoded.velocity = d[9]; decoded.accent_velocity = d[10];
    decoded.length_percent = d[11]; decoded.overlap_ms = d[12] | (uint16_t)d[13] << 8;
    decoded.low = d[14]; decoded.high = d[15]; decoded.accent_cc = (int)d[16] - 1;
    decoded.internal_clock = d[17] & 1; decoded.learning = d[17] & 2;
    memcpy(decoded.strategy, d + 18, 5); decoded.green_chance = d[23]; decoded.yellow_chance = d[24];
    if (decoded.selected >= ACID_PATTERNS) return false;
    bool legacy = d[4] == 1; uint16_t old_scale[8] = {0};
    decoded.scale = d[25] | (uint16_t)d[26] << 8;
    static const unsigned strides[] = {0, 178, 161, 162};
    unsigned stride = strides[d[4]];
    for (unsigned i = 0; i < ACID_PATTERNS; ++i)
        if (!decode_pattern(&decoded.patterns[i], d + 32 + i * stride, d[4], &old_scale[i]))
            return false;
    if (legacy && !migrate_scales(&decoded, old_scale)) return false;
    if (!acid_patch_valid(&decoded)) return false;
    *p = decoded; return true;
}
bool acid_apply_patch(Grid *g, const AcidPatch *p) {
    if (!acid_patch_valid(p)) return false;
    acid_stop(g); g->acid.patch = *p; acid_reset(g);
    acid_scale(g, g->acid.patch.scale); // Normalize disabled legacy roots without changing valid pitches.
    g->acid.pending_pattern = -1; g->acid.scale_page = false;
    g->acid.initialized_clock = false;
    for (unsigned i = 0; i < 3; ++i)
        memcpy(g->acid.working[i], p->patterns[p->selected].steps[i], 16);
    acid_gate_states(&g->acid.patch.patterns[p->selected], g->acid.working[ACID_GATE]);
    g->dirty = false; return true;
}
