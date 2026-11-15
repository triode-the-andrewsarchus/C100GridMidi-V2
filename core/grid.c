// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <string.h>
bool grid_queue_midi(Grid *g, uint8_t status, uint8_t note, uint8_t value) {
    if (g->tx_count == GRID_TX_CAP) return false;
    unsigned tail = (g->tx_head + g->tx_count) % GRID_TX_CAP;
    g->tx[tail] = (MidiMessage){{status, note, value}};
    ++g->tx_count;
    return true;
}
const MidiMessage *grid_tx_peek(const Grid *g) {
    return g->tx_count ? &g->tx[g->tx_head] : NULL;
}
void grid_tx_ack(Grid *g) {
    if (!g->tx_count) return;
    g->tx_head = (g->tx_head + 1) % GRID_TX_CAP;
    --g->tx_count;
    ++g->midi_out_count;
}
void grid_panic(Grid *g) {
    // Retain queued releases even across repeated stops and USB disconnections.
    unsigned pending = g->tx_count;
    for (unsigned i = 0; i < pending; ++i) {
        MidiMessage m = *grid_tx_peek(g);
        g->tx_head = (g->tx_head + 1) % GRID_TX_CAP;
        --g->tx_count;
        if ((m.bytes[0] & 0xf0) == 0x80)
            grid_queue_midi(g, m.bytes[0], m.bytes[1], 0);
    }
    for (unsigned i = 0; i < GRID_MAX_NOTES; ++i) {
        ActiveNote *n = &g->notes[i];
        if (n->active && grid_queue_midi(g, 0x80 | (n->channel - 1), n->note, 0))
            n->active = false;
    }
    g->acid.sliding = false; g->acid.tie_steps = 0; g->acid.gate_down = 0;
}
void grid_init(Grid *g, uint32_t seed) { memset(g, 0, sizeof(*g)); acid_init(g, seed); }
void grid_reset(Grid *g) { acid_reset(g); }
void grid_start(Grid *g, bool reset) { acid_start(g, reset); }
void grid_stop(Grid *g) { acid_stop(g); }
void grid_tick(Grid *g) { acid_clock(g); }
void grid_poll(Grid *g, uint32_t now) { acid_poll(g, now); }
void grid_midi(Grid *g, const uint8_t *d, size_t n, uint32_t now) { acid_midi(g, d, n, now); }
void grid_key(Grid *g, unsigned x, unsigned y, bool down, uint32_t now) {
    acid_key(g, x, y, down, now);
}
void grid_press(Grid *g, uint8_t x, uint8_t y) { grid_key(g, x, y, true, g->now); }
void grid_render(const Grid *g, Color colors[100]) { acid_render(g, colors); }
void grid_patch_encode(const Patch *p, uint8_t out[GRID_PATCH_BYTES]) { acid_patch_encode(p, out); }
bool grid_patch_decode(Patch *p, const uint8_t *d, size_t n) { return acid_patch_decode(p, d, n); }
bool grid_apply_patch(Grid *g, const Patch *p) { return acid_apply_patch(g, p); }
