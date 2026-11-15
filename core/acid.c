// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <string.h>
const uint8_t acid_lane_capacity[ACID_LANES] = {16, 16, 16, 16, 8, 8};
const uint8_t acid_lane_rows[ACID_LANES] = {0, 2, 4, 6, 8, 9};
static AcidPattern *pattern(Grid *g) { return &g->acid.patch.patterns[g->acid.patch.selected]; }
bool acid_coordinate(unsigned x, unsigned y, unsigned *lane, unsigned *step) {
    if (x < 1 || x > 8 || y > 9) return false;
    *lane = y < 8 ? y / 2 : y - 4;
    *step = (y < 8 ? y % 2 * 8 : 0) + x - 1;
    return true;
}
uint8_t acid_policy(const uint8_t *steps, unsigned length, unsigned step, uint8_t fallback) {
    for (unsigned i = 0; i < length; ++i) {
        uint8_t value = steps[(step + length - i) % length];
        if (value) return value;
    }
    return fallback;
}
void acid_gate_states(const AcidPattern *p, uint8_t out[ACID_STEPS]) {
    unsigned length = p->length[ACID_GATE];
    bool occupied[16] = {false};
    for (unsigned i = 0; i < 16; ++i) out[i] = p->steps[ACID_GATE][i];
    for (unsigned i = 0; i < length; ++i)
        if (out[i] == GATE_LONG) out[i] = GATE_ON;
    // Newest edited long note wins overlapping ties. Covered base values stay intact.
    for (unsigned offset = 0; offset < length; ++offset) {
        unsigned at = (p->gate_priority + offset) % length, next = (at + 1) % length;
        if (p->steps[ACID_GATE][at] != GATE_LONG || occupied[at] || occupied[next]) continue;
        out[at] = GATE_LONG; occupied[at] = occupied[next] = true;
        if (next != at) out[next] = GATE_TIED;
    }
}
int acid_scale_key(unsigned x, unsigned y) {
    static const uint8_t whites[7] = {0, 2, 4, 5, 7, 9, 11};
    static const uint8_t black_x[5] = {2, 3, 5, 6, 7}, blacks[5] = {1, 3, 6, 8, 10};
    if (y == 5 && x >= 1 && x <= 7) return whites[x - 1];
    if (y == 4)
        for (unsigned i = 0; i < 5; ++i) if (x == black_x[i]) return blacks[i];
    return -1;
}
void acid_reset(Grid *g) {
    Acid *a = &g->acid; grid_panic(g);
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) a->head[lane] = pattern(g)->length[lane] - 1;
    a->length_select = a->scale_held = -1;
    a->pulses = 0; g->tick = 0; a->pending_reset = false;
}
void acid_init(Grid *g, uint32_t seed) {
    Acid *a = &g->acid; memset(a, 0, sizeof(*a));
    a->pending_pattern = a->pending_transport = a->length_select = -1;
    a->patch.bpm = 120; a->patch.internal_clock = true; a->patch.learning = true;
    a->patch.channel = 1; a->patch.velocity = 85; a->patch.accent_velocity = 120;
    a->patch.length_percent = 65; a->patch.overlap_ms = 18; a->patch.accent_cc = -1;
    a->patch.scale = 0x5ad; // One instrument-wide C natural minor scale.
    a->patch.low = 36; a->patch.high = 59;
    const uint8_t weights[5] = {30, 25, 20, 15, 10}; memcpy(a->patch.strategy, weights, 5);
    a->patch.green_chance = 25; a->patch.yellow_chance = 10;
    a->learner.grouping_ms = 15; a->learner.echo_ms = 35; a->pulse_ms = 21;
    for (unsigned i = 0; i < 8; ++i) {
        AcidPattern *p = &a->patch.patterns[i];
        p->rng = (seed ? seed : 1) + i * 0x9e3779b9u;
        if (!p->rng) p->rng = 1;
        memset(p->weights, 1, 12); memcpy(p->length, acid_lane_capacity, ACID_LANES);
        memset(p->steps[ACID_CC], 1, 8); // Neutral middle CC value: 64.
        for (unsigned n = 0; n < 16; ++n) {
            p->steps[0][n] = n % 4 == 0 || n == 6 || n == 10 || n == 14;
            p->steps[1][n] = n % 8 == 0; p->steps[2][n] = n == 6 || n == 14;
        }
        acid_generate(g, i);
    }
    acid_reset(g);
    for (unsigned i = 0; i < 3; ++i) memcpy(a->working[i], pattern(g)->steps[i], 16);
    acid_gate_states(pattern(g), a->working[ACID_GATE]);
    g->dirty = false;
}
void acid_start(Grid *g, bool reset) {
    if (reset) acid_reset(g);
    g->running = true; g->acid.pending_transport = -1;
}
void acid_stop(Grid *g) {
    g->running = false; g->acid.pending_transport = -1; grid_panic(g);
}
static void right_control(Grid *g, unsigned y) {
    Acid *a = &g->acid; AcidPattern *p = pattern(g);
    if (y < 6 && y % 2) {
        unsigned lane = y / 2;
        p->intensity[lane] = (p->intensity[lane] + 1) % 4;
        if (!p->intensity[lane]) {
            if (lane == ACID_GATE) acid_gate_states(p, a->working[lane]);
            else memcpy(a->working[lane], p->steps[lane], 16);
        }
        g->dirty = true;
    } else if (y == 7) { p->pitch_lock = !p->pitch_lock; g->dirty = true; }
    else {
        unsigned lane = y < 8 ? y / 2 : y - 4;
        a->gate_down = 0;
        a->length_select = a->length_select == (int)lane ? -1 : (int)lane;
    }
}
static void reset_key(Grid *g, bool down, uint32_t now) {
    Acid *a = &g->acid;
    if (down && !a->reset_down) {
        a->reset_down = true; a->reset_at = now; a->hold_opened = false;
        a->gate_down = 0;
    } else if (!down && a->reset_down) {
        a->reset_down = false;
        if (a->hold_opened) return; // Long press leaves the editor latched.
        if (a->scale_page) { a->scale_page = false; a->scale_held = -1; }
        else if ((uint32_t)(now - a->reset_at) >= ACID_HOLD_MS) { a->scale_page = true; a->length_select = -1; }
        else a->pending_reset = true;
    }
}
static void scale_note_key(Grid *g, unsigned pc, bool down, uint32_t now) {
    Acid *a = &g->acid;
    if (down) {
        if (a->scale_held >= 0) return;
        a->scale_held = pc; a->scale_at = now; a->scale_hold_done = false;
    } else if (a->scale_held == (int)pc) {
        if (!a->scale_hold_done) {
            if ((uint32_t)(now - a->scale_at) >= ACID_HOLD_MS) acid_root(g, pc);
            else acid_scale(g, a->patch.scale ^ (1u << pc));
        }
        a->scale_held = -1;
    }
}
static void scale_page_key(Grid *g, unsigned x, unsigned y, bool down, uint32_t now) {
    int pc = acid_scale_key(x, y);
    if (pc >= 0) { scale_note_key(g, pc, down, now); return; }
    if (!down || x != 9) return;
    switch (y) {
    case 0: acid_stop(g); g->action = ACTION_SAVE; return;
    case 1: acid_stop(g); g->action = ACTION_LOAD; return;
    case 2: g->acid.patch.internal_clock = !g->acid.patch.internal_clock;
            g->acid.initialized_clock = false; g->dirty = true; return;
    case 8: acid_restore(g); return;
    case 9: acid_generate(g, g->acid.patch.selected); return;
    default: return;
    }
}
static void refresh_octave_base(AcidPattern *p) {
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t policy = acid_policy(p->steps[ACID_OCTAVE], p->length[ACID_OCTAVE], i, OCT_BASE);
        p->base_octave[i] = policy == OCT_UP ? 1 : policy == OCT_DOWN ? -1 : 0;
    }
}
static void refresh_lane(Grid *g, unsigned lane) {
    AcidPattern *p = pattern(g);
    if (lane == ACID_GATE) acid_gate_states(p, g->acid.working[lane]);
    if (lane == ACID_OCTAVE) refresh_octave_base(p);
}
static void edit_gate(Grid *g, unsigned step, bool long_note) {
    Acid *a = &g->acid; AcidPattern *p = pattern(g);
    uint8_t states[16]; acid_gate_states(p, states);
    unsigned source = step;
    if (states[step] == GATE_TIED) {
        source = (step + p->length[ACID_GATE] - 1) % p->length[ACID_GATE];
        p->steps[ACID_GATE][source] = GATE_ON;
        p->gate_priority = step; // Restore a covered long note before resolving other ties.
    } else {
        p->steps[ACID_GATE][step] = long_note ? GATE_LONG :
                                     states[step] == GATE_OFF ? GATE_ON : GATE_OFF;
        if (p->steps[ACID_GATE][step] == GATE_LONG) p->gate_priority = step;
    }
    // End a removed live tie at the next step so a restored On can attack there.
    uint8_t updated[16]; acid_gate_states(p, updated);
    if (a->tie_steps && a->tied_step == source && updated[source] != GATE_LONG) a->tie_steps = 1;
    for (unsigned i = 0; i < 16; ++i)
        if (i == step || i == source || states[i] != updated[i]) a->working[ACID_GATE][i] = updated[i];
    g->dirty = true;
}
static void gate_key(Grid *g, unsigned step, bool down, uint32_t now) {
    Acid *a = &g->acid;
    uint16_t bit = 1u << step;
    if (down) {
        if (a->gate_down & bit) return;
        a->gate_down |= bit; a->gate_hold_done &= ~bit; a->gate_at[step] = now;
    } else if (a->gate_down & bit) {
        a->gate_down &= ~bit;
        if (!(a->gate_hold_done & bit))
            edit_gate(g, step, (uint32_t)(now - a->gate_at[step]) >= ACID_HOLD_MS);
    }
}
static void gate_holds(Grid *g, uint32_t now) {
    Acid *a = &g->acid;
    if (a->scale_page) { a->gate_down = 0; return; }
    for (unsigned step = 0; step < ACID_STEPS; ++step) {
        uint16_t bit = 1u << step;
        if (!(a->gate_down & bit) || (a->gate_hold_done & bit)) continue;
        if ((uint32_t)(now - a->gate_at[step]) < ACID_HOLD_MS) continue;
        edit_gate(g, step, true); a->gate_hold_done |= bit;
    }
}
static void edit_step(Grid *g, unsigned lane, unsigned step, bool down) {
    Acid *a = &g->acid;
    if (a->length_select == (int)lane) {
        if (!down) return;
        pattern(g)->length[lane] = step + 1;
        refresh_lane(g, lane);
        a->length_select = -1; g->dirty = true;
        return;
    }
    if (lane == ACID_GATE) { gate_key(g, step, down, g->now); return; }
    if (!down) return;
    static const uint8_t states[ACID_LANES] = {3, 2, 2, 4, 6, 3};
    AcidPattern *p = pattern(g);
    p->steps[lane][step] = (p->steps[lane][step] + 1) % states[lane];
    if (lane < 3) g->acid.working[lane][step] = p->steps[lane][step];
    refresh_lane(g, lane);
    g->dirty = true;
}
void acid_key(Grid *g, unsigned x, unsigned y, bool down, uint32_t now) {
    if (x > 9 || y > 9) return;
    g->now = now; Acid *a = &g->acid;
    if (x == 0 && y == 9) { reset_key(g, down, now); return; }
    if (a->scale_page) { scale_page_key(g, x, y, down, now); return; }
    if (x == 0) {
        if (!down) return;
        if (y < 8) a->pending_pattern = y == a->patch.selected ? -1 : (int)y;
        else a->pending_transport = a->pending_transport >= 0 ? -1 : !g->running;
        return;
    }
    if (x == 9) { if (down) right_control(g, y); return; }
    unsigned lane, step;
    if (!acid_coordinate(x, y, &lane, &step)) return;
    edit_step(g, lane, step, down);
}
static bool release_note(Grid *g, unsigned slot) {
    ActiveNote *n = &g->notes[slot];
    if (!n->active) return true;
    if (!grid_queue_midi(g, 0x80 | (n->channel - 1), n->note, 0)) return false;
    n->active = false;
    return true;
}
static bool gate_attack(Grid *g, unsigned step) {
    Acid *a = &g->acid;
    if (a->tie_steps) {
        if (--a->tie_steps) return false;
        if (!a->sliding) release_note(g, 0);
    }
    uint8_t value = a->working[ACID_GATE][step];
    return value == GATE_ON || value == GATE_LONG;
}
static unsigned note_lifetime(Grid *g, unsigned step, bool slide) {
    Acid *a = &g->acid; AcidPattern *p = pattern(g);
    unsigned duration = 6 * a->pulse_ms;
    // Slide extends through rests to the following sounding note, with a bounded safety tail.
    bool long_note = a->working[ACID_GATE][step] == GATE_LONG;
    unsigned tail = p->length[ACID_GATE] < 3 ? 3 : p->length[ACID_GATE];
    // A long note ends on the second clock boundary; due is only a lost-clock watchdog.
    unsigned length = slide ? duration * tail + a->patch.overlap_ms :
                      long_note ? duration * 3 : duration * a->patch.length_percent / 100;
    if (long_note) { a->tie_steps = 2; a->tied_step = step; }
    return length ? length : 1;
}
static void play_note(Grid *g, unsigned step) {
    Acid *a = &g->acid; AcidPattern *p = pattern(g);
    if (!gate_attack(g, step)) return;
    if (g->tx_count > GRID_TX_CAP - 8) { ++g->dropped_notes; return; }
    unsigned b = a->head[ACID_ACCENT], c = a->head[ACID_SLIDE], e = a->head[ACID_OCTAVE];
    int pitch = p->pitch[step] + 12 * p->octave[e];
    bool accent = a->working[ACID_ACCENT][b], slide = a->working[ACID_SLIDE][c];
    uint8_t channel = a->patch.channel;
    ActiveNote old = g->notes[0];
    bool legato = a->sliding && old.active && old.channel == channel;
    if (!legato) release_note(g, 0);
    if (a->patch.accent_cc >= 0)
        grid_queue_midi(g, 0xb0 | (channel - 1), a->patch.accent_cc, accent ? 127 : 0);
    if (!legato || old.note != pitch) {
        // Retire an older overlap before attacking: A>B>A must not release the new A.
        release_note(g, 1);
        grid_queue_midi(g, 0x90 | (channel - 1), pitch,
                        accent ? a->patch.accent_velocity : a->patch.velocity);
        a->echoes[a->echo_next++ % 32] = (AcidEcho){g->now, pitch, channel, true};
        if (legato) {
            old.due = g->now + a->patch.overlap_ms; g->notes[1] = old;
        }
    }
    unsigned length = note_lifetime(g, step, slide);
    g->notes[0] = (ActiveNote){channel, pitch, g->now + (length ? length : 1), true};
    a->sliding = slide;
}
static void evolve_binary(Grid *g, unsigned lane, unsigned step) {
    Acid *a = &g->acid; AcidPattern *p = pattern(g);
    static const uint8_t chances[4] = {0, 5, 15, 35};
    uint8_t value = p->steps[lane][step];
    if (lane == ACID_GATE) {
        uint8_t states[16]; acid_gate_states(p, states); value = states[step];
        if (value >= GATE_LONG) { a->working[lane][step] = value; return; }
    }
    unsigned chance = chances[p->intensity[lane]];
    if (lane == ACID_GATE && step % 4 == 0 && value) chance /= 2;
    if (chance && acid_random(p, 100) < chance) value = !value;
    a->working[lane][step] = value;
}
static void evolve_octave(Grid *g, unsigned step) {
    AcidPattern *p = pattern(g);
    if (p->pitch_lock) return;
    uint8_t policy = acid_policy(p->steps[ACID_OCTAVE], p->length[ACID_OCTAVE], step, OCT_BASE);
    int value = 0;
    if (policy == OCT_UP || policy == OCT_UP_OR_BASE) value = 1;
    if (policy == OCT_DOWN || policy == OCT_DOWN_OR_BASE) value = -1;
    if ((policy == OCT_UP_OR_BASE || policy == OCT_DOWN_OR_BASE) && acid_random(p, 2)) value = 0;
    if (p->octave[step] != value) g->dirty = true;
    p->octave[step] = value;
}
static bool pattern_boundary(const Grid *g) {
    const Acid *a = &g->acid;
    if (!g->running) return a->pulses % 96 == 0;
    return a->head[0] + 1 >= a->patch.patterns[a->patch.selected].length[ACID_GATE];
}
static void switch_pattern(Grid *g) {
    Acid *a = &g->acid;
    grid_panic(g); a->patch.selected = a->pending_pattern; a->pending_pattern = -1;
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) a->head[lane] = pattern(g)->length[lane] - 1;
    a->length_select = -1;
    for (unsigned l = 0; l < 3; ++l) memcpy(a->working[l], pattern(g)->steps[l], 16);
    acid_gate_states(pattern(g), a->working[ACID_GATE]);
    g->dirty = true;
}
static void advance_lanes(Grid *g) {
    Acid *a = &g->acid; AcidPattern *p = pattern(g);
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) {
        // Shrinking past the playhead wraps cleanly on the next clock-derived step.
        a->head[lane] = a->head[lane] + 1 >= p->length[lane] ? 0 : a->head[lane] + 1;
        if (lane < 3) evolve_binary(g, lane, a->head[lane]);
    }
    evolve_octave(g, a->head[ACID_OCTAVE]);
    static const uint8_t values[3] = {0, 64, 127};
    if (g->tx_count < GRID_TX_CAP - 8)
        grid_queue_midi(g, 0xb0 | (a->patch.channel - 1), ACID_SEQUENCE_CC,
                        values[p->steps[ACID_CC][a->head[ACID_CC]]]);
    uint8_t policy = acid_policy(p->steps[ACID_PITCH], p->length[ACID_PITCH],
                                 a->head[ACID_PITCH], PITCH_GREEN);
    acid_mutate(g, a->head[0], policy);
    play_note(g, a->head[0]); ++g->tick;
}
void acid_clock(Grid *g) {
    Acid *a = &g->acid;
    if (a->pulses % 6 == 0) {
        if (a->pending_reset) acid_reset(g);
        if (a->pending_transport >= 0) {
            bool play = a->pending_transport; a->pending_transport = -1;
            if (play) g->running = true; else acid_stop(g);
        }
        if (a->pending_pattern >= 0 && pattern_boundary(g)) switch_pattern(g);
        if (g->running) advance_lanes(g);
    }
    if (a->tie_steps && g->notes[0].active)
        g->notes[0].due = g->now + 24 * a->pulse_ms + a->patch.overlap_ms;
    ++a->pulses;
}
static bool is_echo(const Acid *a, const uint8_t *d, uint32_t now) {
    if (!a->learner.echo_ms) return false;
    for (unsigned i = 0; i < 32; ++i) {
        const AcidEcho *e = &a->echoes[i];
        if (e->valid && e->note == d[1] && e->channel == (d[0] & 15) + 1 &&
            (uint32_t)(now - e->at) <= a->learner.echo_ms) return true;
    }
    return false;
}
static bool realtime(Grid *g, uint8_t status, uint32_t now) {
    Acid *a = &g->acid;
    switch (status) {
    case 0xfa: acid_start(g, true); return true;
    case 0xfb: acid_start(g, false); return true;
    case 0xfc: acid_stop(g); return true;
    case 0xff: acid_stop(g); acid_reset(g); return true;
    case 0xf8:
        if (a->last_clock) {
            unsigned elapsed = now - a->last_clock;
            if (elapsed > 0 && elapsed < 500) a->pulse_ms = elapsed;
        }
        a->last_clock = now;
        if (!a->patch.internal_clock) acid_clock(g);
        return true;
    default: return false;
    }
}
void acid_midi(Grid *g, const uint8_t *d, size_t length, uint32_t now) {
    if (!length) return;
    g->now = now; ++g->midi_in_count;
    if (realtime(g, d[0], now)) return;
    if (length == 3 && (d[0] & 0xf0) == 0x90 && d[1] < 128 && d[2] > 0 && d[2] < 128)
        acid_learn(g, d[1], now, is_echo(&g->acid, d, now));
}
static void scale_hold(Grid *g, uint32_t now) {
    Acid *a = &g->acid;
    if (!a->scale_page || a->scale_held < 0 || a->scale_hold_done) return;
    if ((uint32_t)(now - a->scale_at) < ACID_HOLD_MS) return;
    acid_root(g, a->scale_held); a->scale_hold_done = true;
}
void acid_poll(Grid *g, uint32_t now) {
    g->now = now; Acid *a = &g->acid;
    acid_learn_flush(g, now);
    scale_hold(g, now);
    if (a->reset_down && !a->hold_opened && (uint32_t)(now - a->reset_at) >= ACID_HOLD_MS) {
        a->scale_page = true; a->hold_opened = true; a->length_select = -1;
    }
    gate_holds(g, now);
    for (unsigned i = 0; i < GRID_MAX_NOTES; ++i)
        if (g->notes[i].active && (int32_t)(now - g->notes[i].due) >= 0) {
            if (release_note(g, i) && !i) a->sliding = false;
        }
    if (!a->patch.internal_clock) { a->initialized_clock = false; return; }
    unsigned divisor = a->patch.bpm * 24u;
    a->pulse_ms = 60000u / divisor;
    if (!a->initialized_clock) {
        a->next_ms = now; a->remainder = 0; a->initialized_clock = true;
    }
    // Fixed-point millisecond deadlines preserve fractional BPM phase. Bound catch-up after stalls.
    unsigned budget = 24;
    while (budget-- && (int32_t)(now - a->next_ms) >= 0) {
        acid_clock(g);
        a->remainder += 60000;
        a->next_ms += a->remainder / divisor; a->remainder %= divisor;
    }
    if ((int32_t)(now - a->next_ms) >= 0) a->next_ms = now + a->pulse_ms;
}
static Color brighten(Color c) {
    c.r = c.r / 2 + 110; c.g = c.g / 2 + 110; c.b = c.b / 2 + 110; return c;
}
static Color step_color(unsigned lane, uint8_t value) {
    static const Color colors[6] = {{5, 5, 7}, {8, 125, 25}, {145, 115, 0}, {150, 6, 4}, {160, 55, 0}, {150, 6, 4}};
    static const Color octave[6] = {{5, 5, 7}, {0, 30, 160}, {0, 140, 150}, {145, 115, 0}, {160, 55, 0}, {150, 6, 4}};
    if (lane == ACID_GATE && value >= GATE_LONG)
        return value == GATE_LONG ? (Color){145, 115, 0} : (Color){150, 6, 4};
    if (lane == ACID_OCTAVE) return octave[value];
    if (lane == ACID_CC) {
        static const Color cc[3] = {{150, 6, 4}, {145, 115, 0}, {8, 125, 25}};
        return cc[value];
    }
    if (lane == ACID_PITCH) return colors[value];
    static const Color on[3] = {{5, 135, 65}, {150, 60, 0}, {70, 35, 160}};
    return value ? on[lane] : colors[0];
}
static void render_scale(const AcidPatch *p, Color colors[100]) {
    for (unsigned y = 0; y < 10; ++y)
        for (unsigned x = 0; x < 10; ++x) {
            int pc = acid_scale_key(x, y);
            if (pc >= 0) colors[y * 10 + x] = p->scale & (1u << pc) ?
                (Color){25, 160, 65} : y == 5 ? (Color){35, 35, 40} : (Color){8, 8, 25};
        }
    for (unsigned y = 4; y <= 5; ++y) for (unsigned x = 1; x <= 7; ++x) {
        int pc = acid_scale_key(x, y);
        if (pc == p->patterns[p->selected].root && (p->scale & (1u << pc)))
            colors[y * 10 + x] = (Color){180, 180, 150};
    }
    colors[9] = (Color){20, 100, 100}; colors[19] = (Color){30, 50, 130};
    colors[89] = (Color){120, 80, 20}; colors[99] = (Color){90, 20, 120};
    colors[90] = (Color){120, 30, 140};
}
static void render_lane(const Grid *g, unsigned lane, Color colors[100]) {
    const Acid *a = &g->acid; const AcidPattern *p = &a->patch.patterns[a->patch.selected];
    uint8_t gates[16];
    if (lane == ACID_GATE) acid_gate_states(p, gates);
    for (unsigned step = 0; step < acid_lane_capacity[lane]; ++step) {
        unsigned cell = (acid_lane_rows[lane] + step / 8) * 10 + step % 8 + 1;
        Color color = step_color(lane, lane == ACID_GATE ? gates[step] : p->steps[lane][step]);
        if (step >= p->length[lane]) { color.r /= 4; color.g /= 4; color.b /= 4; }
        else if (g->running && a->head[lane] == step) color = brighten(color);
        colors[cell] = color;
    }
    Color control = {0, 85, 100};
    if (a->length_select == (int)lane)
        control = (g->now / 180) % 2 ? (Color){220, 140, 15} : (Color){35, 20, 3};
    colors[acid_lane_rows[lane] * 10 + 9] = control;
    if (lane < 3) {
        static const Color intensities[4] = {{12, 12, 15}, {0, 80, 40}, {140, 100, 0}, {155, 0, 20}};
        colors[lane * 20 + 19] = intensities[p->intensity[lane]];
    }
    if (lane == ACID_PITCH)
        colors[79] = p->pitch_lock ? (Color){185, 15, 20} : (Color){0, 65, 75};
}
void acid_render(const Grid *g, Color *colors) {
    const Acid *a = &g->acid;
    for (unsigned i = 0; i < 100; ++i) colors[i] = (Color){3, 3, 5};
    if (a->scale_page) {
        render_scale(&a->patch, colors);
        colors[29] = a->patch.internal_clock ? (Color){0, 110, 60} : (Color){10, 30, 155};
        return;
    }
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) render_lane(g, lane, colors);
    for (unsigned i = 0; i < 8; ++i) colors[i * 10] = i == a->patch.selected ?
        (Color){15, 160, 100} : (int)i == a->pending_pattern ? (Color){180, 100, 0} : (Color){8, 15, 20};
    colors[80] = a->pending_transport >= 0 ? (Color){160, 100, 0} :
                 g->running ? (Color){0, 150, 30} : (Color){110, 10, 12};
    colors[90] = a->pending_reset ? (Color){175, 90, 0} : (Color){45, 35, 65};
}
