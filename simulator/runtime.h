// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "grid.h"
#include "synth.h"
#include <pthread.h>
#include <stdatomic.h>
typedef enum {
    CMD_PRESS,
    CMD_RELEASE,
    CMD_RESTORE,
    CMD_GENERATE,
    CMD_CLOCK_SOURCE,
    CMD_START,
    CMD_CONTINUE,
    CMD_STOP,
    CMD_STEP,
    CMD_MIDI,
    CMD_TEMPO,
    CMD_SAVE,
    CMD_LOAD,
    CMD_APPLY,
    CMD_SAVED,
    CMD_ACTION_DONE
} CommandKind;
typedef struct {
    CommandKind kind;
    unsigned x, y, storage_id;
    Patch patch;
    uint8_t midi[3];
} Command;
typedef struct {
    Grid grid;
    Patch storage_patch;
    unsigned storage_id;
    char events[20][100];
    unsigned log_next;
    char midi_status[140];
} Snapshot;
typedef struct {
    pthread_mutex_t commands_lock, snapshot_lock;
    Command commands[64];
    unsigned command_head, command_count;
    Snapshot snapshot;
    atomic_bool quit;
    pthread_t thread;
    bool midi;
    Synth *synth;
    const char *in, *out;
    Grid initial;
} Runtime;
uint32_t monotonic_ms(void);
bool runtime_start(Runtime *r);
bool runtime_command(Runtime *r, Command command);
void runtime_snapshot(Runtime *r, Snapshot *out);
void runtime_stop(Runtime *r);
