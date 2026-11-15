// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "patch_json.h"
#include "runtime.h"
#include <SDL2/SDL.h>
#include <cairo/cairo.h>
#include <getopt.h>
#include <errno.h>
#include <limits.h>
#include <inttypes.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define WIDTH 1160
#define HEIGHT 1040
#define CELL 43
#define GRID_TOP 174
#define GRID_LEFT 48
#define KEYBOARD_Y 760
#define KEYBOARD_X 48
#define WHITE_KEY_W 60
static Synth synth;
static bool audio_requested = true;
static SDL_AudioDeviceID audio_device;
static char audio_status[120] = "Audio not opened";
static int held_note = -1;
static bool knob_dragging;
static int knob_y, knob_bpm;
static Runtime runtime;
static volatile sig_atomic_t exit_requested;
static void request_exit(int signal_number) {
    (void)signal_number;
    exit_requested = 1;
}
static const char *patch_path = "grid.json";
static char status[160] = "Edit all six lanes live. Hold RESET for scale; tap RESET to leave scale.";
static bool inspect_learning;
static bool cell_pressed;
static unsigned pressed_x, pressed_y;
static unsigned io_completed;
static void send_command(CommandKind kind, unsigned x, unsigned y) {
    if (!runtime_command(&runtime, (Command){.kind = kind, .x = x, .y = y}))
        snprintf(status, sizeof(status), "Command queue is full; try again.");
}
static void rect(cairo_t *c, double x, double y, double w, double h, double r, double g, double b) {
    cairo_set_source_rgb(c, r, g, b);
    cairo_rectangle(c, x, y, w, h);
    cairo_fill(c);
}
static void text(cairo_t *c, double x, double y, double size, const char *label, double r, double g,
                 double b) {
    cairo_set_font_size(c, size);
    cairo_set_source_rgb(c, r, g, b);
    cairo_move_to(c, x, y);
    cairo_show_text(c, label);
}
static void audio_callback(void *userdata, Uint8 *stream, int length) {
    synth_render(userdata, (float *)stream, length / sizeof(float));
}
static void open_audio(void) {
    if (!audio_requested) {
        snprintf(audio_status, sizeof(audio_status), "Audio disabled (--no-audio)");
        return;
    }
    synth_init(&synth);
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        snprintf(audio_status, sizeof(audio_status), "Audio unavailable: %.80s", SDL_GetError());
        return;
    }
    SDL_AudioSpec wanted = {0};
    wanted.freq = SYNTH_RATE;
    wanted.format = AUDIO_F32SYS;
    wanted.channels = 1;
    wanted.samples = 512;
    wanted.callback = audio_callback;
    wanted.userdata = &synth;
    audio_device = SDL_OpenAudioDevice(NULL, 0, &wanted, NULL, 0);
    if (!audio_device) {
        snprintf(audio_status, sizeof(audio_status), "Audio unavailable: %.80s", SDL_GetError());
        return;
    }
    runtime.synth = &synth;
    snprintf(audio_status, sizeof(audio_status), "Acid preview / resonant filter + glide / CC cutoff: 350 / 800 / 1800 Hz");
    SDL_PauseAudioDevice(audio_device, 0);
}
static void keyboard_release(void) {
    if (held_note < 0)
        return;
    Command c = {.kind = CMD_MIDI, .midi = {0x80, held_note, 0}};
    if (runtime_command(&runtime, c))
        held_note = -1;
}
static void keyboard_press(unsigned note) {
    keyboard_release();
    if (held_note >= 0)
        return;
    Command c = {.kind = CMD_MIDI, .midi = {0x90, note, 100}};
    if (runtime_command(&runtime, c)) {
        held_note = note;
    }
}
static const unsigned white_notes[] = {48, 50, 52, 53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71};
static const unsigned black_notes[] = {49, 51, 54, 56, 58, 61, 63, 66, 68, 70};
static const unsigned black_after[] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
static int keyboard_hit(int x, int y) {
    int local = x - KEYBOARD_X;
    if (local < 0 || local >= 14 * WHITE_KEY_W || y < KEYBOARD_Y || y >= KEYBOARD_Y + 100)
        return -1;
    if (y < KEYBOARD_Y + 62)
        for (unsigned i = 0; i < 10; ++i) {
            int left = (black_after[i] + 1) * WHITE_KEY_W - 18;
            if (local >= left && local < left + 36)
                return black_notes[i];
        }
    return white_notes[local / WHITE_KEY_W];
}
static void draw_keyboard(cairo_t *c) {
    char label[160];
    snprintf(label, sizeof(label), "MIDI INPUT | click keys to teach vocabulary | C3-B4");
    text(c, KEYBOARD_X, KEYBOARD_Y - 20, 12, label, .8, .86, .92);
    for (unsigned i = 0; i < 14; ++i) {
        bool down = held_note == (int)white_notes[i];
        rect(c, KEYBOARD_X + i * WHITE_KEY_W, KEYBOARD_Y, WHITE_KEY_W - 2, 100, down ? .3 : .8,
             down ? .65 : .83, down ? .85 : .87);
        snprintf(label, sizeof(label), "%u", white_notes[i]);
        text(c, KEYBOARD_X + i * WHITE_KEY_W + 18, KEYBOARD_Y + 88, 11, label, .12, .16, .2);
    }
    for (unsigned i = 0; i < 10; ++i) {
        bool down = held_note == (int)black_notes[i];
        rect(c, KEYBOARD_X + (black_after[i] + 1) * WHITE_KEY_W - 18, KEYBOARD_Y, 36, 62, down ? .25 : .06,
             down ? .55 : .08, down ? .8 : .12);
    }
    rect(c, 930, KEYBOARD_Y + 48, 170, 32, .13, .17, .22);
    const char *sound = !audio_device                 ? "AUDIO UNAVAILABLE"
                        : atomic_load(&synth.enabled) ? "SOUND ON"
                                                      : "SOUND MUTED";
    text(c, 940, KEYBOARD_Y + 69, 11, sound, .85, .9, .95);
    text(c, KEYBOARD_X, KEYBOARD_Y + 122, 11, audio_status, .55, .7, .8);
}
static void draw_tempo(cairo_t *c, const Grid *g, int left) {
    double x = left + 464, y = 210;
    double angle = -3.926990817 + (g->acid.patch.bpm - 30) / 270.0 * 4.71238898;
    cairo_set_source_rgb(c, .15, .22, .3);
    cairo_arc(c, x, y, 27, 0, 6.283185307);
    cairo_fill(c);
    cairo_set_source_rgb(c, .5, .85, 1);
    cairo_set_line_width(c, 3);
    cairo_move_to(c, x, y);
    cairo_line_to(c, x + cos(angle) * 22, y + sin(angle) * 22);
    cairo_stroke(c);
    char label[32];
    snprintf(label, sizeof(label), "%u BPM", g->acid.patch.bpm);
    text(c, x - 28, y + 48, 11, label, .85, .9, .95);
    text(c, x - 28, y + 68, 9, "DRAG / INT", .55, .7, .8);
}
static bool control_press(const Snapshot *s, int x, int y) {
    int note = keyboard_hit(x, y);
    if (note >= 0) {
        keyboard_press(note);
        return true;
    }
    if (x >= 930 && x < 1100 && y >= KEYBOARD_Y + 48 && y < KEYBOARD_Y + 80) {
        if (audio_device)
            atomic_store(&synth.enabled, !atomic_load(&synth.enabled));
        return true;
    }
    int center = GRID_LEFT + 464;
    if (abs(x - center) <= 30 && abs(y - 210) <= 30) {
        knob_dragging = true; knob_y = y; knob_bpm = s->grid.acid.patch.bpm;
        send_command(CMD_TEMPO, knob_bpm, 0); return true;
    }
    return false;
}
static void control_motion(const SDL_MouseMotionEvent *event) {
    if (knob_dragging) {
        int bpm = knob_bpm + knob_y - event->y;
        if (bpm < 30)
            bpm = 30;
        if (bpm > 300)
            bpm = 300;
        send_command(CMD_TEMPO, bpm, 0);
    } else if (held_note >= 0) {
        int note = keyboard_hit(event->x, event->y);
        if (note != held_note) {
            keyboard_release();
            if (note >= 0)
                keyboard_press(note);
        }
    }
}
static const char *scale_label(unsigned x, unsigned y) {
    static const char *notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    int pc = acid_scale_key(x, y);
    if (pc >= 0) return notes[pc];
    if (x == 0 && y == 9) return "EXIT";
    if (x != 9) return "";
    switch (y) {
    case 0: return "SAVE";
    case 1: return "LOAD";
    case 2: return "CLOCK";
    case 8: return "BASE";
    case 9: return "NEW";
    default: return "";
    }
}
static const char *control_label(const Acid *a, unsigned y, char buffer[16]) {
    const AcidPattern *p = &a->patch.patterns[a->patch.selected];
    if (y < 6 && y % 2) {
        const char *levels[] = {"NONE", "SUB", "MOD", "MAX"}; return levels[p->intensity[y / 2]];
    }
    if (y == 7) return p->pitch_lock ? "LOCK" : "FREE";
    unsigned lane = y < 8 ? y / 2 : y - 4;
    if (a->length_select == (int)lane) return "END?";
    snprintf(buffer, 16, "L%u", p->length[lane]); return buffer;
}
static const char *cell_label(const Grid *g, unsigned x, unsigned y, char buffer[16]) {
    const Acid *a = &g->acid; const AcidPattern *p = &a->patch.patterns[a->patch.selected];
    if (a->scale_page) {
        const char *label = scale_label(x, y);
        if (acid_scale_key(x, y) == p->root) {
            snprintf(buffer, 16, "%s*", label); return buffer;
        }
        return label;
    }
    if (x == 0) {
        if (y == 8) return "PLAY";
        if (y == 9) return "RESET";
        snprintf(buffer, 16, "%u", y + 1); return buffer;
    }
    if (x == 9) return control_label(a, y, buffer);
    unsigned lane, step; acid_coordinate(x, y, &lane, &step);
    if (lane == ACID_GATE) {
        uint8_t gates[16]; acid_gate_states(p, gates);
        if (gates[step] == GATE_TIED) return "TIE";
        if (gates[step] == GATE_LONG) { snprintf(buffer, 16, "%u x2", step + 1); return buffer; }
    }
    static const uint8_t cc[3] = {0, 64, 127};
    snprintf(buffer, 16, "%u", lane == ACID_CC ? cc[p->steps[lane][step]] : step + 1); return buffer;
}
static void draw_cell(cairo_t *c, const Grid *g, int left, unsigned x, unsigned y, Color color) {
    int px = left + x * CELL, py = GRID_TOP + y * CELL;
    rect(c, px, py, CELL - 5, CELL - 5, .035 + color.r / 300., .04 + color.g / 300., .05 + color.b / 300.);
    char buffer[16]; const char *label = cell_label(g, x, y, buffer);
    text(c, px + 3, py + 23, strlen(label) > 4 ? 8 : 10, label, .87, .9, .94);
}
static void debug_row(cairo_t *c, int left, int y, const char *name, const int *values, unsigned count) {
    text(c, left, y, 10, name, .5, .7, .78);
    for (unsigned i = 0; i < count; ++i) {
        char label[10]; snprintf(label, sizeof(label), "%d", values[i]);
        text(c, left + 59 + i * 23, y, 10, label, .85, .88, .9);
    }
}
static void draw_learning_detail(cairo_t *c, const Acid *a, int left) {
    char label[180]; double largest = 1;
    text(c, left, 846, 10, "INTERVALS -24..+24 / long-term green / recent blue", .5, .7, .78);
    for (unsigned i = 0; i < ACID_INTERVALS; ++i)
        if (a->learner.long_term.interval[i] > largest) largest = a->learner.long_term.interval[i];
    for (unsigned i = 0; i < ACID_INTERVALS; ++i) {
        double height = a->learner.long_term.interval[i] / largest * 42;
        rect(c, left + i * 8.5, 900 - height, 4, height, .12, .6, .4);
        height = a->learner.recent.interval[i] / largest * 42;
        rect(c, left + i * 8.5 + 4, 900 - height, 3, height, .2, .4, .9);
    }
    for (unsigned i = 0; i < ACID_INTERVALS; i += 12) {
        snprintf(label, sizeof(label), "%+d", (int)i - 24);
        text(c, left + i * 8.5, 918, 9, label, .5, .7, .78);
    }
    unsigned row = 0;
    for (unsigned i = 0; i < ACID_MOTIFS && row < 6; ++i) {
        const AcidMotif *m = &a->learner.motifs[i];
        if (!m->length) continue;
        snprintf(label, sizeof(label), "Motif %u [%+d %+d %+d] n=%.0f recent=%.1f", m->length,
                 m->intervals[0], m->intervals[1], m->intervals[2], m->count, m->recent);
        text(c, left, 939 + row++ * 18, 10, label, .6, .75, .8);
    }
    unsigned events = a->mutation_count < 2 ? a->mutation_count : 2;
    for (unsigned i = 0; i < events; ++i) {
        const AcidMutation *e = &a->mutations[(a->mutation_count - events + i) % 16];
        snprintf(label, sizeof(label), "Mutation @%u step %u: %u>%u / strategy %u / policy %u",
                 e->at, e->step + 1, e->before, e->after, e->strategy, e->policy);
        text(c, left, 1056 + i * 18, 9, label, .75, .7, .55);
    }
}
static void draw_memory(cairo_t *c, const AcidPattern *p, int left) {
    int values[16]; char label[16];
    for (unsigned i = 0; i < 16; ++i) values[i] = p->pitch[i];
    debug_row(c, left, 743, "Pitch", values, 16);
    for (unsigned i = 0; i < 8; ++i) values[i] = p->octave[i];
    debug_row(c, left, 763, "Octave", values, 8);
    for (unsigned i = 0; i < 16; ++i) values[i] = i < p->length[3] ? acid_policy(p->steps[3], p->length[3], i, PITCH_GREEN) : -1;
    debug_row(c, left, 783, "D:1-3", values, 16);
    for (unsigned i = 0; i < 8; ++i) values[i] = i < p->length[4] ? acid_policy(p->steps[4], p->length[4], i, OCT_BASE) : -1;
    debug_row(c, left, 803, "E:1-5", values, 8);
    static const uint8_t cc_values[] = {0, 64, 127};
    for (unsigned i = 0; i < 8; ++i) values[i] = cc_values[p->steps[ACID_CC][i]];
    snprintf(label, sizeof(label), "CC%u", ACID_SEQUENCE_CC);
    debug_row(c, left, 823, label, values, 8);
}
static void draw_debug(cairo_t *c, const Grid *g, int left) {
    const Acid *a = &g->acid; const AcidPattern *p = &a->patch.patterns[a->patch.selected];
    char label[180]; draw_memory(c, p, left);
    if (inspect_learning) { draw_learning_detail(c, a, left); return; }
    text(c, left, 846, 10, "LEARNED PITCH CLASSES   /   combined probability", .5, .7, .78);
    static const char *names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    for (unsigned i = 0; i < 12; ++i) {
        double weight = acid_pitch_weight(&a->learner, i, p->root);
        double height = weight * 150;
        rect(c, left + i * 35, 905 - height, 25, height, .12, .58, .42);
        text(c, left + i * 35, 923, 9, names[i], .7, .75, .8);
        snprintf(label, sizeof(label), "%.0f", a->learner.long_term.pitch[i]);
        text(c, left + i * 35, 938, 8, label, .5, .65, .7);
    }
    double conf[3]; acid_confidence(&a->learner, conf);
    snprintf(label, sizeof(label), "Confidence PC %.2f / interval %.2f / motif %.2f", conf[0], conf[1], conf[2]);
    text(c, left, 959, 10, label, .7, .8, .85);
    unsigned top = 0; double best = -1;
    for (unsigned i = 0; i < ACID_INTERVALS; ++i)
        if (a->learner.long_term.interval[i] > best) { best = a->learner.long_term.interval[i]; top = i; }
    snprintf(label, sizeof(label), "Signed intervals: peak %+d (%.0f), +2 %.0f / -2 %.0f", (int)top - 24,
             best, a->learner.long_term.interval[26], a->learner.long_term.interval[22]);
    text(c, left, 980, 10, label, .6, .75, .8);
    unsigned motif = 0;
    for (unsigned i = 1; i < ACID_MOTIFS; ++i)
        if (a->learner.motifs[i].count > a->learner.motifs[motif].count) motif = i;
    const AcidMotif *m = &a->learner.motifs[motif];
    snprintf(label, sizeof(label), "Motif: %u notes [%+d %+d %+d] count %.0f / recent %.1f", m->length,
             m->intervals[0], m->intervals[1], m->intervals[2], m->count, m->recent);
    text(c, left, 1001, 10, label, .6, .75, .8);
    snprintf(label, sizeof(label), "Heads A-F: %u %u %u %u %u %u | P:%d T:%d R:%d",
             a->head[0] + 1, a->head[1] + 1, a->head[2] + 1, a->head[3] + 1, a->head[4] + 1, a->head[5] + 1,
             a->pending_pattern < 0 ? 0 : a->pending_pattern + 1, a->pending_transport, a->pending_reset);
    text(c, left, 1022, 10, label, .6, .75, .8);
    static const char *reasons[] = {"motif note", "local interval", "pitch pool", "intentional leap", "motif continuation", "tonal return"};
    if (a->mutation_count) {
        const AcidMutation *event = &a->mutations[(a->mutation_count - 1) % 16];
        snprintf(label, sizeof(label), "Mutation %u: step %u %u>%u / %s / policy %u", a->mutation_count,
                 event->step + 1, event->before, event->after, reasons[event->strategy], event->policy);
    } else snprintf(label, sizeof(label), "Phrase memory repeats; no mutations yet.");
    text(c, left, 1043, 10, label, .75, .7, .55);
    snprintf(label, sizeof(label), "Learn %.0f notes / %u groups / %u echoes rejected", a->learner.long_term.observations,
             a->learner.groups, a->learner.rejected_echoes);
    text(c, left, 1064, 10, label, .6, .75, .8);
    snprintf(label, sizeof(label), "Lengths %u/%u/%u/%u/%u/%u | select %c | scale %03X / root %s",
             p->length[0], p->length[1], p->length[2], p->length[3], p->length[4], p->length[5],
             a->length_select < 0 ? '-' : 'A' + a->length_select, a->patch.scale, names[p->root]);
    text(c, left, 1085, 10, label, .6, .75, .8);
}
static const char *const buttons[] = {"START", "CONT", "STOP", "STEP", "EXT/INT", "SAVE", "LOAD", "BASE", "NEW"};
static const CommandKind button_kinds[] = {CMD_START, CMD_CONTINUE, CMD_STOP, CMD_STEP, CMD_CLOCK_SOURCE,
                                         CMD_SAVE, CMD_LOAD, CMD_RESTORE, CMD_GENERATE};
static void draw_grid(cairo_t *c, const Grid *g) {
    int left = GRID_LEFT; draw_tempo(c, g, left);
    char label[180]; Color colors[100]; grid_render(g, colors);
    snprintf(label, sizeof(label), "PATTERN %u", g->acid.patch.selected + 1);
    text(c, left, 104, 20, label, .91, .94, .98);
    snprintf(label, sizeof(label), "%s | %s | %u BPM | step events %" PRIu64,
             g->running ? "PLAYING" : "PAUSED", g->acid.patch.internal_clock ? "INTERNAL" : "EXTERNAL",
             g->acid.patch.bpm, g->tick);
    text(c, left, 129, 11, label, .55, .65, .73);
    if (g->acid.scale_page)
        text(c, left, 150, 10, "Tap = allow/disable | Hold = ROOT (white / *)", .7, .8, .85);
    static const char *lanes[] = {"Gate", "Accent", "Slide", "Pitch + Oct lock", "Octave", "CC sequence"};
    for (unsigned y = 0; y < 10; ++y)
        for (unsigned x = 0; x < 10; ++x) draw_cell(c, g, left, x, y, colors[y * 10 + x]);
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) {
        int y = GRID_TOP + acid_lane_rows[lane] * CELL + (lane < 4 ? 79 : 30);
        text(c, left + 432, lane ? y : GRID_TOP - 12, 8, lanes[lane], .4, .6, .68);
    }
    snprintf(label, sizeof(label), "IN %u OUT %u / dropped %u / %s", g->midi_in_count,
             g->midi_out_count, g->dropped_notes, g->dirty ? "unsaved" : "saved");
    text(c, left, 635, 11, label, .6, .7, .76);
    for (unsigned b = 0; b < 9; ++b) {
        int px = left + (b % 7) * 61, py = b < 7 ? 660 : 696;
        rect(c, px, py, 56, 29, .13, .17, .22);
        text(c, px + 4, py + 19, 9, buttons[b], .85, .9, .95);
    }
    text(c, left + 135, 714, 9, "BASE restores memory / NEW regenerates", .5, .65, .7);

}
static void draw(cairo_t *c, const Snapshot *s) {
    rect(c, 0, 0, WIDTH, HEIGHT, .025, .035, .055);
    cairo_select_font_face(c, "DejaVu Sans Mono", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
    text(c, 48, 40, 25, "C100 / PHRASE MEMORY", .9, .95, 1);
    text(c, 48, 65, 12, "One 10x10 instrument / USB MIDI in + out / I: learning detail", .5, .64,
         .74);
    text(c, 48, HEIGHT - 134, 11, s->midi_status, .5, .7, .8);
    text(c, 48, HEIGHT - 111, 11, status, .8, .84, .89);
    unsigned lines = s->log_next < 4 ? s->log_next : 4;
    for (unsigned j = 0; j < lines; ++j)
        text(c, 48, HEIGHT - 85 + j * 17, 11, s->events[(s->log_next - lines + j) % 20], .48, .62, .68);
    draw_grid(c, &s->grid);
    text(c, 614, 129, 15, "PHRASE / LEARNING", .55, .75, .8);
    cairo_save(c); cairo_translate(c, 0, -560); draw_debug(c, &s->grid, 614); cairo_restore(c);
    draw_keyboard(c);
}
static void save_load(const Patch *patch, bool save) {
    bool ok;
    if (save) {
        ok = patch_json_save(patch, patch_path);
        if (ok)
            ok = runtime_command(&runtime, (Command){.kind = CMD_SAVED, .patch = *patch});
    } else {
        Patch patch;
        ok = patch_json_load(&patch, patch_path);
        if (ok)
            ok = runtime_command(&runtime, (Command){.kind = CMD_APPLY, .patch = patch});
    }
    snprintf(status, sizeof(status), "%s %s: %s", save ? "Save" : "Load", ok ? "OK" : "FAILED",
             patch_path);
}
static void service_io(const Snapshot *s) {
    GridAction action = s->grid.action;
    if (action != ACTION_NONE && io_completed != s->storage_id) {
        save_load(&s->storage_patch, action == ACTION_SAVE);
        if (runtime_command(&runtime, (Command){.kind = CMD_ACTION_DONE, .storage_id = s->storage_id}))
            io_completed = s->storage_id;
    }
}
static bool grid_cell_hit(int gx, int gy, unsigned *x, unsigned *y) {
    if (gx < 0 || gx >= 10 * CELL || gy < 0 || gy >= 10 * CELL || gx % CELL >= CELL - 5 || gy % CELL >= CELL - 5)
        return false;
    *x = gx / CELL;
    *y = gy / CELL;
    return true;
}
static void click(const Snapshot *s, int x, int y) {
    if (control_press(s, x, y))
        return;
    int gx = x - GRID_LEFT, gy = y - GRID_TOP;
    if (gx < 0 || gx >= 10 * CELL) return;
    unsigned col, row;
    if (grid_cell_hit(gx, gy, &col, &row)) {
        cell_pressed = true; pressed_x = col; pressed_y = row;
        send_command(CMD_PRESS, col, row); return;
    }
    if ((y >= 660 && y < 689) || (y >= 696 && y < 725)) {
        unsigned button = gx / 61 + (y >= 696 ? 7 : 0);
        if (gx % 61 < 56 && button < 9) send_command(button_kinds[button], 0, 0);
    }
}
static void release_pointer(void) {
    keyboard_release();
    if (cell_pressed) {
        send_command(CMD_RELEASE, pressed_x, pressed_y); cell_pressed = false;
    }
    knob_dragging = false;
}
static void keyboard_event(SDL_Keycode key) {
    if (key == SDLK_i) inspect_learning = !inspect_learning;
    if (key == SDLK_ESCAPE) {
        release_pointer(); send_command(CMD_STOP, 0, 0);
    }
}
static bool handle_events(const Snapshot *snapshot) {
    SDL_Event event;
    for (unsigned budget = 0; budget < 128 && SDL_PollEvent(&event); ++budget) {
        switch (event.type) {
        case SDL_QUIT: return true;
        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) click(snapshot, event.button.x, event.button.y);
            break;
        case SDL_MOUSEMOTION: control_motion(&event.motion); break;
        case SDL_MOUSEBUTTONUP:
            if (event.button.button == SDL_BUTTON_LEFT) release_pointer();
            break;
        case SDL_WINDOWEVENT:
            if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) release_pointer();
            break;
        case SDL_KEYDOWN: keyboard_event(event.key.keysym.sym); break;
        default: break;
        }
    }
    return false;
}
static bool initialize_video(void) {
    // Honor explicit overrides (including SDL's dummy test driver), otherwise
    // avoid XWayland scaling when the desktop has a native Wayland connection.
    if (getenv("WAYLAND_DISPLAY"))
        SDL_SetHintWithPriority(SDL_HINT_VIDEODRIVER, "wayland,x11", SDL_HINT_DEFAULT);
    return SDL_InitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0;
}
static SDL_Renderer *create_canvas(SDL_Window *window) {
    SDL_Renderer *renderer = window ? SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE) : NULL;
    if (!renderer)
        return NULL;
    // SDL applies this same viewport/scale to both drawing and mouse events.
    // Do not transform already-logical event coordinates a second time.
    if (SDL_RenderSetLogicalSize(renderer, WIDTH, HEIGHT)) {
        SDL_DestroyRenderer(renderer);
        return NULL;
    }
    return renderer;
}
static int gui(unsigned run_ms, const char *capture) {
    if (!initialize_video()) {
        fprintf(stderr, "SDL: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *window = SDL_CreateWindow("C100 Phrase Memory", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          WIDTH, HEIGHT, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *renderer = create_canvas(window);
    SDL_Texture *texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                                        SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT)
                                    : NULL;
    if (!texture) {
        fprintf(stderr, "SDL graphics: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    int window_w, window_h, output_w, output_h;
    SDL_GetWindowSize(window, &window_w, &window_h);
    SDL_GetRendererOutputSize(renderer, &output_w, &output_h);
    fprintf(stderr, "SDL video=%s window=%dx%d pixels=%dx%d canvas=%dx%d\n", SDL_GetCurrentVideoDriver(),
            window_w, window_h, output_w, output_h, WIDTH, HEIGHT);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, HEIGHT);
    cairo_t *c = cairo_create(surface);
    bool done = false;
    uint32_t begun = monotonic_ms();
    Snapshot snapshot;
    while (!done && !exit_requested && (!run_ms || (uint32_t)(monotonic_ms() - begun) < run_ms)) {
        runtime_snapshot(&runtime, &snapshot);
        service_io(&snapshot);
        done = handle_events(&snapshot);
        draw(c, &snapshot);
        cairo_surface_flush(surface);
        SDL_UpdateTexture(texture, NULL, cairo_image_surface_get_data(surface),
                          cairo_image_surface_get_stride(surface));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        SDL_Delay(16);
    }
    if (capture && cairo_surface_write_to_png(surface, capture) != CAIRO_STATUS_SUCCESS)
        fprintf(stderr, "Capture failed: %s\n", capture);
    cairo_destroy(c);
    cairo_surface_destroy(surface);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    return 0;
}
typedef struct { unsigned min, max, value; } NumericOption;
static NumericOption numeric[] = {{30, 300, 0}, {1, 16, 0}, {1, 100, 0},
                                  {0, 500, UINT_MAX}, {0, 127, UINT_MAX}, {1, UINT32_MAX, 0xc100}, {1, 86400000, 0}};
static bool use_external, disable_learning, start_requested;
static bool numeric_option(unsigned index, const char *arg) {
    char *end; errno = 0;
    unsigned long n = strtoul(arg, &end, 10);
    if (!*arg || *end || errno || n < numeric[index].min || n > numeric[index].max) return false;
    numeric[index].value = n; return true;
}
static void configure_instrument(Grid *g) {
    AcidPatch *p = &g->acid.patch;
    if (numeric[0].value) p->bpm = numeric[0].value;
    if (numeric[1].value) p->channel = numeric[1].value;
    if (numeric[2].value) p->length_percent = numeric[2].value;
    if (numeric[3].value != UINT_MAX) p->overlap_ms = numeric[3].value;
    if (numeric[4].value != UINT_MAX) p->accent_cc = numeric[4].value;
    if (use_external) p->internal_clock = false;
    if (disable_learning) p->learning = false;
    if (start_requested) grid_start(g, true);
}
static bool parse_options(int argc, char **argv, bool *headless, bool *demo, unsigned *run_ms,
                          const char **capture) {
    const char **destinations[] = {capture, &patch_path, &runtime.in, &runtime.out};
    static const struct option options[] = {{"headless", no_argument, NULL, 'H'},
                                            {"demo", no_argument, NULL, 'D'},
                                            {"no-midi", no_argument, NULL, 'M'},
                                            {"no-audio", no_argument, NULL, 'A'},
                                            {"run-ms", required_argument, NULL, 306},
                                            {"capture", required_argument, NULL, 256},
                                            {"patch", required_argument, NULL, 257},
                                            {"in", required_argument, NULL, 258},
                                            {"out", required_argument, NULL, 259},
                                            {"bpm", required_argument, NULL, 300},
                                            {"channel", required_argument, NULL, 301},
                                            {"note-length", required_argument, NULL, 302},
                                            {"overlap-ms", required_argument, NULL, 303},
                                            {"accent-cc", required_argument, NULL, 304},
                                            {"seed", required_argument, NULL, 305},
                                            {"external-clock", no_argument, NULL, 'E'},
                                            {"play", no_argument, NULL, 'P'},
                                            {"no-learning", no_argument, NULL, 'L'},
                                            {NULL, 0, NULL, 0}};
    struct { int option; bool *target; bool value; } flags[] = {
        {'H', headless, true}, {'D', demo, true}, {'A', &audio_requested, false},
        {'M', &runtime.midi, false}, {'E', &use_external, true},
        {'L', &disable_learning, true}, {'P', &start_requested, true}
    };
    int option;
    while ((option = getopt_long(argc, argv, "", options, NULL)) != -1) {
        if (option >= 256 && option <= 259) {
            *destinations[option - 256] = optarg;
            continue;
        }
        if (option >= 300 && option <= 306) {
            if (!numeric_option(option - 300, optarg)) return false;
            continue;
        }
        bool found = false;
        for (unsigned i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i) {
            if (option == flags[i].option) { *flags[i].target = flags[i].value; found = true; }
        }
        if (!found) return false;
    }
    *run_ms = numeric[6].value;
    return optind == argc;
}

int main(int argc, char **argv) {
    struct sigaction action = {0};
    action.sa_handler = request_exit;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTERM, &action, NULL);
    bool headless = false, demo = false;
    unsigned run_ms = 0;
    const char *capture = NULL;
    runtime.midi = true;
    if (!parse_options(argc, argv, &headless, &demo, &run_ms, &capture)) {
        fprintf(stderr,
                "Usage: %s [--headless] [--demo] [--no-midi] [--no-audio] [--run-ms N] [--capture "
                "file.png]\n  [--patch file.json] [--in client:port] [--out client:port]\n"
                "  [--bpm 30..300] [--external-clock] [--channel 1..16]\n"
                "  [--note-length 1..100] [--overlap-ms 0..500] [--accent-cc 0..127]\n"
                "  [--seed N] [--no-learning] [--play]\n",
                argv[0]);
        return 2;
    }
    grid_init(&runtime.initial, numeric[5].value);
    Patch patch;
    if (!demo && patch_json_load(&patch, patch_path)) grid_apply_patch(&runtime.initial, &patch);
    configure_instrument(&runtime.initial);
    if (!headless)
        open_audio();
    if (!runtime_start(&runtime)) {
        fprintf(stderr, "Cannot start simulation worker\n");
        return 1;
    }
    int result = 0;
    if (headless) {
        if (!run_ms)
            run_ms = 1000;
        uint32_t start = monotonic_ms();
        struct timespec pause = {0, 10000000};
        Snapshot s;
        while (!exit_requested && (uint32_t)(monotonic_ms() - start) < run_ms) {
            runtime_snapshot(&runtime, &s);
            service_io(&s);
            nanosleep(&pause, NULL);
        }
    } else
        result = gui(run_ms, capture);
    runtime_stop(&runtime);
    if (audio_device)
        SDL_CloseAudioDevice(audio_device);
    SDL_Quit();
    Grid *g = &runtime.snapshot.grid;
    printf("Sequencer: tick=%" PRIu64 " input=%u output=%u dropped_notes=%u\n", g->tick,
           g->midi_in_count, g->midi_out_count, g->dropped_notes);
    return result;
}
