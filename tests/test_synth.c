// SPDX-License-Identifier: GPL-2.0-or-later
#include "synth.h"
#include "grid.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static Synth synth;
static float output[2048];
static void event(unsigned status, unsigned data1, unsigned data2) {
    uint8_t bytes[3] = {status, data1, data2};
    synth_push(&synth, bytes);
}
static void render(size_t frames) {
    while (frames) {
        size_t count = frames > 2048 ? 2048 : frames;
        synth_render(&synth, output, count);
        for (size_t i = 0; i < count; ++i)
            assert(isfinite(output[i]) && fabsf(output[i]) <= 0.43f);
        frames -= count;
    }
}
static double energy(const float *samples, size_t count) {
    double sum = 0;
    for (size_t i = 0; i < count; ++i) sum += samples[i] * samples[i];
    return sum;
}
static double brightness(const float *samples, size_t count) {
    double differences = 0;
    for (size_t i = 1; i < count; ++i) {
        double delta = samples[i] - samples[i - 1];
        differences += delta * delta;
    }
    return differences / energy(samples, count);
}
static void silent(void) {
    for (unsigned i = 0; i < 2048; ++i) assert(output[i] == 0);
}
static void test_cutoff_and_accent(void) {
    const unsigned values[] = {0, 64, 127};
    double spectral[3];
    for (unsigned state = 0; state < 3; ++state) {
        synth_init(&synth);
        event(0xb0, ACID_SEQUENCE_CC, values[state]);
        event(0x90, 36, 85);
        render(SYNTH_RATE); // Let the attack and filter envelope settle.
        assert(energy(output, 2048) > 0.01); // A sustained oscillator, not a one-shot sample.
        spectral[state] = brightness(output, 2048);
    }
    assert(spectral[1] > spectral[0] * 1.4);
    assert(spectral[2] > spectral[1] * 1.4);
    double ordinary_energy, ordinary_brightness;
    synth_init(&synth); event(0x90, 36, 85); render(2048);
    ordinary_energy = energy(output, 2048);
    ordinary_brightness = brightness(output, 2048);
    synth_init(&synth); event(0x90, 36, 120); render(2048);
    assert(energy(output, 2048) > ordinary_energy * 1.05);
    assert(brightness(output, 2048) > ordinary_brightness * 1.2);
}
static void test_glide_and_lifetime(void) {
    synth_init(&synth); event(0x90, 36, 85); render(SYNTH_RATE / 2);
    float previous = synth.voice.frequency, envelope = synth.voice.envelope;
    event(0x90, 48, 85); // New attack before old release means slide.
    event(0x80, 36, 0);
    render(48);
    assert(synth.voice.gate && synth.voice.note == 48);
    assert(synth.voice.frequency > previous && synth.voice.frequency < previous * 1.1f);
    assert(synth.voice.envelope < envelope); // No filter-envelope retrigger during legato.
    render(SYNTH_RATE / 5);
    assert(fabsf(synth.voice.frequency - previous * 2) < 0.05f);
    event(0x80, 48, 0); event(0x90, 43, 85); render(48);
    assert(synth.voice.envelope > 0.9f); // Ordinary articulation retriggers immediately.
    assert(fabsf(synth.voice.frequency - synth.frequencies[43]) < 0.01f);
    event(0x90, 43, 0); render(SYNTH_RATE / 4); // Velocity-zero Note On is Note Off.
    assert(!synth.voice.active); render(2048); silent();
}
static void test_routing_and_safety(void) {
    synth_init(&synth);
    unsigned unrelated = (ACID_SEQUENCE_CC + 1) % 120;
    event(0xb3, unrelated, 0); render(1); assert(synth.cutoff[3] == 800);
    event(0xb3, ACID_SEQUENCE_CC, 127);
    event(0x93, 69, 100); render(2048);
    assert(synth.voice.channel == 3 && fabsf(synth.voice.frequency - 440) < 0.01f);
    assert(synth.current_cutoff > 1700 && synth.cutoff[0] == 800);
    event(0x80, 69, 0); render(2048); assert(synth.voice.gate); // Wrong-channel Off ignored.
    atomic_store(&synth.enabled, false);
    event(0x83, 69, 0); render(SYNTH_RATE / 4); silent();
    assert(!synth.voice.active);
    atomic_store(&synth.enabled, true);
    for (unsigned i = 0; i <= SYNTH_EVENTS; ++i) event(0x90, 60, 100);
    render(2048);
    assert(atomic_load(&synth.dropped) == 1 && !synth.voice.active); silent();
    event(0x90, 60, 100); render(2048); assert(energy(output, 2048) > 0);
    unsigned panic_cc = ACID_SEQUENCE_CC == 120 ? 123 : 120;
    event(0xb0, panic_cc, 0); render(2048); assert(!synth.voice.active); silent();
    // All MIDI pitches and fast cutoff changes must remain finite and bounded.
    for (unsigned note = 0; note < 128; ++note) {
        event(0x80, note ? note - 1 : 0, 0);
        event(0xb0, ACID_SEQUENCE_CC, note % 2 ? 127 : 0);
        event(0x90, note, 127); render(1024);
    }
    event(0x80, 127, 0); render(SYNTH_RATE / 4); assert(!synth.voice.active);
}
static void test_sequencer_audio(void) {
    Grid grid;
    grid_init(&grid, 303);
    AcidPattern *p = &grid.acid.patch.patterns[0];
    p->pitch_lock = true;
    p->length[ACID_CC] = 3;
    for (unsigned step = 0; step < 16; ++step) {
        p->steps[ACID_GATE][step] = grid.acid.working[ACID_GATE][step] = true;
        p->steps[ACID_SLIDE][step] = grid.acid.working[ACID_SLIDE][step] = step % 4 == 0;
        p->steps[ACID_ACCENT][step] = grid.acid.working[ACID_ACCENT][step] = step % 3 == 0;
        p->pitch[step] = step % 2 ? 48 : 36;
    }
    for (unsigned step = 0; step < 3; ++step) p->steps[ACID_CC][step] = step;
    synth_init(&synth); grid_start(&grid, true);
    unsigned states = 0;
    bool heard_glide = false;
    double sum = 0;
    for (unsigned ms = 0; ms < 4200; ++ms) {
        if (ms == 4000) grid_stop(&grid);
        grid_poll(&grid, ms);
        const MidiMessage *message;
        while ((message = grid_tx_peek(&grid))) {
            if ((message->bytes[0] & 0xf0) == 0xb0) {
                assert(message->bytes[1] == ACID_SEQUENCE_CC);
                unsigned value = message->bytes[2];
                states |= value == 0 ? 1 : value == 64 ? 2 : 4;
            }
            synth_push(&synth, message->bytes);
            grid_tx_ack(&grid);
        }
        render(48); sum += energy(output, 48);
        if (synth.voice.active && fabsf(synth.voice.target_frequency - synth.voice.frequency) > 5)
            heard_glide = true;
    }
    assert(states == 7 && heard_glide && sum > 10 && !synth.voice.active);
}
static void test_tied_audio(void) {
    Grid grid; grid_init(&grid, 303); AcidPattern *p = &grid.acid.patch.patterns[0];
    memset(p->steps[ACID_GATE], 0, 16); memset(p->steps[ACID_SLIDE], 0, 16);
    p->steps[ACID_GATE][0] = GATE_LONG; p->pitch_lock = true;
    p->pitch[0] = 36; p->pitch[1] = 43;
    unsigned attacks = 0, releases = 0; double continuation = 0;
    synth_init(&synth); grid_start(&grid, true);
    for (unsigned ms = 0; ms < 500; ++ms) {
        grid_poll(&grid, ms); const MidiMessage *message;
        while ((message = grid_tx_peek(&grid))) {
            if ((message->bytes[0] & 0xf0) == 0x90) ++attacks;
            if ((message->bytes[0] & 0xf0) == 0x80) { ++releases; assert(ms == 250); }
            synth_push(&synth, message->bytes); grid_tx_ack(&grid);
        }
        render(48);
        if (ms >= 125 && ms < 250) {
            assert(synth.voice.gate && synth.voice.note == 36);
            continuation += energy(output, 48);
        }
    }
    assert(attacks == 1 && releases == 1 && continuation > 0.1 && !synth.voice.active);
}
int main(void) {
    test_cutoff_and_accent();
    test_glide_and_lifetime();
    test_routing_and_safety();
    test_sequencer_audio(); test_tied_audio();
    puts("ok acid preview: three CC cutoffs, accent, sustained saw, legato glide, mono release, "
         "mute/overflow safety and sequencer audio integration");
}
