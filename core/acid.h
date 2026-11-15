// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ACID_H
#define ACID_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define ACID_STEPS 16
#define ACID_LANES 6
#ifndef ACID_SEQUENCE_CC
#define ACID_SEQUENCE_CC 74
#endif
#if ACID_SEQUENCE_CC < 0 || ACID_SEQUENCE_CC > 127
#error "ACID_SEQUENCE_CC must be a MIDI controller number (0..127)"
#endif
#define ACID_PATTERNS 8
#define ACID_MOTIFS 24
#define ACID_INTERVALS 49
#define ACID_PATCH_BYTES 2048
#define ACID_HOLD_MS 450
struct Grid;
struct Color;
typedef enum { ACID_GATE, ACID_ACCENT, ACID_SLIDE, ACID_PITCH, ACID_OCTAVE, ACID_CC } AcidLane;
typedef enum { GATE_OFF, GATE_ON, GATE_LONG, GATE_TIED } GateState;
typedef enum { PITCH_INHERIT, PITCH_GREEN, PITCH_YELLOW, PITCH_RED } PitchPolicy;
typedef enum { OCT_INHERIT, OCT_UP, OCT_UP_OR_BASE, OCT_BASE, OCT_DOWN_OR_BASE, OCT_DOWN } OctPolicy;
typedef struct {
    uint8_t steps[ACID_LANES][ACID_STEPS], length[ACID_LANES], intensity[3];
    uint8_t base_pitch[16], pitch[16];
    int8_t base_octave[8], octave[8];
    uint16_t anchors;
    uint8_t root, weights[12], gate_priority;
    bool pitch_lock;
    uint32_t base_rng, rng;
} AcidPattern;
typedef struct {
    AcidPattern patterns[8];
    uint16_t bpm, overlap_ms, scale;
    uint8_t selected, channel, velocity, accent_velocity, length_percent;
    uint8_t low, high, strategy[5], green_chance, yellow_chance;
    int8_t accent_cc; // -1 disables optional accent CC.
    bool internal_clock, learning;
} AcidPatch;
typedef struct {
    double pitch[12], interval[ACID_INTERVALS], register_sum, observations, transitions;
    uint8_t low, high;
} AcidProfile;
typedef struct {
    int8_t intervals[3];
    uint8_t length;
    double count, recent;
} AcidMotif;
typedef struct {
    AcidProfile long_term, recent;
    AcidMotif motifs[ACID_MOTIFS];
    uint32_t group_at, groups, rejected_echoes;
    uint8_t group_count, group_note, previous, history[4], history_count;
    uint16_t grouping_ms, echo_ms;
    bool has_previous;
} AcidLearner;
typedef struct {
    uint32_t at;
    uint8_t note, channel;
    bool valid;
} AcidEcho;
typedef struct {
    uint8_t step, before, after, strategy, policy;
    uint32_t at;
} AcidMutation;
typedef struct {
    AcidPatch patch;
    AcidLearner learner;
    uint8_t head[ACID_LANES], working[3][16];
    int8_t length_select;
    uint8_t tie_steps, tied_step;
    uint16_t gate_down, gate_hold_done;
    uint32_t gate_at[ACID_STEPS];
    uint32_t pulses, next_ms, remainder, last_clock, pulse_ms, reset_at, scale_at;
    int8_t scale_held;
    bool scale_hold_done;
    bool sliding, initialized_clock, scale_page, reset_down, hold_opened;
    int8_t pending_pattern, pending_transport;
    bool pending_reset;
    AcidEcho echoes[32];
    uint8_t echo_next;
    AcidMutation mutations[16];
    uint32_t mutation_count;
} Acid;
extern const uint8_t acid_lane_capacity[ACID_LANES], acid_lane_rows[ACID_LANES];
bool acid_coordinate(unsigned x, unsigned y, unsigned *lane, unsigned *step);
uint8_t acid_policy(const uint8_t *steps, unsigned length, unsigned step, uint8_t fallback);
int acid_scale_key(unsigned x, unsigned y);
void acid_gate_states(const AcidPattern *p, uint8_t out[ACID_STEPS]);
void acid_init(struct Grid *g, uint32_t seed);
void acid_key(struct Grid *g, unsigned x, unsigned y, bool down, uint32_t now);
void acid_clock(struct Grid *g);
void acid_poll(struct Grid *g, uint32_t now);
void acid_midi(struct Grid *g, const uint8_t *data, size_t length, uint32_t now);
void acid_start(struct Grid *g, bool reset);
void acid_stop(struct Grid *g);
void acid_reset(struct Grid *g);
void acid_render(const struct Grid *g, struct Color *colors);
unsigned acid_random(AcidPattern *p, unsigned bound);
uint8_t acid_quantize(const AcidPatch *config, int note);
void acid_mutate(struct Grid *g, unsigned step, uint8_t policy);
void acid_generate(struct Grid *g, unsigned pattern);
void acid_restore(struct Grid *g);
bool acid_scale(struct Grid *g, uint16_t mask);
bool acid_root(struct Grid *g, unsigned pitch_class);
void acid_learn(struct Grid *g, uint8_t note, uint32_t now, bool self_generated);
void acid_learn_flush(struct Grid *g, uint32_t now);
double acid_pitch_weight(const AcidLearner *l, unsigned pitch_class, unsigned root);
double acid_interval_weight(const AcidLearner *l, int interval);
void acid_confidence(const AcidLearner *l, double out[3]);
double acid_motif_weight(const AcidLearner *l, unsigned index);
bool acid_patch_valid(const AcidPatch *p);
void acid_patch_encode(const AcidPatch *p, uint8_t out[ACID_PATCH_BYTES]);
bool acid_patch_decode(AcidPatch *p, const uint8_t *data, size_t length);
bool acid_apply_patch(struct Grid *g, const AcidPatch *p);
#endif
