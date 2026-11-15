// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static Grid g;
static unsigned ons, offs, ccs, held[128];
static AcidPattern *pattern(void) { return &g.acid.patch.patterns[0]; }
static void init(void) {
    grid_init(&g, 303); g.acid.patch.internal_clock = false;
    g.acid.patch.green_chance = 0;
    memset(pattern()->steps[ACID_GATE], 0, 16);
    memset(pattern()->steps[ACID_SLIDE], 0, 16);
    memset(pattern()->steps[ACID_ACCENT], 0, 16);
    pattern()->pitch_lock = true;
    pattern()->pitch[0] = 36; pattern()->pitch[1] = 43; pattern()->pitch[2] = 39;
    ons = offs = ccs = 0; memset(held, 0, sizeof(held));
}
static void tap_step(unsigned step) {
    grid_key(&g, step % 8 + 1, step / 8, true, g.now);
    grid_key(&g, step % 8 + 1, step / 8, false, g.now + 1);
}
static void hold_step(unsigned step) {
    grid_key(&g, step % 8 + 1, step / 8, true, g.now);
    grid_poll(&g, g.now + ACID_HOLD_MS);
    grid_key(&g, step % 8 + 1, step / 8, false, g.now + 1);
}
static void drain(void) {
    const MidiMessage *m;
    while ((m = grid_tx_peek(&g))) {
        unsigned kind = m->bytes[0] & 0xf0, note = m->bytes[1];
        if (kind == 0x90) { assert(!held[note]); ++held[note]; ++ons; }
        if (kind == 0x80) { assert(held[note]); --held[note]; ++offs; }
        if (kind == 0xb0) ++ccs;
        grid_tx_ack(&g);
    }
}
static void clocks(unsigned count) {
    uint8_t clock = 0xf8;
    for (unsigned i = 0; i < count; ++i) {
        grid_midi(&g, &clock, 1, g.now + 21); grid_poll(&g, g.now); drain();
    }
}
static void test_edit_and_recovery(void) {
    init(); uint8_t states[16]; Color colors[100];
    g.acid.working[0][4] = GATE_ON; // Unrelated temporary evolution survives editing.
    tap_step(0); assert(pattern()->steps[0][0] == GATE_ON);
    grid_render(&g, colors); assert(colors[1].g > colors[1].r);
    tap_step(0); assert(pattern()->steps[0][0] == GATE_OFF);
    hold_step(0); acid_gate_states(pattern(), states);
    assert(states[0] == GATE_LONG && states[1] == GATE_TIED && !pattern()->steps[0][1]);
    grid_render(&g, colors); assert(colors[1].r > colors[1].b && colors[2].r > colors[2].g);
    tap_step(1); acid_gate_states(pattern(), states);
    assert(states[0] == GATE_ON && states[1] == GATE_OFF);
    assert(g.acid.working[0][4] == GATE_ON);
    hold_step(0); tap_step(0); acid_gate_states(pattern(), states);
    assert(states[0] == GATE_OFF && states[1] == GATE_OFF); // Yellow taps straight to Off.
    tap_step(1); hold_step(0); tap_step(1);
    acid_gate_states(pattern(), states); assert(states[0] == GATE_ON && states[1] == GATE_ON);
    hold_step(1); // An existing long note can itself be covered, then restored.
    hold_step(0); acid_gate_states(pattern(), states);
    assert(states[1] == GATE_TIED && states[2] == GATE_OFF);
    tap_step(1); acid_gate_states(pattern(), states);
    assert(states[0] == GATE_ON && states[1] == GATE_LONG && states[2] == GATE_TIED);
    uint8_t encoded[ACID_PATCH_BYTES]; AcidPatch loaded;
    grid_patch_encode(&g.acid.patch, encoded); assert(encoded[4] == 3);
    assert(grid_patch_decode(&loaded, encoded, sizeof(encoded)));
    acid_gate_states(&loaded.patterns[0], states);
    assert(states[1] == GATE_LONG && states[2] == GATE_TIED && !loaded.patterns[0].steps[0][2]);
    init(); pattern()->length[0] = 3; memset(pattern()->steps[0], GATE_LONG, 3);
    acid_gate_states(pattern(), states); assert(states[2] == GATE_ON);
    hold_step(2); acid_gate_states(pattern(), states);
    assert(states[2] == GATE_LONG && states[0] == GATE_TIED);
    tap_step(0); acid_gate_states(pattern(), states);
    assert(states[0] == GATE_LONG && states[1] == GATE_TIED);
}
static void test_hold_gesture(void) {
    init(); tap_step(0);
    grid_key(&g, 1, 0, true, 10); grid_key(&g, 1, 0, true, 20); // Debounced repeat.
    grid_poll(&g, 10 + ACID_HOLD_MS - 1); assert(pattern()->steps[0][0] == GATE_ON);
    grid_poll(&g, 10 + ACID_HOLD_MS); assert(pattern()->steps[0][0] == GATE_LONG);
    grid_poll(&g, g.now + 1000); assert(pattern()->steps[0][0] == GATE_LONG);
    grid_key(&g, 1, 0, false, g.now); assert(pattern()->steps[0][0] == GATE_LONG);
    tap_step(0); assert(pattern()->steps[0][0] == GATE_OFF);
    // Holds work without an intervening poll, across timestamp rollover, and concurrently.
    grid_key(&g, 1, 0, true, UINT32_MAX - 100);
    grid_key(&g, 1, 0, false, ACID_HOLD_MS - 101); assert(pattern()->steps[0][0] == GATE_LONG);
    grid_key(&g, 5, 0, true, 1000); grid_key(&g, 7, 0, true, 1001);
    grid_poll(&g, 1001 + ACID_HOLD_MS);
    assert(pattern()->steps[0][4] == GATE_LONG && pattern()->steps[0][6] == GATE_LONG);
    grid_key(&g, 5, 0, false, g.now); grid_key(&g, 7, 0, false, g.now);
    // A held cell cannot edit a different pattern or become an edit after endpoint selection.
    init(); grid_key(&g, 4, 0, true, 0); grid_key(&g, 0, 1, true, 1); clocks(1);
    assert(g.acid.patch.selected == 1); uint8_t before = g.acid.patch.patterns[1].steps[0][3];
    grid_key(&g, 4, 0, false, ACID_HOLD_MS); assert(g.acid.patch.patterns[1].steps[0][3] == before);
    init(); grid_key(&g, 4, 0, true, 0); grid_key(&g, 9, 0, true, 1);
    grid_key(&g, 4, 0, false, 2); assert(!pattern()->steps[0][3]);
    grid_key(&g, 4, 0, true, 3); grid_poll(&g, 1000); grid_key(&g, 4, 0, false, g.now);
    assert(pattern()->length[0] == 4 && !pattern()->steps[0][3]);
    init(); grid_key(&g, 4, 0, true, 0); grid_key(&g, 0, 9, true, 1);
    grid_poll(&g, ACID_HOLD_MS + 1); assert(g.acid.scale_page && !pattern()->steps[0][3]);
}
static void test_clocked_eighth(void) {
    init(); hold_step(0); tap_step(2);
    grid_start(&g, true); clocks(1);
    assert(ons == 1 && !offs && g.acid.tie_steps == 2 && held[36]);
    clocks(6); assert(ons == 1 && !offs && ccs == 2 && held[36]);
    // A tempo change during the continuation follows clock count, not a fixed millisecond length.
    uint8_t clock = 0xf8;
    for (unsigned i = 0; i < 5; ++i) {
        grid_midi(&g, &clock, 1, g.now + 40); grid_poll(&g, g.now); drain();
        assert(ons == 1 && !offs && held[36]);
    }
    grid_midi(&g, &clock, 1, g.now + 40); grid_poll(&g, g.now); drain();
    assert(ons == 2 && offs == 1 && ccs == 3 && held[39] && !held[36]);
    grid_stop(&g); drain(); assert(ons == offs && !g.acid.tie_steps);
}
static void test_live_cancel_and_slide(void) {
    init(); tap_step(1); hold_step(0);
    grid_start(&g, true); clocks(1); tap_step(1); clocks(6);
    assert(ons == 2 && offs == 1 && held[43]); // Restored On attacks at the next step.
    grid_stop(&g); drain();
    init(); hold_step(0); tap_step(2); pattern()->steps[ACID_SLIDE][0] = 1;
    grid_start(&g, true); clocks(7); assert(ons == 1 && !offs);
    clocks(6); assert(ons == 2 && !offs && g.notes[1].active);
    grid_poll(&g, g.now + g.acid.patch.overlap_ms); drain(); assert(offs == 1);
    grid_stop(&g); drain(); assert(ons == offs);
}
static void test_wrap_lengths_and_safety(void) {
    init(); pattern()->length[0] = 3; hold_step(2);
    uint8_t states[16]; acid_gate_states(pattern(), states);
    assert(states[2] == GATE_LONG && states[0] == GATE_TIED);
    grid_start(&g, true); clocks(1); assert(!ons); // Reset starts on a continuation, not an attack.
    clocks(12); assert(ons == 1 && !offs);
    clocks(6); assert(ons == 1 && !offs); clocks(6); assert(ons == offs);
    grid_stop(&g); drain();
    pattern()->length[0] = 2; acid_gate_states(pattern(), states); assert(states[0] == GATE_OFF);
    pattern()->length[0] = 3; acid_gate_states(pattern(), states); assert(states[0] == GATE_TIED);
    init(); pattern()->length[0] = 1; hold_step(0);
    grid_start(&g, true); clocks(19); assert(ons == 2 && offs == 1); // One-cell lane: every other visit.
    grid_stop(&g); drain(); assert(ons == offs && !g.acid.tie_steps);
    init(); hold_step(0); grid_start(&g, true); clocks(1);
    uint32_t due = g.notes[0].due; grid_poll(&g, due - 1); drain(); assert(!offs);
    grid_poll(&g, due); drain(); assert(ons == offs); // Clock-loss watchdog.
    grid_reset(&g); assert(!g.acid.tie_steps);
}
static void test_tied_transport(void) {
    init(); pattern()->length[0] = 2; hold_step(0);
    grid_start(&g, true); clocks(1);
    grid_key(&g, 0, 1, true, g.now); grid_key(&g, 0, 1, false, g.now + 1);
    clocks(6); assert(g.acid.patch.selected == 0 && !offs);
    clocks(6); assert(g.acid.patch.selected == 1 && offs == 1 && !g.acid.tie_steps);
    grid_stop(&g); drain(); assert(ons == offs);
    init(); hold_step(0); grid_start(&g, true); clocks(1);
    grid_key(&g, 0, 9, true, g.now); grid_key(&g, 0, 9, false, g.now + 1);
    clocks(6); assert(!g.acid.head[0] && g.acid.tie_steps == 2);
    assert(ons == 2 && offs == 1);
    grid_key(&g, 0, 8, true, g.now); grid_key(&g, 0, 8, false, g.now + 1);
    clocks(6); assert(!g.running && !g.acid.tie_steps && ons == offs);
}
static void test_evolution_and_resolution(void) {
    init(); hold_step(0); pattern()->intensity[0] = 3;
    uint8_t base[16]; memcpy(base, pattern()->steps[0], 16);
    grid_start(&g, true);
    for (unsigned i = 0; i < 32; ++i) {
        clocks(i ? 96 : 1); assert(g.acid.working[0][0] == GATE_LONG);
        clocks(6); assert(g.acid.working[0][1] == GATE_TIED);
        clocks(90);
    }
    assert(!memcmp(base, pattern()->steps[0], 16)); grid_stop(&g); drain();
    uint32_t rng = 13;
    for (unsigned length = 1; length <= 16; ++length) for (unsigned round = 0; round < 100; ++round) {
        pattern()->length[0] = length;
        for (unsigned i = 0; i < 16; ++i) {
            rng = rng * 1664525u + 1013904223u; pattern()->steps[0][i] = rng % 3;
        }
        pattern()->gate_priority = (rng >> 16) % 16;
        uint8_t states[16]; acid_gate_states(pattern(), states);
        for (unsigned i = 0; i < length; ++i) {
            unsigned next = (i + 1) % length;
            if (length > 1 && states[i] == GATE_LONG) assert(states[next] == GATE_TIED);
        }
        for (unsigned i = 0; i < length; ++i) if (states[i] == GATE_TIED) {
            uint8_t previous = pattern()->steps[0][i]; tap_step(i);
            acid_gate_states(pattern(), states); assert(states[i] == previous);
            break;
        }
    }
}
int main(void) {
    test_edit_and_recovery(); test_hold_gesture(); test_clocked_eighth(); test_live_cancel_and_slide();
    test_wrap_lengths_and_safety(); test_tied_transport(); test_evolution_and_resolution();
    puts("ok Gate ties: tap toggles, hold creates Long, red recovery, exact clock length, slides, wrap, "
         "length one, evolution, overlap resolution and storage");
}
