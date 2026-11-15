// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static Grid g, other;
static AcidPattern *p(void) { return &g.acid.patch.patterns[g.acid.patch.selected]; }
static void init(unsigned seed) {
    grid_init(&g, seed); g.acid.patch.internal_clock = false;
    g.acid.patch.green_chance = g.acid.patch.yellow_chance = 0;
}
static void drain(void) { while (grid_tx_peek(&g)) grid_tx_ack(&g); }
static void clocks(unsigned n) {
    const uint8_t clock = 0xf8;
    for (unsigned i = 0; i < n; ++i) { grid_midi(&g, &clock, 1, g.now + 21); grid_poll(&g, g.now); }
}
static void tap(unsigned x, unsigned y) {
    grid_key(&g, x, y, true, g.now); grid_key(&g, x, y, false, g.now + 1);
}
static void test_layout(void) {
    init(123); unsigned count = 0, sizes[ACID_LANES] = {0};
    for (unsigned y = 0; y < 10; ++y) for (unsigned x = 0; x < 10; ++x) {
        unsigned lane = 99, step = 99; bool valid = acid_coordinate(x, y, &lane, &step);
        assert(valid == (x >= 1 && x <= 8));
        if (valid) {
            assert(lane == (y < 8 ? y / 2 : y - 4));
            assert(step == (y < 8 ? y % 2 * 8 : 0) + x - 1);
            ++count; ++sizes[lane];
        }
    }
    assert(count == 80);
    const unsigned states[6] = {2, 2, 2, 4, 6, 3};
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) {
        unsigned last = acid_lane_capacity[lane] - 1;
        assert(sizes[lane] == acid_lane_capacity[lane] && p()->length[lane] == sizes[lane]);
        uint8_t before = p()->steps[lane][last];
        tap(8, acid_lane_rows[lane] + last / 8);
        assert(p()->steps[lane][last] == (before + 1) % states[lane]);
        tap(9, acid_lane_rows[lane]); assert(g.acid.length_select == (int)lane);
        before = p()->steps[lane][last]; tap(8, acid_lane_rows[lane] + last / 8);
        assert(g.acid.length_select == -1 && p()->steps[lane][last] == before);
    }
    tap(9, 1); assert(p()->intensity[0] == 1);
    tap(9, 7); assert(p()->pitch_lock);
    tap(9, 9); assert(g.acid.length_select == ACID_CC && p()->pitch_lock);
}
static void test_policies(void) {
    uint8_t steps[16] = {0};
    for (unsigned i = 0; i < 16; ++i) assert(acid_policy(steps, 16, i, PITCH_GREEN) == PITCH_GREEN);
    assert(acid_policy(steps, 16, 0, OCT_BASE) == OCT_BASE);
    steps[15] = PITCH_RED; steps[5] = PITCH_YELLOW;
    for (unsigned i = 0; i < 5; ++i) assert(acid_policy(steps, 16, i, PITCH_GREEN) == PITCH_RED);
    for (unsigned i = 5; i < 15; ++i) assert(acid_policy(steps, 16, i, PITCH_GREEN) == PITCH_YELLOW);
}
static void test_short_policy_wrap(void) {
    uint8_t steps[16] = {0};
    steps[15] = PITCH_RED; // Outside a shortened lane: must not contribute inheritance.
    assert(acid_policy(steps, 7, 0, PITCH_GREEN) == PITCH_GREEN);
    steps[6] = PITCH_YELLOW;
    for (unsigned i = 0; i < 7; ++i) assert(acid_policy(steps, 7, i, PITCH_GREEN) == PITCH_YELLOW);
    memset(steps, 0, sizeof(steps)); steps[7] = OCT_DOWN;
    assert(acid_policy(steps, 8, 0, OCT_BASE) == OCT_DOWN);
    assert(acid_policy(steps, 7, 0, OCT_BASE) == OCT_BASE);
    assert(acid_policy(steps, 1, 0, OCT_BASE) == OCT_BASE);
}
static void test_transport_and_patterns(void) {
    init(99); clocks(1); tap(0, 8); assert(!g.running && g.acid.pending_transport == 1);
    Color frame[100]; grid_render(&g, frame); assert(frame[80].r > frame[80].g);
    clocks(5); assert(!g.running); clocks(1); assert(g.running && g.acid.head[0] == 0);
    tap(0, 3); assert(g.acid.pending_pattern == 3 && g.acid.patch.selected == 0);
    uint8_t remembered[16]; memcpy(remembered, p()->pitch, 16);
    clocks(95); assert(g.acid.patch.selected == 0);
    clocks(1); assert(g.acid.patch.selected == 3 && g.acid.head[0] == 0);
    assert(!memcmp(remembered, g.acid.patch.patterns[0].pitch, 16));
    tap(0, 8); clocks(6); assert(!g.running);
    for (unsigned i = 0; i < GRID_MAX_NOTES; ++i) assert(!g.notes[i].active);
    tap(0, 9); assert(g.acid.pending_reset); clocks(6);
    assert(!g.acid.pending_reset && g.acid.head[0] == 15);
    tap(0, 8); clocks(6); assert(g.running && g.acid.head[0] == 0);
}
static void test_lengths(void) {
    init(21); tap(9, 0);
    Color bright[100], dim[100]; g.now = 0; grid_render(&g, dim); g.now = 180; grid_render(&g, bright);
    assert(bright[9].r > dim[9].r);
    uint8_t value = p()->steps[0][14]; tap(7, 1); // A: step 15 becomes endpoint.
    assert(p()->length[0] == 15 && p()->steps[0][14] == value && g.acid.length_select == -1);
    tap(9, 2); tap(7, 2); assert(p()->length[1] == 7);
    tap(9, 8); tap(3, 8); assert(p()->length[4] == 3);
    tap(9, 9); tap(5, 9); assert(p()->length[5] == 5);
    // Re-press cancels; choosing another length control changes the armed lane.
    tap(9, 4); tap(9, 4); assert(g.acid.length_select == -1);
    tap(9, 4); tap(9, 6); assert(g.acid.length_select == ACID_PITCH);
    tap(9, 6); grid_start(&g, true); clocks(1); drain();
    assert(!g.acid.head[0] && !g.acid.head[1]);
    for (unsigned step = 1; step <= 105; ++step) {
        clocks(6); drain();
        assert(g.acid.head[0] == step % 15 && g.acid.head[1] == step % 7);
        assert(g.acid.head[4] == step % 3 && g.acid.head[5] == step % 5);
        if (step < 105) assert(g.acid.head[0] || g.acid.head[1]);
    }
    // A live shrink past the head wraps at the next step without extra attacks.
    clocks(60); drain(); assert(g.acid.head[0] == 10);
    tap(9, 0); tap(3, 0); assert(g.acid.head[0] == 10 && p()->length[0] == 3);
    uint64_t tick = g.tick; clocks(6); drain(); assert(!g.acid.head[0] && g.tick == tick + 1);
    // All six lanes can be one step, and expansion restores the hidden base steps.
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) { tap(9, acid_lane_rows[lane]); tap(1, acid_lane_rows[lane]); }
    clocks(12); drain(); for (unsigned lane = 0; lane < ACID_LANES; ++lane) assert(!g.acid.head[lane]);
    tap(9, 0); tap(8, 1); assert(p()->length[0] == 16 && p()->steps[0][14] == value);
    // Pattern switching follows the shortened Gate phrase, never a fixed 16-step bar.
    init(2); p()->length[0] = 3; grid_start(&g, true); clocks(1); drain(); tap(0, 1);
    clocks(17); drain(); assert(g.acid.patch.selected == 0);
    clocks(1); assert(g.acid.patch.selected == 1 && !g.acid.head[0]); drain();
}
static void test_scale_editor(void) {
    init(123); grid_start(&g, true); clocks(20);
    unsigned tick = g.tick; grid_key(&g, 0, 9, true, g.now);
    grid_poll(&g, g.now + ACID_HOLD_MS);
    assert(g.acid.scale_page && !g.acid.pending_reset && g.tick == tick);
    grid_key(&g, 0, 9, false, g.now); assert(g.acid.scale_page && !g.acid.pending_reset);
    bool seen[12] = {false};
    for (unsigned y = 0; y < 10; ++y) for (unsigned x = 0; x < 10; ++x) {
        int pc = acid_scale_key(x, y); if (pc >= 0) { assert(!seen[pc]); seen[pc] = true; }
    }
    for (unsigned i = 0; i < 12; ++i) assert(seen[i]);
    assert(!acid_scale(&g, 0)); assert(acid_scale(&g, 1u << 1));
    for (unsigned i = 0; i < 16; ++i) assert(p()->pitch[i] % 12 == 1 && p()->base_pitch[i] % 12 == 1);
    for (unsigned pattern = 0; pattern < ACID_PATTERNS; ++pattern)
        for (unsigned i = 0; i < 16; ++i) {
            const AcidPattern *q = &g.acid.patch.patterns[pattern];
            assert(q->pitch[i] % 12 == 1 && q->base_pitch[i] % 12 == 1);
        }
    tap(2, 4); assert(g.acid.patch.scale == (1u << 1)); // Cannot disable last note.
    tap(0, 9); assert(!g.acid.scale_page && !g.acid.pending_reset);
    tap(0, 9); assert(g.acid.pending_reset);
}
static void test_generation_and_memory(void) {
    unsigned leaps = 0, repetitions = 0;
    for (unsigned seed = 1; seed <= 100; ++seed) {
        init(seed); grid_init(&other, seed);
        assert(!memcmp(p()->pitch, other.acid.patch.patterns[0].pitch, 16));
        for (unsigned i = 0; i < 16; ++i) {
            assert(g.acid.patch.scale & (1u << (p()->pitch[i] % 12)));
            assert(p()->pitch[i] >= 36 && p()->pitch[i] <= 59);
            if (p()->pitch[i] == p()->pitch[(i + 1) % 16]) ++repetitions;
            if (abs((int)p()->pitch[i] - p()->pitch[(i + 1) % 16]) >= 7) ++leaps;
        }
    }
    assert(leaps > 100 && repetitions > 200);
    init(41); uint8_t phrase[16], base[16]; memcpy(phrase, p()->pitch, 16); memcpy(base, p()->base_pitch, 16);
    grid_start(&g, true); for (unsigned i = 0; i < 4; ++i) { clocks(96); drain(); }
    assert(!memcmp(phrase, p()->pitch, 16));
    g.acid.patch.green_chance = 25; memset(p()->steps[0], 0, 16);
    for (unsigned i = 0; i < 8; ++i) clocks(96);
    assert(g.acid.mutation_count > 0 && g.acid.mutation_count < 80);
    assert(memcmp(phrase, p()->pitch, 16) && !memcmp(base, p()->base_pitch, 16));
    memcpy(phrase, p()->pitch, 16); p()->pitch_lock = true;
    clocks(192); assert(!memcmp(phrase, p()->pitch, 16));
    p()->pitch_lock = false; memset(p()->steps[3], PITCH_RED, 16);
    clocks(192); assert(!memcmp(phrase, p()->pitch, 16));
    acid_restore(&g); assert(!memcmp(base, p()->pitch, 16));
}
static void test_combined_lock_and_evolution(void) {
    init(52); memset(p()->steps[0], 1, 16); memset(p()->steps[4], OCT_UP_OR_BASE, 8);
    uint8_t phrase[16]; memcpy(phrase, p()->pitch, 16);
    p()->intensity[0] = 3; grid_start(&g, true); clocks(96); drain();
    bool changed = false; for (unsigned i = 0; i < 8; ++i) if (p()->octave[i]) changed = true;
    assert(changed); int8_t octave[8]; memcpy(octave, p()->octave, 8);
    tap(9, 7); assert(p()->pitch_lock); g.acid.patch.green_chance = 100;
    for (unsigned i = 0; i < 4; ++i) { clocks(96); drain(); }
    assert(!memcmp(phrase, p()->pitch, 16) && !memcmp(octave, p()->octave, 8));
    tap(9, 7); assert(!p()->pitch_lock);
    for (unsigned i = 0; i < 4; ++i) { clocks(96); drain(); }
    assert(memcmp(phrase, p()->pitch, 16) && memcmp(octave, p()->octave, 8));
    for (unsigned i = 0; i < 16; ++i) assert(p()->steps[0][i] == 1);
    unsigned changes = 0;
    for (unsigned i = 0; i < 16; ++i) changes += g.acid.working[0][i] != 1;
    assert(changes > 0);
    p()->intensity[0] = 0; clocks(96); drain();
    for (unsigned i = 0; i < 16; ++i) assert(g.acid.working[0][i] == 1);
    init(42); g.acid.patch.green_chance = 100; p()->length[3] = 7;
    memset(p()->steps[3], PITCH_RED, 16); p()->steps[3][0] = PITCH_GREEN;
    uint8_t before[16]; memcpy(before, p()->pitch, 16);
    grid_start(&g, true); clocks(43); drain();
    for (unsigned i = 1; i < 7; ++i) assert(before[i] == p()->pitch[i]);
    for (unsigned i = 0; i < g.acid.mutation_count; ++i)
        assert(g.acid.mutations[i].step == 0 || g.acid.mutations[i].step == 7);
}
static void test_cc_lane(void) {
    init(91); memset(p()->steps[0], 0, 16); p()->pitch_lock = true; p()->length[ACID_CC] = 3;
    p()->steps[ACID_CC][0] = 0; p()->steps[ACID_CC][1] = 1; p()->steps[ACID_CC][2] = 2;
    static const uint8_t values[] = {0, 64, 127};
    Color frame[100]; grid_render(&g, frame);
    assert(frame[91].r > frame[91].g && frame[92].r > frame[92].b && frame[93].g > frame[93].r);
    grid_start(&g, true);
    for (unsigned i = 0; i < 9; ++i) {
        clocks(i ? 6 : 1); const MidiMessage *m = grid_tx_peek(&g);
        assert(g.tx_count == 1 && m->bytes[0] == 0xb0 && m->bytes[1] == ACID_SEQUENCE_CC);
        assert(m->bytes[2] == values[i % 3]); drain();
    }
    grid_stop(&g); clocks(12); assert(!g.tx_count);
    // The CC row has three states and no inherited/off state.
    tap(1, 9); assert(p()->steps[ACID_CC][0] == 1);
    tap(1, 9); tap(1, 9); assert(p()->steps[ACID_CC][0] == 0);
}
static const MidiMessage *peek_note(void) {
    while (grid_tx_peek(&g) && (grid_tx_peek(&g)->bytes[0] & 0xf0) == 0xb0) grid_tx_ack(&g);
    return grid_tx_peek(&g);
}
static void note(uint8_t n, unsigned time) {
    const uint8_t d[] = {0x90, n, 100}; grid_midi(&g, d, 3, time);
}
static void test_learning(void) {
    init(34); uint8_t phrase[16]; memcpy(phrase, p()->pitch, 16);
    double before = acid_pitch_weight(&g.acid.learner, 6, 0);
    for (unsigned i = 0; i < 240; ++i) note(i % 2 ? 66 : 64, i * 30);
    acid_learn_flush(&g, 10000);
    assert(!g.running && !memcmp(phrase, p()->pitch, 16));
    assert(g.acid.learner.long_term.interval[26] == 120);
    assert(g.acid.learner.long_term.interval[22] == 119);
    assert(acid_pitch_weight(&g.acid.learner, 6, 0) > before);
    double conf[3]; acid_confidence(&g.acid.learner, conf);
    assert(conf[0] < .81 && conf[0] > .5 && conf[1] > .4 && conf[2] < conf[0]);
    bool motif = false;
    for (unsigned i = 0; i < ACID_MOTIFS; ++i)
        if (g.acid.learner.motifs[i].length == 3 && g.acid.learner.motifs[i].count > 20) motif = true;
    assert(motif);
    acid_generate(&g, 0);
    for (unsigned i = 0; i < 16; ++i) assert(g.acid.patch.scale & (1u << (p()->pitch[i] % 12)));
    init(20); note(60, 0); note(64, 2); note(67, 4); acid_learn_flush(&g, 20);
    assert(g.acid.learner.long_term.observations == 3 && !g.acid.learner.long_term.transitions);
    note(62, 100); acid_learn_flush(&g, 120); assert(!g.acid.learner.long_term.transitions);
    note(60, 140); acid_learn_flush(&g, 160); assert(g.acid.learner.long_term.interval[22] == 1);
    acid_learn(&g, 90, 200, true); assert(g.acid.learner.long_term.observations == 5);
    g.acid.patch.learning = false; note(70, 250); assert(g.acid.learner.long_term.observations == 5);
}
static void test_recent_vocabulary(void) {
    init(303); unsigned time = 0;
    uint8_t phrase[16]; memcpy(phrase, p()->pitch, 16);
    for (unsigned i = 0; i < 1100; ++i) {
        note(i < 1000 ? 60 : 67, time); time += 20; acid_learn_flush(&g, time);
    }
    double latest = acid_pitch_weight(&g.acid.learner, 7, 0);
    assert(latest > acid_pitch_weight(&g.acid.learner, 0, 0));
    assert(!g.running && !memcmp(phrase, p()->pitch, 16));
    acid_learn_flush(&g, time + 1000000);
    assert(latest == acid_pitch_weight(&g.acid.learner, 7, 0)); // Silence keeps the vocabulary.
    init(303); time = 0;
    for (unsigned i = 0; i < 1200; ++i) {
        note(i < 1000 ? 60 + 2 * (i % 3) : 67 - 2 * (i % 3), time);
        time += 20; acid_learn_flush(&g, time);
    }
    assert(acid_interval_weight(&g.acid.learner, -2) > acid_interval_weight(&g.acid.learner, 2));
    double old = 0, recent = 0;
    for (unsigned i = 0; i < ACID_MOTIFS; ++i) {
        const AcidMotif *m = &g.acid.learner.motifs[i];
        if (m->length != 3) continue;
        if (m->intervals[0] == 2 && m->intervals[1] == 2) old = acid_motif_weight(&g.acid.learner, i);
        if (m->intervals[0] == -2 && m->intervals[1] == -2) recent = acid_motif_weight(&g.acid.learner, i);
    }
    assert(recent > old && old > 0);
}
static void test_motif_continuation(void) {
    init(303); assert(acid_scale(&g, 0xfff));
    memset(g.acid.patch.strategy, 0, 5); g.acid.patch.strategy[4] = 100;
    g.acid.patch.green_chance = 100; p()->anchors = 0;
    memset(p()->steps[ACID_GATE], 1, 16);
    memset(p()->pitch, 36, 16); p()->pitch[1] = 38; p()->pitch[2] = 40;
    g.acid.learner.motifs[0] = (AcidMotif){.length = 4, .intervals = {2, 2, 7}, .count = 200, .recent = 200};
    g.acid.learner.motifs[1] = (AcidMotif){.length = 4, .intervals = {1, 1, 12}, .count = 1000, .recent = 1000};
    unsigned continuations = 0;
    for (unsigned i = 0; i < 1000; ++i) {
        p()->pitch[3] = 40; acid_mutate(&g, 3, PITCH_GREEN);
        assert(p()->pitch[3] == 40 || p()->pitch[3] == 47);
        continuations += p()->pitch[3] == 47;
    }
    assert(continuations > 250); // Follow +2,+2 with +7; never splice the incompatible motif.
}
static void test_bassline_structure(void) {
    unsigned initial[12] = {0}, evolved[12] = {0}, attacks = 0, octave_jumps = 0;
    unsigned changed = 0, home_starts = 0;
    for (unsigned seed = 1; seed <= 200; ++seed) {
        grid_init(&g, seed);
        for (unsigned step = 0; step < 16; ++step) {
            octave_jumps += abs((int)p()->pitch[step] - p()->pitch[(step + 15) % 16]) >= 12;
            if (!p()->steps[ACID_GATE][step]) continue;
            ++initial[p()->pitch[step] % 12]; ++attacks;
        }
        for (unsigned visit = 0; visit < 16 * 64; ++visit) {
            unsigned step = visit % 16; uint8_t before = p()->pitch[step];
            acid_mutate(&g, step, PITCH_GREEN); changed += before != p()->pitch[step];
        }
        for (unsigned step = 0; step < 16; ++step)
            if (p()->steps[ACID_GATE][step]) ++evolved[p()->pitch[step] % 12];
        home_starts += p()->pitch[0] % 12 == 0 || p()->pitch[0] % 12 == 7;
    }
    assert(initial[0] > attacks * .4 && initial[7] > attacks * .1);
    assert(evolved[0] > attacks * .35 && evolved[7] > attacks * .1);
    assert(evolved[0] + evolved[7] > attacks * .7);
    assert(evolved[0] + evolved[7] < attacks * .95); // Leave room for color tones.
    assert(home_starts > 150 && octave_jumps < 3200 * .15);
    assert(changed > 1000 && changed < 200 * 16 * 64 * .2); // Selective persistent edits.
}
static void test_root_editor(void) {
    init(31); g.acid.scale_page = true;
    uint8_t phrase[16]; memcpy(phrase, p()->pitch, 16);
    grid_key(&g, 2, 5, true, 100); // D: tap must not disable it before hold is known.
    grid_poll(&g, 100 + ACID_HOLD_MS - 1); assert(p()->root == 0);
    grid_poll(&g, 100 + ACID_HOLD_MS); assert(p()->root == 2);
    Color frame[100]; grid_render(&g, frame); assert(frame[52].r > 150 && frame[52].b > 100);
    grid_key(&g, 2, 5, false, g.now + 1);
    assert((g.acid.patch.scale & 4) && !memcmp(phrase, p()->pitch, 16));
    for (unsigned i = 0; i < 8; ++i) assert(g.acid.patch.patterns[i].root == 2);
    // A disabled key held without intervening polls becomes an enabled root on release.
    grid_key(&g, 3, 5, true, 1000); grid_key(&g, 3, 5, false, 1000 + ACID_HOLD_MS);
    assert(p()->root == 4 && (g.acid.patch.scale & 16));
    tap(3, 5); assert(!(g.acid.patch.scale & 16) && p()->root != 4);
    assert(!acid_root(&g, 12) && acid_root(&g, 2));
    acid_generate(&g, 0); assert(p()->pitch[0] % 12 == 2);
    uint8_t bytes[ACID_PATCH_BYTES]; AcidPatch loaded;
    grid_patch_encode(&g.acid.patch, bytes); assert(grid_patch_decode(&loaded, bytes, sizeof(bytes)));
    for (unsigned i = 0; i < 8; ++i) assert(loaded.patterns[i].root == 2);
}
static void test_midi_voice(void) {
    init(45); memset(p()->steps[0], 1, 16); memset(p()->steps[1], 0, 16); p()->steps[1][0] = 1;
    p()->pitch_lock = true; p()->pitch[0] = 36; p()->pitch[1] = 43; p()->pitch[2] = 43;
    p()->steps[2][0] = 1; p()->steps[2][1] = 1;
    grid_start(&g, true); clocks(1);
    const MidiMessage *m = peek_note(); assert(m && m->bytes[0] == 0x90 && m->bytes[2] == 120);
    uint8_t echo[3]; memcpy(echo, m->bytes, 3); grid_midi(&g, echo, 3, g.now + 1);
    assert(!g.acid.learner.long_term.observations && g.acid.learner.rejected_echoes == 1);
    drain(); clocks(6); m = peek_note();
    assert(m && m->bytes[0] == 0x90 && m->bytes[1] == 43 && m->bytes[2] == 85);
    assert(g.notes[1].active && g.notes[1].note == 36); drain();
    grid_poll(&g, g.now + 18); m = peek_note();
    assert(m && m->bytes[0] == 0x80 && m->bytes[1] == 36); drain();
    clocks(6); assert(!peek_note()); // Same-pitch slide continues without retrigger.
    grid_stop(&g); assert(peek_note()->bytes[0] == 0x80); drain();
    // Ordinary articulation releases before the next attack.
    init(45); memset(p()->steps[0], 1, 16); memset(p()->steps[2], 0, 16);
    g.acid.patch.length_percent = 100; p()->pitch_lock = true; grid_start(&g, true); clocks(1); drain();
    clocks(6); assert(peek_note()->bytes[0] == 0x80); grid_tx_ack(&g);
    assert(peek_note()->bytes[0] == 0x90); drain();
    // Saturated output never blocks receive; panic retains all releases.
    while (grid_queue_midi(&g, 0x90, 36, 80)) {}
    note(70, g.now + 100); assert(g.acid.learner.long_term.observations == 1);
    grid_stop(&g); assert(g.tx_count <= 2);
    for (unsigned i = 0; i < GRID_MAX_NOTES; ++i) assert(!g.notes[i].active);
}
static void seal_fixture(uint8_t bytes[ACID_PATCH_BYTES]) {
    uint32_t hash = 2166136261u;
    for (unsigned i = 0; i < ACID_PATCH_BYTES - 4; ++i) { hash ^= bytes[i]; hash *= 16777619u; }
    for (unsigned i = 0; i < 4; ++i) bytes[ACID_PATCH_BYTES - 4 + i] = hash >> (i * 8);
}
static void legacy_fixture(uint8_t bytes[ACID_PATCH_BYTES]) {
    init(23); grid_patch_encode(&g.acid.patch, bytes);
    bytes[4] = 1; bytes[5] = 1; // Legacy bank, selected pattern has C# as its only scale note.
    memset(bytes + 32, 0, ACID_PATCH_BYTES - 36);
    for (unsigned i = 0; i < 8; ++i) {
        uint8_t *d = bytes + 32 + i * 178;
        d[0] = 1; d[64] = OCT_UP; d[72] = OCT_DOWN;
        memset(d + 80, 1, 5); // Original 1x rates.
        memset(d + 88, 36 + i % 2, 32);
        memset(d + 120, 2, 16); memset(d + 136, 1, 16);
        d[152] = 1u << (i % 2); d[154] = 1; // Scale followed by anchor mask.
        memset(d + 157, 1, 12);
        d[169] = i == 1 ? 2 : 0; // Former Octave Lock becomes the combined lock.
        d[170] = 100 + i; d[174] = 110 + i;
    }
    seal_fixture(bytes);
}
static void test_legacy_migration(void) {
    uint8_t bytes[ACID_PATCH_BYTES]; legacy_fixture(bytes); AcidPatch loaded;
    assert(grid_patch_decode(&loaded, bytes, sizeof(bytes)) && loaded.scale == 2);
    for (unsigned i = 0; i < 8; ++i) {
        const AcidPattern *q = &loaded.patterns[i];
        for (unsigned lane = 0; lane < ACID_LANES; ++lane) assert(q->length[lane] == acid_lane_capacity[lane]);
        for (unsigned step = 0; step < 16; ++step) assert(q->pitch[step] == 37 && q->base_pitch[step] == 37);
        for (unsigned step = 0; step < 8; ++step) {
            assert(q->steps[ACID_CC][step] == 1 && q->octave[step] == 0 && q->base_octave[step] == 1);
        }
    }
    assert(loaded.patterns[1].pitch_lock && loaded.patterns[0].steps[ACID_OCTAVE][0] == OCT_UP);
    assert(grid_apply_patch(&g, &loaded));
    tap(0, 5); clocks(1); assert(g.acid.patch.selected == 5 && g.acid.patch.scale == 2);
    AcidPatch unchanged = loaded;
    bytes[32 + 80] = 3; seal_fixture(bytes); // Invalid old rate is rejected rather than silently dropped.
    assert(!grid_patch_decode(&loaded, bytes, sizeof(bytes)) && !memcmp(&loaded, &unchanged, sizeof(loaded)));
}
static void test_v2_bank_migration(void) {
    init(91); uint8_t bytes[ACID_PATCH_BYTES]; grid_patch_encode(&g.acid.patch, bytes);
    bytes[4] = 2;
    for (unsigned i = 0; i < 8; ++i) memmove(bytes + 32 + i * 161, bytes + 32 + i * 162, 161);
    seal_fixture(bytes); AcidPatch loaded;
    assert(grid_patch_decode(&loaded, bytes, sizeof(bytes)));
    assert(!memcmp(loaded.patterns[7].pitch, g.acid.patch.patterns[7].pitch, 16));
    assert(!loaded.patterns[7].gate_priority && loaded.patterns[7].length[0] == 16);
    bytes[32] = GATE_LONG; seal_fixture(bytes); // Old formats never contained long Gate states.
    assert(!grid_patch_decode(&loaded, bytes, sizeof(bytes)));
}
static void test_roundtrip_and_determinism(void) {
    init(789); clocks(1); tap(9, 1); tap(9, 7); p()->octave[4] = -1;
    p()->length[0] = 15; p()->length[1] = 7; p()->length[4] = 3; p()->length[5] = 5;
    p()->steps[ACID_CC][0] = 2; assert(acid_scale(&g, 0x529));
    uint8_t data[ACID_PATCH_BYTES], encoded[ACID_PATCH_BYTES]; Patch decoded;
    assert(acid_patch_valid(&g.acid.patch));
    grid_patch_encode(&g.acid.patch, data); assert(grid_patch_decode(&decoded, data, sizeof(data)));
    grid_patch_encode(&decoded, encoded); assert(!memcmp(data, encoded, sizeof(data)));
    grid_init(&other, 12); assert(grid_apply_patch(&other, &decoded));
    grid_patch_encode(&other.acid.patch, encoded); assert(!memcmp(data, encoded, sizeof(data)));
    data[35] ^= 1; assert(!grid_patch_decode(&decoded, data, sizeof(data)));
    assert(!grid_patch_decode(&decoded, data, sizeof(data) - 1));
    // Identical performer/MIDI/clock streams produce identical generated memory.
    grid_init(&g, 1); grid_init(&other, 1);
    g.acid.patch.internal_clock = other.acid.patch.internal_clock = false;
    grid_start(&g, true); grid_start(&other, true);
    for (unsigned i = 0; i < 1000; ++i) {
        uint8_t d[] = {i % 7 ? 0xf8 : 0x90, 60 + i % 12, 100};
        grid_midi(&g, d, i % 7 ? 1 : 3, i * 21); grid_midi(&other, d, i % 7 ? 1 : 3, i * 21);
        grid_poll(&g, i * 21); grid_poll(&other, i * 21);
        drain(); while (grid_tx_peek(&other)) grid_tx_ack(&other);
    }
    assert(!memcmp(&g.acid, &other.acid, sizeof(Acid)));
    // Devices own all phrase, RNG, learning and transport state.
    acid_scale(&g, 1); assert(other.acid.patch.scale != 1);
    for (unsigned length = 1; length <= 16; ++length) {
        p()->length[ACID_GATE] = length; p()->pitch_lock = false; acid_generate(&g, 0);
        assert(!(p()->anchors >> length));
        for (unsigned i = 0; i < 16; ++i) assert(p()->pitch[i] % 12 == 0);
    }
}
static void test_internal_clock(void) {
    grid_init(&g, 42); g.acid.patch.green_chance = 0; grid_start(&g, true);
    for (unsigned t = 0; t < 2000; ++t) { grid_poll(&g, t); drain(); }
    assert(g.acid.pulses == 96 && g.tick == 16);
    grid_stop(&g); tap(0, 8);
    for (unsigned t = 2000; t < 2200; ++t) { grid_poll(&g, t); drain(); }
    assert(g.running);
    uint8_t stop = 0xfc, start = 0xfa, cont = 0xfb;
    grid_midi(&g, &stop, 1, 2200); assert(!g.running);
    unsigned pulse = g.acid.pulses; grid_midi(&g, &cont, 1, 2200); assert(g.running && g.acid.pulses == pulse);
    grid_midi(&g, &start, 1, 2200); assert(g.running && !g.acid.pulses && g.acid.head[0] == 15);
}
static void test_yellow_and_voice_stress(void) {
    init(651); g.acid.patch.yellow_chance = 100;
    for (unsigned n = 0; n < 1000; ++n) {
        unsigned step = n % 16; uint8_t before = p()->pitch[step];
        acid_mutate(&g, step, PITCH_YELLOW);
        assert(abs((int)p()->pitch[step] - before) <= 4);
    }
    init(7); memset(p()->steps[0], 1, 16); memset(p()->steps[2], 1, 16);
    g.acid.patch.overlap_ms = 500;
    p()->pitch[0] = 36; p()->pitch[1] = 43; p()->pitch[2] = 36; p()->pitch_lock = true;
    unsigned active[16][128] = {{0}}, attacks = 0; uint32_t rng = 18;
    grid_start(&g, true);
    for (unsigned i = 0; i < 12000; ++i) {
        rng = rng * 1664525u + 1013904223u;
        if (i > 24 && i % 91 == 0) tap(0, (rng >> 4) % 10);
        if (i % 73 == 0) tap(9, (rng >> 8) % 10);
        if (i % 53 == 0) tap(1 + (rng >> 16) % 8, (rng >> 24) % 10);
        clocks(1);
        while (grid_tx_peek(&g)) {
            const MidiMessage *m = grid_tx_peek(&g); unsigned ch = m->bytes[0] & 15, note = m->bytes[1];
            if ((m->bytes[0] & 0xf0) == 0x90) { assert(!active[ch][note]); ++active[ch][note]; ++attacks; }
            if ((m->bytes[0] & 0xf0) == 0x80) { assert(active[ch][note]); --active[ch][note]; }
            grid_tx_ack(&g);
        }
    }
    grid_stop(&g);
    while (grid_tx_peek(&g)) {
        const MidiMessage *m = grid_tx_peek(&g); assert((m->bytes[0] & 0xf0) == 0x80);
        assert(active[m->bytes[0] & 15][m->bytes[1]]); --active[m->bytes[0] & 15][m->bytes[1]];
        grid_tx_ack(&g);
    }
    for (unsigned ch = 0; ch < 16; ++ch) for (unsigned note = 0; note < 128; ++note) assert(!active[ch][note]);
    assert(attacks > 100);
}
int main(void) {
    test_layout(); test_policies(); test_short_policy_wrap(); test_transport_and_patterns(); test_lengths(); test_scale_editor();
    test_recent_vocabulary(); test_motif_continuation(); test_bassline_structure(); test_root_editor();
    test_generation_and_memory(); test_combined_lock_and_evolution(); test_cc_lane(); test_learning(); test_midi_voice();
    test_legacy_migration(); test_v2_bank_migration(); test_roundtrip_and_determinism(); test_internal_clock(); test_yellow_and_voice_stress();
    puts("ok acid layout, clock, six polymetric lanes, memory, locks, scale, learning, voices, persistence and determinism");
}
