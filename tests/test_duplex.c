// SPDX-License-Identifier: GPL-2.0-or-later
#define _POSIX_C_SOURCE 200809L
#include "runtime.h"
#include <alsa/asoundlib.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static Runtime runtime;
static unsigned on[16][128], off[16][128], received, controllers;
static void read_output(snd_seq_t *seq) {
    snd_seq_event_t *event;
    while (snd_seq_event_input(seq, &event) >= 0) {
        if (event->type == SND_SEQ_EVENT_NOTEON) {
            ++on[event->data.note.channel][event->data.note.note];
            ++received;
        }
        if (event->type == SND_SEQ_EVENT_NOTEOFF)
            ++off[event->data.note.channel][event->data.note.note];
        if (event->type == SND_SEQ_EVENT_CONTROLLER) {
            assert(event->data.control.param == ACID_SEQUENCE_CC); ++controllers;
        }
        snd_seq_free_event(event);
    }
}
static void send_event(snd_seq_t *seq, int source, int type) {
    snd_seq_event_t event;
    snd_seq_ev_clear(&event);
    snd_seq_ev_set_source(&event, source);
    snd_seq_ev_set_subs(&event);
    snd_seq_ev_set_direct(&event);
    event.type = type;
    if (type == SND_SEQ_EVENT_NOTEON)
        snd_seq_ev_set_noteon(&event, 3, 65, 100);
    assert(snd_seq_event_output_direct(seq, &event) >= 0);
}
int main(void) {
    snd_seq_t *tester = NULL;
    int result = snd_seq_open(&tester, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (result < 0) {
        fprintf(stderr, "SKIP live ALSA: %s\n", snd_strerror(result));
        return 77;
    }
    snd_seq_set_client_name(tester, "C100 duplex test");
    int port = snd_seq_create_simple_port(tester, "test loop",
                                          SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_WRITE |
                                              SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                          SND_SEQ_PORT_TYPE_APPLICATION);
    assert(port >= 0);
    char address[32];
    snprintf(address, sizeof(address), "%d:%d", snd_seq_client_id(tester), port);
    runtime.midi = true;
    grid_init(&runtime.initial, 123);
    runtime.initial.acid.patch.internal_clock = false;
    runtime.in = address; runtime.out = address;
    assert(runtime_start(&runtime));
    struct timespec pause = {0, 1000000};
    Snapshot snapshot;
    uint32_t deadline = monotonic_ms() + 1000;
    do {
        runtime_snapshot(&runtime, &snapshot);
        if (strstr(snapshot.midi_status, "ALSA "))
            break;
        nanosleep(&pause, NULL);
    } while ((int32_t)(deadline - monotonic_ms()) > 0);
    assert(strstr(snapshot.midi_status, "ALSA "));
    send_event(tester, port, SND_SEQ_EVENT_START);
    unsigned early_output = 0;
    for (unsigned pulse = 0; pulse < 300; ++pulse) {
        if (pulse % 6 == 0)
            send_event(tester, port, SND_SEQ_EVENT_NOTEON);
        send_event(tester, port, SND_SEQ_EVENT_CLOCK);
        read_output(tester);
        nanosleep(&pause, NULL);
        if (pulse == 149)
            early_output = received;
    }
    send_event(tester, port, SND_SEQ_EVENT_STOP);
    deadline = monotonic_ms() + 150;
    while ((int32_t)(deadline - monotonic_ms()) > 0) {
        read_output(tester);
        nanosleep(&pause, NULL);
    }
    runtime_snapshot(&runtime, &snapshot);
    assert(early_output > 0 && received > early_output);
    assert(snapshot.grid.tick == 50);
    assert(snapshot.grid.midi_in_count == 352);
    assert(snapshot.grid.midi_out_count > 20 && !snapshot.grid.dropped_notes);
    runtime_stop(&runtime);
    read_output(tester);
    for (unsigned ch = 0; ch < 16; ++ch)
        for (unsigned note = 0; note < 128; ++note)
            assert(on[ch][note] == off[ch][note]);
    assert(snapshot.grid.acid.learner.long_term.observations == 50);
    assert(controllers == 50);
    snd_seq_close(tester);
    printf("ok live ALSA duplex: %u attacks, balanced releases, one instrument received all 352 events\n",
           received);
}
