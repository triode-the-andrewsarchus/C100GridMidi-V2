// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <string.h>
#include <stdlib.h>
unsigned acid_random(AcidPattern *p, unsigned bound) {
    uint32_t x = p->rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    p->rng = x ? x : 1;
    return bound ? p->rng % bound : 0;
}
uint8_t acid_quantize(const AcidPatch *c, int note) {
    int best = c->low, distance = 256;
    for (int n = c->low; n <= c->high; ++n) {
        if (!(c->scale & (1u << (n % 12)))) continue;
        int d = abs(n - note);
        if (d < distance) { best = n; distance = d; }
    }
    return best;
}
static unsigned weighted(AcidPattern *p, const double *weights, unsigned count) {
    double total = 0;
    for (unsigned i = 0; i < count; ++i) total += weights[i];
    double target = acid_random(p, 1000000) * total / 1000000.;
    for (unsigned i = 0; i + 1 < count; ++i) {
        if (target < weights[i]) return i;
        target -= weights[i];
    }
    return count - 1;
}
static int tonic_note(const AcidPatch *c, const AcidPattern *p) {
    int note = c->low + (p->root + 12 - c->low % 12) % 12;
    return c->scale & (1u << p->root) ? note : acid_quantize(c, note);
}
static unsigned context_step(const AcidPattern *p, unsigned step, int direction) {
    uint8_t gates[16]; acid_gate_states(p, gates);
    unsigned length = p->length[ACID_GATE], at = step;
    for (unsigned offset = 0; offset < length; ++offset) {
        at = (at + length + direction) % length;
        if (gates[at] == GATE_ON || gates[at] == GATE_LONG) return at;
    }
    return (step + length + direction) % length;
}
static double bass_weight(Grid *g, AcidPattern *p, unsigned note, unsigned step) {
    const AcidPatch *c = &g->acid.patch;
    double weight = acid_pitch_weight(&g->acid.learner, note % 12, p->root) * p->weights[note % 12];
    unsigned degree = (note + 12 - p->root) % 12;
    bool strong = (p->anchors & (1u << step)) || (step % 4 == 0 && p->steps[ACID_GATE][step]);
    if (degree == 0) weight *= strong ? 1.8 : 1.2;
    else if (degree == 7) weight *= strong ? 1.6 : 1.2;
    else if (strong) weight *= .65;
    int distance = (int)note - tonic_note(c, p);
    // Prefer a compact base register. E remains the independent source of octave motion.
    weight /= 1 + .02 * distance * distance;
    const AcidLearner *l = &g->acid.learner;
    if (l->long_term.observations >= 16) {
        double center = .25 * l->long_term.register_sum / l->long_term.observations +
                        .75 * l->recent.register_sum / l->recent.observations;
        while (center > c->high) center -= 12;
        while (center < c->low) center += 12;
        weight *= 1 + .15 / (1 + abs((int)note - (int)center));
    }
    return weight;
}
static uint8_t pool_note(Grid *g, AcidPattern *p, unsigned step, int previous, bool leap) {
    const AcidPatch *c = &g->acid.patch;
    uint8_t notes[128]; double weights[128]; unsigned count = 0;
    for (unsigned n = c->low; n <= c->high; ++n) {
        if (!(c->scale & (1u << (n % 12)))) continue;
        double weight = bass_weight(g, p, n, step);
        int distance = abs((int)n - previous);
        if (leap) {
            // Intentional leaps keep their own scoring: no subsequent stepwise penalty.
            if (distance < 5) weight *= .015;
            unsigned degree = (n + 12 - p->root) % 12;
            if (degree == 0 || degree == 3 || degree == 4 || degree == 7) weight *= 3;
            weight *= .5 + 8 * acid_interval_weight(&g->acid.learner, (int)n - previous);
        }
        notes[count] = n; weights[count++] = weight;
    }
    return notes[weighted(p, weights, count)];
}
static unsigned strategy(Grid *g, AcidPattern *p, uint8_t policy) {
    double weights[5];
    for (unsigned i = 0; i < 5; ++i) weights[i] = g->acid.patch.strategy[i];
    if (policy == PITCH_YELLOW) { weights[0] *= 2; weights[1] *= 3; weights[2] *= .1; weights[3] = 0; }
    return weighted(p, weights, 5);
}
static int motif_note(Grid *g, AcidPattern *p, unsigned step, unsigned previous_step) {
    const AcidLearner *l = &g->acid.learner;
    unsigned before = context_step(p, previous_step, -1), earlier = context_step(p, before, -1);
    int last = (int)p->pitch[previous_step] - p->pitch[before];
    int preceding = (int)p->pitch[before] - p->pitch[earlier];
    double weights[ACID_MOTIFS] = {0}, total = 0;
    for (unsigned i = 0; i < ACID_MOTIFS; ++i) {
        const AcidMotif *m = &l->motifs[i];
        if (m->count <= 1 || m->length < 2) continue;
        if (m->length == 3 && m->intervals[0] != last) continue;
        if (m->length == 4 && (m->intervals[0] != preceding || m->intervals[1] != last)) continue;
        weights[i] = acid_motif_weight(l, i) * (m->length - 1);
        total += weights[i];
    }
    if (total > 0) {
        const AcidMotif *m = &l->motifs[weighted(p, weights, ACID_MOTIFS)];
        double confidence[3]; acid_confidence(l, confidence);
        double support = (m->count - 1) / (m->count + 12 * (m->length - 1));
        if (acid_random(p, 1000) < confidence[2] * support * 1000)
            return p->pitch[previous_step] + m->intervals[m->length - 2];
    }
    // Reuse a local contour four positions back when no learned continuation fits.
    unsigned length = p->length[ACID_GATE], source = (step + length * 4 - 4) % length;
    return p->pitch[previous_step] + (int)p->pitch[source] - p->pitch[context_step(p, source, -1)];
}
static uint8_t candidate(Grid *g, AcidPattern *p, unsigned step, unsigned kind, uint8_t policy) {
    unsigned length = p->length[ACID_GATE];
    unsigned previous_step = context_step(p, step, -1);
    int previous = p->pitch[previous_step], next = p->pitch[context_step(p, step, 1)];
    int result;
    switch (kind) {
    case 0: result = p->pitch[acid_random(p, 2) ? step : (step + length * 4 - 4) % length]; break;
    case 1: {
        // Learned signed intervals are tempered by local context for this strategy only.
        double weights[9];
        for (int d = -4; d <= 4; ++d) {
            int note = acid_quantize(&g->acid.patch, previous + d);
            weights[d + 4] = acid_interval_weight(&g->acid.learner, note - previous) *
                             bass_weight(g, p, note, step);
            if (abs(next - note) < 5) weights[d + 4] *= 1.5;
        }
        result = previous + (int)weighted(p, weights, 9) - 4;
        break;
    }
    case 2: result = pool_note(g, p, step, previous, false); break;
    case 3: result = pool_note(g, p, step, previous, true); break;
    default: result = motif_note(g, p, step, previous_step); break;
    }
    if (policy == PITCH_YELLOW && abs(result - p->pitch[step]) > 4)
        result = p->pitch[step] + (result > p->pitch[step] ? 2 : -2);
    uint8_t resolved = acid_quantize(&g->acid.patch, result);
    if (policy == PITCH_YELLOW && abs((int)resolved - p->pitch[step]) > 4) return p->pitch[step];
    return resolved;
}
static uint8_t next_anchor(const AcidPattern *p, unsigned step) {
    unsigned length = p->length[ACID_GATE];
    for (unsigned offset = 1; offset <= length; ++offset) {
        unsigned i = (step + offset) % length;
        if (p->anchors & (1u << i)) return p->pitch[i];
    }
    return p->pitch[0];
}
static uint8_t anchor_note(Grid *g, AcidPattern *p, unsigned step) {
    const AcidPatch *c = &g->acid.patch;
    unsigned choice = acid_random(p, 100), fifth = (p->root + 7) % 12;
    int tonic = tonic_note(c, p);
    if (choice < 50) return tonic;
    if (choice < 80 && (c->scale & (1u << fifth)))
        return c->low + (fifth + 12 - c->low % 12) % 12;
    return pool_note(g, p, step, tonic, false);
}
void acid_generate(Grid *g, unsigned index) {
    if (index >= 8) return;
    AcidPattern *p = &g->acid.patch.patterns[index];
    if (p->pitch_lock) return;
    uint8_t tonic = tonic_note(&g->acid.patch, p);
    memset(p->pitch, tonic, sizeof(p->pitch));
    p->anchors = 1;
    // Anchors vary in count and spacing; step zero is a stable phrase reference.
    unsigned length = p->length[ACID_GATE];
    unsigned count = 3 + acid_random(p, 4);
    if (count >= length) count = length - 1;
    uint8_t gates[16]; acid_gate_states(p, gates);
    double positions[16] = {0};
    for (unsigned step = 1; step < length; ++step)
        positions[step] = gates[step] == GATE_ON || gates[step] == GATE_LONG ? (step % 4 ? 2 : 5) : 1;
    while (count) {
        unsigned at = weighted(p, positions, length);
        positions[at] = 0;
        p->anchors |= 1u << at; --count;
        p->pitch[at] = anchor_note(g, p, at);
    }
    for (unsigned step = 1; step < 16; ++step) {
        if (p->anchors & (1u << step)) continue;
        unsigned kind = strategy(g, p, PITCH_GREEN);
        uint8_t note = candidate(g, p, step, kind, PITCH_GREEN);
        // Anchor attraction includes direct jumps; it never vetoes a chosen leap.
        if (kind != 3 && acid_random(p, 100) < 20) note = next_anchor(p, step);
        p->pitch[step] = note;
    }
    memcpy(p->base_pitch, p->pitch, 16);
    p->base_rng = p->rng;
    g->dirty = true;
}
void acid_mutate(Grid *g, unsigned step, uint8_t policy) {
    Acid *a = &g->acid; AcidPattern *p = &a->patch.patterns[a->patch.selected];
    if (p->pitch_lock || policy == PITCH_RED) return;
    unsigned chance = policy == PITCH_YELLOW ? a->patch.yellow_chance : a->patch.green_chance;
    bool anchor = p->anchors & (1u << step);
    if (anchor) chance /= policy == PITCH_YELLOW ? 2 : 3;
    if (acid_random(p, 100) >= chance) return;
    unsigned kind = strategy(g, p, policy);
    uint8_t before = p->pitch[step];
    bool tonal_return = anchor && policy == PITCH_GREEN && acid_random(p, 100) < 70;
    uint8_t after = tonal_return ? anchor_note(g, p, step) : candidate(g, p, step, kind, policy);
    if (tonal_return) kind = 5;
    if (before == after) return;
    p->pitch[step] = after;
    a->mutations[a->mutation_count++ % 16] = (AcidMutation){step, before, after, kind, policy, g->now};
    g->dirty = true;
}
bool acid_scale(Grid *g, uint16_t mask) {
    if (!mask || (mask & ~0xfffu)) return false;
    g->acid.patch.scale = mask;
    for (unsigned pattern = 0; pattern < ACID_PATTERNS; ++pattern) {
        AcidPattern *p = &g->acid.patch.patterns[pattern];
        if (!(mask & (1u << p->root))) p->root = tonic_note(&g->acid.patch, p) % 12;
        for (unsigned i = 0; i < 16; ++i) {
            p->pitch[i] = acid_quantize(&g->acid.patch, p->pitch[i]);
            p->base_pitch[i] = acid_quantize(&g->acid.patch, p->base_pitch[i]);
        }
    }
    // A scale edit is an explicit performer action, so it also requantizes locked memory.
    grid_panic(g); g->dirty = true;
    return true;
}
bool acid_root(Grid *g, unsigned pc) {
    if (pc > 11) return false;
    // Explicit performer choice, not a guessed key from an ambiguous scale mask.
    g->acid.patch.scale |= 1u << pc;
    for (unsigned i = 0; i < ACID_PATTERNS; ++i) g->acid.patch.patterns[i].root = pc;
    g->dirty = true;
    return true;
}
void acid_restore(Grid *g) {
    Acid *a = &g->acid; AcidPattern *p = &a->patch.patterns[a->patch.selected];
    grid_panic(g);
    memcpy(p->pitch, p->base_pitch, 16); memcpy(p->octave, p->base_octave, 8);
    p->rng = p->base_rng;
    for (unsigned l = 0; l < 3; ++l) memcpy(a->working[l], p->steps[l], 16);
    acid_gate_states(p, a->working[ACID_GATE]);
    g->dirty = true;
}
