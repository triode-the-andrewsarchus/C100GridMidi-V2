// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define SYNTH_RATE 48000
#define SYNTH_EVENTS 1024

typedef struct {
    uint8_t status, data1, data2;
} SynthEvent;
typedef struct {
    bool active, gate;
    uint8_t channel, note;
    float phase, frequency, target_frequency;
    float amp, gain, target_gain, envelope, accent, target_accent;
    float filter[4];
} SynthVoice;
typedef struct {
    float frequencies[128], cutoff[16], current_cutoff;
    SynthVoice voice;
    SynthEvent events[SYNTH_EVENTS];
    atomic_uint head, tail, dropped;
    atomic_bool panic, enabled;
} Synth;
void synth_init(Synth *s);
// One simulation-worker producer, one audio-callback consumer. Neither waits.
// Local monophonic acid preview; all MIDI channels use the same sound.
void synth_push(Synth *s, const uint8_t bytes[3]);
void synth_render(Synth *s, float *output, size_t frames);
