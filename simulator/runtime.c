// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include <alsa/asoundlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
uint32_t monotonic_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}
bool runtime_command(Runtime *r, Command command) {
    pthread_mutex_lock(&r->commands_lock);
    bool ok = r->command_count < 64;
    if (ok) {
        r->commands[(r->command_head + r->command_count) % 64] = command;
        ++r->command_count;
    }
    pthread_mutex_unlock(&r->commands_lock);
    return ok;
}
static bool pop_command(Runtime *r, Command *c) {
    if (pthread_mutex_trylock(&r->commands_lock))
        return false;
    bool ok = r->command_count != 0;
    if (ok) {
        *c = r->commands[r->command_head];
        r->command_head = (r->command_head + 1) % 64;
        --r->command_count;
    }
    pthread_mutex_unlock(&r->commands_lock);
    return ok;
}
static void request_storage(Snapshot *s, GridAction action) {
    Grid *g = &s->grid;
    grid_stop(g);
    g->action = action;
    s->storage_patch = g->acid.patch;
    ++s->storage_id;
}
static void apply_press(Snapshot *s, const Command *c) {
    Grid *g = &s->grid;
    grid_key(g, c->x, c->y, true, g->now);
    if (g->action != ACTION_NONE) request_storage(s, g->action);
}
static bool command_storage(Snapshot *s, const Command *c) {
    Grid *g = &s->grid;
    switch (c->kind) {
    case CMD_SAVE:
    case CMD_LOAD:
        request_storage(s, c->kind == CMD_SAVE ? ACTION_SAVE : ACTION_LOAD);
        break;
    case CMD_APPLY:
        grid_apply_patch(g, &c->patch);
        break;
    case CMD_SAVED: {
        uint8_t current[GRID_PATCH_BYTES], saved[GRID_PATCH_BYTES];
        grid_patch_encode(&g->acid.patch, current);
        grid_patch_encode(&c->patch, saved);
        if (!memcmp(current, saved, sizeof(current)))
            g->dirty = false;
        break;
    }
    case CMD_ACTION_DONE:
        if (c->storage_id == s->storage_id)
            g->action = ACTION_NONE;
        break;
    default:
        return false;
    }
    return true;
}
static void set_tempo(Grid *g, unsigned bpm) {
    if (bpm < 30 || bpm > 300)
        return;
    g->acid.patch.bpm = bpm;
    g->acid.patch.internal_clock = true; // Turning the knob explicitly selects internal timing.
    g->dirty = true;
    if (g->running)
        grid_start(g, false);
}
static void command_apply(Snapshot *s, const Command *c) {
    Grid *g = &s->grid;
    if (command_storage(s, c))
        return;
    switch (c->kind) {
    case CMD_PRESS:
        apply_press(s, c);
        break;
    case CMD_RELEASE:
        grid_key(g, c->x, c->y, false, g->now);
        break;
    case CMD_RESTORE:
        acid_restore(g);
        break;
    case CMD_GENERATE:
        acid_generate(g, g->acid.patch.selected);
        break;
    case CMD_CLOCK_SOURCE:
        g->acid.patch.internal_clock = !g->acid.patch.internal_clock;
        g->acid.initialized_clock = false;
        g->dirty = true;
        break;
    case CMD_START:
        grid_start(g, true);
        break;
    case CMD_CONTINUE:
        grid_start(g, false);
        break;
    case CMD_STOP:
        grid_stop(g);
        break;
    case CMD_MIDI:
        grid_midi(g, c->midi, 3, g->now);
        break;
    case CMD_TEMPO:
        set_tempo(g, c->x);
        break;
    case CMD_STEP:
        if (!g->running)
            grid_tick(g);
        break;
    default:
        break;
    }
}
static void log_event(Snapshot *s, const char *dir, const uint8_t *d, unsigned size) {
    snprintf(s->events[s->log_next++ % 20], 100, "%9u  %s  %02X %02X %02X (%u)", monotonic_ms(),
             dir, d[0], size > 1 ? d[1] : 0, size > 2 ? d[2] : 0, size);
}
static snd_seq_t *open_midi(Runtime *r, int *in, int *out, Snapshot *s) {
    if (!r->midi) {
        snprintf(s->midi_status, sizeof(s->midi_status), "MIDI disabled (--no-midi)"); return NULL;
    }
    snd_seq_t *seq = NULL;
    int error = snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (error < 0) {
        snprintf(s->midi_status, sizeof(s->midi_status), "ALSA unavailable: %s", snd_strerror(error));
        return NULL;
    }
    snd_seq_set_client_name(seq, "C100 Acid Simulator");
    *in = snd_seq_create_simple_port(seq, "MIDI input", SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                    SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    *out = snd_seq_create_simple_port(seq, "MIDI output", SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
                                     SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (*in < 0 || *out < 0) {
        snd_seq_close(seq); snprintf(s->midi_status, sizeof(s->midi_status), "ALSA port creation failed");
        return NULL;
    }
    snd_seq_addr_t address;
    if (r->in && (snd_seq_parse_address(seq, &address, r->in) < 0 ||
                  snd_seq_connect_from(seq, *in, address.client, address.port) < 0))
        fprintf(stderr, "Cannot connect input: %s\n", r->in);
    if (r->out && (snd_seq_parse_address(seq, &address, r->out) < 0 ||
                   snd_seq_connect_to(seq, *out, address.client, address.port) < 0))
        fprintf(stderr, "Cannot connect output: %s\n", r->out);
    snprintf(s->midi_status, sizeof(s->midi_status), "ALSA %d | IN:%d OUT:%d", snd_seq_client_id(seq), *in, *out);
    fprintf(stderr, "%s\n", s->midi_status); return seq;
}
static unsigned event_bytes(const snd_seq_event_t *ev, uint8_t d[3]) {
    switch (ev->type) {
    case SND_SEQ_EVENT_CLOCK:
        d[0] = 0xf8;
        return 1;
    case SND_SEQ_EVENT_START:
        d[0] = 0xfa;
        return 1;
    case SND_SEQ_EVENT_CONTINUE:
        d[0] = 0xfb;
        return 1;
    case SND_SEQ_EVENT_STOP:
        d[0] = 0xfc;
        return 1;
    case SND_SEQ_EVENT_NOTEON:
    case SND_SEQ_EVENT_NOTEOFF:
        d[0] = (ev->type == SND_SEQ_EVENT_NOTEON ? 0x90 : 0x80) | ev->data.note.channel;
        d[1] = ev->data.note.note;
        d[2] = ev->data.note.velocity;
        return 3;
    case SND_SEQ_EVENT_CONTROLLER:
        d[0] = 0xb0 | ev->data.control.channel;
        d[1] = ev->data.control.param;
        d[2] = ev->data.control.value;
        return 3;
    default:
        return 0;
    }
}
static void receive_midi(snd_seq_t *seq, int port, Snapshot *s) {
    if (!seq)
        return;
    for (unsigned budget = 0; budget < 128; ++budget) {
        snd_seq_event_t *ev;
        int result = snd_seq_event_input(seq, &ev);
        if (result < 0)
            break;
        if (ev->source.client != snd_seq_client_id(seq)) {
            uint8_t d[3] = {0};
            unsigned size = event_bytes(ev, d);
            if (ev->dest.port == port && size) {
                grid_midi(&s->grid, d, size, monotonic_ms()); log_event(s, "IN ", d, size);
            }
        }
        snd_seq_free_event(ev);
    }
}
static void transmit_midi(snd_seq_t *seq, int port, Snapshot *s, Synth *synth) {
    for (unsigned budget = 0; budget < 32; ++budget) {
        const MidiMessage *m = grid_tx_peek(&s->grid);
        if (!m)
            break;
        if (seq) {
            snd_seq_event_t ev;
            snd_seq_ev_clear(&ev);
            snd_seq_ev_set_source(&ev, port);
            snd_seq_ev_set_subs(&ev);
            snd_seq_ev_set_direct(&ev);
            if ((m->bytes[0] & 0xf0) == 0x90)
                snd_seq_ev_set_noteon(&ev, m->bytes[0] & 15, m->bytes[1], m->bytes[2]);
            else if ((m->bytes[0] & 0xf0) == 0xb0)
                snd_seq_ev_set_controller(&ev, m->bytes[0] & 15, m->bytes[1], m->bytes[2]);
            else
                snd_seq_ev_set_noteoff(&ev, m->bytes[0] & 15, m->bytes[1], 0);
            int result = snd_seq_event_output_direct(seq, &ev);
            if (result < 0)
                break; // Retry later; input keeps running.
        }
        if (synth)
            synth_push(synth, m->bytes);
        log_event(s, seq ? "OUT" : "SIM", m->bytes, 3);
        grid_tx_ack(&s->grid);
    }
}
static void publish(Runtime *r, const Snapshot *s) {
    if (!pthread_mutex_trylock(&r->snapshot_lock)) {
        r->snapshot = *s;
        pthread_mutex_unlock(&r->snapshot_lock);
    }
}
static void *worker(void *userdata) {
    Runtime *r = userdata;
    Snapshot s = {0};
    s.grid = r->initial;
    int in = 0, out = 0;
    snd_seq_t *seq = open_midi(r, &in, &out, &s);
    struct timespec pause = {0, 500000};
    uint32_t published = 0;
    while (!atomic_load(&r->quit)) {
        uint32_t now = monotonic_ms();
        grid_poll(&s.grid, now);
        receive_midi(seq, in, &s);
        Command c;
        for (unsigned budget = 0; budget < 64 && pop_command(r, &c); ++budget) {
            command_apply(&s, &c);
            if (c.kind == CMD_MIDI) log_event(&s, "IN ", c.midi, 3);
        }
        transmit_midi(seq, out, &s, r->synth);
        if ((uint32_t)(now - published) >= 16) {
            publish(r, &s);
            published = now;
        }
        nanosleep(&pause, NULL);
    }
    grid_stop(&s.grid);
    uint32_t deadline = monotonic_ms() + 100;
    while ((int32_t)(deadline - monotonic_ms()) > 0 && s.grid.tx_count) {
        transmit_midi(seq, out, &s, r->synth);
        nanosleep(&pause, NULL);
    }
    publish(r, &s);
    if (seq)
        snd_seq_close(seq);
    return NULL;
}
bool runtime_start(Runtime *r) {
    pthread_mutex_init(&r->commands_lock, NULL);
    pthread_mutex_init(&r->snapshot_lock, NULL);
    atomic_init(&r->quit, false);
    r->snapshot.grid = r->initial;
    return pthread_create(&r->thread, NULL, worker, r) == 0;
}
void runtime_snapshot(Runtime *r, Snapshot *out) {
    pthread_mutex_lock(&r->snapshot_lock);
    *out = r->snapshot;
    pthread_mutex_unlock(&r->snapshot_lock);
}
void runtime_stop(Runtime *r) {
    atomic_store(&r->quit, true);
    pthread_join(r->thread, NULL);
    pthread_mutex_destroy(&r->commands_lock);
    pthread_mutex_destroy(&r->snapshot_lock);
}
