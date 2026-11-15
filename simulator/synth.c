// SPDX-License-Identifier: GPL-2.0-or-later
#include "synth.h"
#include "acid.h"
#include <math.h>
#include <string.h>

void synth_init(Synth *s) {
    memset(s, 0, sizeof(*s));
    atomic_init(&s->head, 0);
    atomic_init(&s->tail, 0);
    atomic_init(&s->dropped, 0);
    atomic_init(&s->panic, false);
    atomic_init(&s->enabled, true);
    for (unsigned note = 0; note < 128; ++note)
        s->frequencies[note] = 440.0f * exp2f(((int)note - 69) / 12.0f);
    for (unsigned channel = 0; channel < 16; ++channel)
        s->cutoff[channel] = 800.0f;
    s->current_cutoff = 800.0f;
}
void synth_push(Synth *s, const uint8_t bytes[3]) {
    unsigned head = atomic_load_explicit(&s->head, memory_order_relaxed);
    unsigned tail = atomic_load_explicit(&s->tail, memory_order_acquire);
    if (head - tail == SYNTH_EVENTS) {
        atomic_fetch_add(&s->dropped, 1);
        atomic_store(&s->panic, true); // Overflow must not strand a missing Note Off.
        return;
    }
    s->events[head % SYNTH_EVENTS] = (SynthEvent){bytes[0], bytes[1], bytes[2]};
    atomic_store_explicit(&s->head, head + 1, memory_order_release);
}
static void control(Synth *s, SynthEvent e) {
    unsigned channel = e.status & 15;
    if (e.data1 == ACID_SEQUENCE_CC) {
        // Three moderate base cutoffs. Envelope and accent animate these further.
        s->cutoff[channel] = e.data2 < 32 ? 350.0f : e.data2 < 96 ? 800.0f : 1800.0f;
    } else if (e.data1 == 120 || e.data1 == 123) {
        if (s->voice.channel == channel)
            memset(&s->voice, 0, sizeof(s->voice));
    }
}
static void apply_event(Synth *s, SynthEvent e) {
    if (e.data1 > 127 || e.data2 > 127) return;
    unsigned kind = e.status & 0xf0, channel = e.status & 15;
    SynthVoice *v = &s->voice;
    if (kind == 0xb0) { control(s, e); return; }
    if (kind != 0x80 && kind != 0x90) return;
    bool on = kind == 0x90 && e.data2;
    if (!on) {
        // A slide's delayed old Note Off must not release its new destination.
        if (v->channel == channel && v->note == e.data1) v->gate = false;
        return;
    }
    bool legato = v->active && v->gate && v->channel == channel;
    v->target_frequency = s->frequencies[e.data1];
    v->target_gain = 0.55f + 0.45f * e.data2 / 127.0f;
    v->target_accent = fmaxf(0, (e.data2 - 90.0f) / 37.0f);
    if (!legato) {
        v->frequency = v->target_frequency;
        v->amp = 0;
        v->envelope = 1;
    } else {
        v->envelope = fmaxf(v->envelope, v->target_accent * 0.75f);
    }
    v->active = v->gate = true;
    v->channel = channel;
    v->note = e.data1;
}
// Polynomial correction removes the saw discontinuity without a sample bank.
static float saw(float phase, float increment) {
    float correction = 0;
    if (phase < increment) {
        float t = phase / increment;
        correction = 2 * t - t * t - 1;
    } else if (phase > 1 - increment) {
        float t = (phase - 1) / increment;
        correction = t * t + 2 * t + 1;
    }
    return 2 * phase - 1 - correction;
}
static float lowpass(SynthVoice *v, float input, float coefficient) {
    // Four trapezoidal one-pole stages with analytically solved resonance feedback.
    // Gentle input saturation supplies drive. This evokes acid, not a circuit model.
    const float resonance = 3.2f;
    float state = 0, power = 1;
    for (int stage = 3; stage >= 0; --stage) {
        state += power * (1 - coefficient) * v->filter[stage];
        power *= coefficient;
    }
    float value = (tanhf(input * 1.6f) - resonance * state) / (1 + resonance * power);
    for (unsigned stage = 0; stage < 4; ++stage) {
        float change = (value - v->filter[stage]) * coefficient;
        value = v->filter[stage] + change;
        v->filter[stage] = value + change;
    }
    return value;
}
static float voice_sample(Synth *s) {
    SynthVoice *v = &s->voice;
    // 20 ms glide time constant (~60 ms to settle); overlap preserves both envelopes.
    v->frequency += (v->target_frequency - v->frequency) * 0.00104112f;
    v->amp += ((v->gate ? 1.0f : 0) - v->amp) * (v->gate ? 0.0103626f : 0.00138792f);
    v->gain += (v->target_gain - v->gain) * 0.004158f;
    v->accent += (v->target_accent - v->accent) * 0.004158f;
    v->envelope *= 0.99986112f; // 150 ms filter-envelope decay, independent of note pitch.
    s->current_cutoff += (s->cutoff[v->channel] - s->current_cutoff) * 0.00260078f;
    float cutoff = s->current_cutoff * exp2f(v->envelope * (1.2f + v->accent * 0.8f));
    cutoff = fminf(cutoff, 6500.0f);
    float g = tanf(3.141592654f * cutoff / (SYNTH_RATE * 2));
    float coefficient = g / (1 + g), value = 0;
    float increment = v->frequency / (SYNTH_RATE * 2);
    // 2x oversampling keeps the driven saw and resonant filter well behaved.
    for (unsigned sub = 0; sub < 2; ++sub) {
        value += lowpass(v, saw(v->phase, increment), coefficient);
        v->phase += increment;
        v->phase -= floorf(v->phase);
    }
    float output = tanhf(value * 0.7f) * v->amp * v->gain * 0.42f;
    if (!v->gate && v->amp < 0.00001f) memset(v, 0, sizeof(*v));
    return output;
}
void synth_render(Synth *s, float *output, size_t frames) {
    unsigned tail = atomic_load_explicit(&s->tail, memory_order_relaxed);
    unsigned head = atomic_load_explicit(&s->head, memory_order_acquire);
    if (atomic_exchange(&s->panic, false)) {
        memset(&s->voice, 0, sizeof(s->voice));
        tail = head;
    }
    for (; tail != head; ++tail)
        apply_event(s, s->events[tail % SYNTH_EVENTS]);
    atomic_store_explicit(&s->tail, tail, memory_order_release);
    bool enabled = atomic_load(&s->enabled);
    for (size_t frame = 0; frame < frames; ++frame) {
        float value = s->voice.active ? voice_sample(s) : 0;
        output[frame] = enabled ? value : 0;
    }
}
