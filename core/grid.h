// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef GRID_H
#define GRID_H
#include "acid.h"
#define GRID_W 10
#define GRID_TX_CAP 256
#define GRID_MAX_NOTES 2
#define GRID_PATCH_BYTES ACID_PATCH_BYTES
typedef struct { uint8_t bytes[3]; } MidiMessage;
typedef struct Color { uint8_t r, g, b; } Color;
typedef struct { uint8_t channel, note; uint32_t due; bool active; } ActiveNote;
typedef enum { ACTION_NONE, ACTION_SAVE, ACTION_LOAD } GridAction;
typedef AcidPatch Patch;
typedef struct Grid {
    Acid acid;
    MidiMessage tx[GRID_TX_CAP];
    ActiveNote notes[GRID_MAX_NOTES];
    uint16_t tx_head, tx_count;
    uint32_t now, midi_in_count, midi_out_count, dropped_notes;
    uint64_t tick;
    GridAction action;
    bool running, dirty;
} Grid;
void grid_init(Grid *g, uint32_t seed);
void grid_reset(Grid *g);
void grid_start(Grid *g, bool reset);
void grid_stop(Grid *g);
void grid_tick(Grid *g);
void grid_poll(Grid *g, uint32_t now);
void grid_midi(Grid *g, const uint8_t *data, size_t length, uint32_t now);
void grid_key(Grid *g, unsigned x, unsigned y, bool down, uint32_t now);
void grid_press(Grid *g, uint8_t x, uint8_t y);
void grid_render(const Grid *g, Color colors[100]);
bool grid_queue_midi(Grid *g, uint8_t status, uint8_t note, uint8_t value);
const MidiMessage *grid_tx_peek(const Grid *g);
void grid_tx_ack(Grid *g);
void grid_panic(Grid *g);
void grid_patch_encode(const Patch *p, uint8_t out[GRID_PATCH_BYTES]);
bool grid_patch_decode(Patch *p, const uint8_t *data, size_t length);
bool grid_apply_patch(Grid *g, const Patch *p);
#endif
