// SPDX-License-Identifier: GPL-2.0-or-later
#include "grid.h"
#include <string.h>
static double confidence(double count, double support, double cap) {
    return cap * count / (count + support);
}
static double agreement(const double *accumulated, double total, const double *recent,
                        double recent_total, unsigned bins) {
    double distance = 0;
    for (unsigned i = 0; i < bins; ++i) {
        double difference = (accumulated[i] + 1) / (total + bins) -
                            (recent[i] + 1) / (recent_total + bins);
        distance += difference < 0 ? -difference : difference;
    }
    return 1 - .25 * distance;
}
void acid_confidence(const AcidLearner *l, double out[3]) {
    out[0] = confidence(l->long_term.observations, 48, .80) *
             agreement(l->long_term.pitch, l->long_term.observations, l->recent.pitch, l->recent.observations, 12);
    out[1] = confidence(l->long_term.transitions, 64, .75) *
             agreement(l->long_term.interval, l->long_term.transitions, l->recent.interval,
                       l->recent.transitions, ACID_INTERVALS);
    double motifs = 0;
    for (unsigned i = 0; i < ACID_MOTIFS; ++i) motifs += l->motifs[i].count > 1 ? l->motifs[i].count - 1 : 0;
    out[2] = confidence(motifs, 160, .65);
}
static double blend(double prior, double count, double total, double recent, double recent_total,
                    unsigned bins, double alpha) {
    double learned = (count + 1) / (total + bins);
    double current = (recent + 1) / (recent_total + bins);
    return (1 - alpha) * prior + alpha * (.25 * learned + .75 * current);
}
double acid_pitch_weight(const AcidLearner *l, unsigned pc, unsigned root) {
    static const double prior[12] = {6, 1, 2, 3, 3, 2, 1, 5, 2, 2, 3, 1};
    double alpha[3]; acid_confidence(l, alpha);
    return blend(prior[(pc + 12 - root) % 12] / 31., l->long_term.pitch[pc],
                 l->long_term.observations, l->recent.pitch[pc], l->recent.observations, 12, alpha[0]);
}
double acid_interval_weight(const AcidLearner *l, int interval) {
    if (interval < -24 || interval > 24) return .001;
    unsigned bin = interval + 24;
    int magnitude = interval < 0 ? -interval : interval;
    double prior = magnitude == 0 ? .24 : magnitude <= 2 ? .08 :
                   magnitude == 7 || magnitude == 12 ? .05 : .008;
    double alpha[3]; acid_confidence(l, alpha);
    return blend(prior / 1.08, l->long_term.interval[bin], l->long_term.transitions,
                 l->recent.interval[bin], l->recent.transitions, ACID_INTERVALS, alpha[1]);
}
static void decay_recent(AcidLearner *l) {
    for (unsigned i = 0; i < 12; ++i) l->recent.pitch[i] *= .985;
    for (unsigned i = 0; i < ACID_INTERVALS; ++i) l->recent.interval[i] *= .985;
    l->recent.observations *= .985;
    l->recent.transitions *= .985;
    l->recent.register_sum *= .985;
    for (unsigned i = 0; i < ACID_MOTIFS; ++i) l->motifs[i].recent *= .985;
}
double acid_motif_weight(const AcidLearner *l, unsigned index) {
    const AcidMotif *m = &l->motifs[index];
    return .25 * m->count / (l->long_term.transitions + 1) +
           .75 * m->recent / (l->recent.transitions + 1);
}
static void observe_motif(AcidLearner *l, unsigned length) {
    int8_t intervals[3] = {0};
    unsigned start = l->history_count - length;
    for (unsigned i = 0; i + 1 < length; ++i)
        intervals[i] = (int)l->history[start + i + 1] - l->history[start + i];
    unsigned slot = 0;
    for (unsigned i = 0; i < ACID_MOTIFS; ++i) {
        AcidMotif *m = &l->motifs[i];
        if (m->length == length && !memcmp(m->intervals, intervals, length - 1)) {
            ++m->count; ++m->recent; return;
        }
        if (acid_motif_weight(l, i) < acid_motif_weight(l, slot)) slot = i;
    }
    AcidMotif *m = &l->motifs[slot];
    memset(m, 0, sizeof(*m));
    memcpy(m->intervals, intervals, sizeof(intervals));
    m->length = length; m->count = m->recent = 1;
}
void acid_learn_flush(Grid *g, uint32_t now) {
    AcidLearner *l = &g->acid.learner;
    if (!l->group_count || (uint32_t)(now - l->group_at) < l->grouping_ms) return;
    ++l->groups;
    if (l->group_count == 1) {
        if (l->has_previous) {
            int interval = (int)l->group_note - l->previous;
            if (interval >= -24 && interval <= 24) {
                ++l->long_term.interval[interval + 24];
                ++l->recent.interval[interval + 24];
                ++l->long_term.transitions; ++l->recent.transitions;
            }
        }
        if (l->history_count == 4) {
            memmove(l->history, l->history + 1, 3); --l->history_count;
        }
        l->history[l->history_count++] = l->group_note;
        for (unsigned n = 2; n <= l->history_count; ++n) observe_motif(l, n);
        l->previous = l->group_note; l->has_previous = true;
    } else {
        // Chords contribute harmony only. Do not infer a melody through a chord.
        l->has_previous = false; l->history_count = 0;
    }
    l->group_count = 0;
}
void acid_learn(Grid *g, uint8_t note, uint32_t now, bool self_generated) {
    AcidLearner *l = &g->acid.learner;
    if (self_generated) { ++l->rejected_echoes; return; }
    if (!g->acid.patch.learning || note > 127) return;
    acid_learn_flush(g, now);
    decay_recent(l);
    AcidProfile *profiles[] = {&l->long_term, &l->recent};
    for (unsigned i = 0; i < 2; ++i) {
        AcidProfile *p = profiles[i];
        ++p->pitch[note % 12]; p->register_sum += note; ++p->observations;
        if (p->observations == 1 || note < p->low) p->low = note;
        if (note > p->high) p->high = note;
    }
    if (!l->group_count) { l->group_at = now; l->group_note = note; }
    if (l->group_count < 255) ++l->group_count;
}
