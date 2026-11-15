// SPDX-License-Identifier: GPL-2.0-or-later
// Drive the real SDL handler, drawing, storage and concurrent engine thread.
#define main simulator_application_main
#include "../simulator/main.c"
#undef main
#include <assert.h>
#include <unistd.h>
#include <fontconfig/fontconfig.h>
static Snapshot latest;
static void settle(unsigned ms) {
    uint32_t end = monotonic_ms() + ms; struct timespec pause = {0, 1000000};
    do { runtime_snapshot(&runtime, &latest); service_io(&latest); nanosleep(&pause, NULL); }
    while ((int32_t)(end - monotonic_ms()) > 0);
    runtime_snapshot(&runtime, &latest);
}
static void pointer(Uint32 type, int x, int y) {
    SDL_Event e = {.type = type}; e.button.button = SDL_BUTTON_LEFT;
    e.button.x = x; e.button.y = y; assert(SDL_PushEvent(&e) == 1); assert(!handle_events(&latest));
}
static void key(unsigned x, unsigned y) {
    int px = GRID_LEFT + x * CELL + 18, py = GRID_TOP + y * CELL + 18;
    pointer(SDL_MOUSEBUTTONDOWN, px, py); pointer(SDL_MOUSEBUTTONUP, px, py); settle(30);
}
static void test_gate_tie_ui(void) {
    key(2, 0); key(2, 0); // Fast taps only toggle Off/On.
    assert(latest.grid.acid.patch.patterns[0].steps[0][1] == GATE_OFF);
    pointer(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 2 * CELL + 18, GRID_TOP + 18);
    settle(ACID_HOLD_MS + 50);
    pointer(SDL_MOUSEBUTTONUP, GRID_LEFT + 2 * CELL + 18, GRID_TOP + 18); settle(30);
    const AcidPattern *p = &latest.grid.acid.patch.patterns[0];
    assert(p->steps[0][1] == GATE_LONG && p->steps[0][2] == GATE_OFF);
    char label[16]; assert(!strcmp(cell_label(&latest.grid, 3, 0, label), "TIE"));
    key(3, 0); assert(p->steps[0][1] == GATE_ON && p->steps[0][2] == GATE_OFF);
    pointer(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 2 * CELL + 18, GRID_TOP + 18);
    settle(ACID_HOLD_MS + 50);
    pointer(SDL_MOUSEBUTTONUP, GRID_LEFT + 2 * CELL + 18, GRID_TOP + 18); settle(30);
}
int main(void) {
    assert(SDL_setenv("SDL_AUDIODRIVER", "dummy", 1) == 0);
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    open_audio(); assert(audio_device && runtime.synth == &synth);
    char path[128];
    snprintf(path, sizeof(path), "/tmp/acid-ui-%ld.json", (long)getpid());
    patch_path = path; grid_init(&runtime.initial, 100);
    assert(runtime_start(&runtime)); settle(30);
    test_gate_tie_ui();
    uint8_t gate = latest.grid.acid.patch.patterns[0].steps[0][15];
    key(8, 1); assert(latest.grid.acid.patch.patterns[0].steps[0][15] == (gate + 1) % 2);
    key(9, 0); assert(latest.grid.acid.length_select == ACID_GATE);
    key(7, 1); assert(latest.grid.acid.patch.patterns[0].length[ACID_GATE] == 15);
    key(9, 2); key(7, 2); assert(latest.grid.acid.patch.patterns[0].length[ACID_ACCENT] == 7);
    key(9, 8); key(3, 8); assert(latest.grid.acid.patch.patterns[0].length[ACID_OCTAVE] == 3);
    key(9, 9); key(5, 9); assert(latest.grid.acid.patch.patterns[0].length[ACID_CC] == 5);
    key(1, 9); assert(latest.grid.acid.patch.patterns[0].steps[ACID_CC][0] == 2);
    key(9, 7); assert(latest.grid.acid.patch.patterns[0].pitch_lock);
    key(0, 8); settle(160); assert(latest.grid.running);
    // GUI input and output remain concurrent; learning also continues after pause.
    for (unsigned n = 0; n < 8; ++n) {
        keyboard_press(60 + n); settle(25); keyboard_release();
    }
    settle(150); assert(latest.grid.acid.learner.long_term.observations == 8);
    assert(latest.grid.midi_out_count > 0 && latest.log_next > 0);
    send_command(CMD_STOP, 0, 0); settle(30); assert(!latest.grid.running);
    keyboard_press(70); settle(30); keyboard_release(); settle(30);
    assert(latest.grid.acid.learner.long_term.observations == 9);
    // Long press enters the same latched physical scale page through SDL release events.
    pointer(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 18, GRID_TOP + 9 * CELL + 18);
    settle(ACID_HOLD_MS + 50);
    pointer(SDL_MOUSEBUTTONUP, GRID_LEFT + 18, GRID_TOP + 9 * CELL + 18); settle(30);
    assert(latest.grid.acid.scale_page && !latest.grid.acid.pending_reset);
    pointer(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 2 * CELL + 18, GRID_TOP + 5 * CELL + 18);
    settle(ACID_HOLD_MS + 50);
    pointer(SDL_MOUSEBUTTONUP, GRID_LEFT + 2 * CELL + 18, GRID_TOP + 5 * CELL + 18); settle(30);
    assert(latest.grid.acid.patch.patterns[0].root == 2);
    char root_label[16]; assert(!strcmp(cell_label(&latest.grid, 2, 5, root_label), "D*"));
    uint16_t mask = latest.grid.acid.patch.scale;
    key(2, 4); assert(latest.grid.acid.patch.scale == (mask ^ 2));
    key(9, 0); settle(50); assert(!latest.grid.dirty); // Atomic bank save.
    key(2, 4); key(9, 1); settle(50);
    assert(latest.grid.acid.patch.scale == (mask ^ 2));
    for (unsigned i = 0; i < 8; ++i) assert(latest.grid.acid.patch.patterns[i].root == 2);
    assert(latest.grid.acid.patch.patterns[0].length[0] == 15);
    assert(latest.grid.acid.patch.patterns[0].length[1] == 7);
    assert(latest.grid.acid.patch.patterns[0].length[4] == 3);
    assert(latest.grid.acid.patch.patterns[0].length[5] == 5);
    assert(latest.grid.acid.patch.patterns[0].steps[ACID_CC][0] == 2);
    assert(!latest.grid.acid.scale_page); // Loading a bank returns to performance.
    uint8_t restored[16]; acid_gate_states(&latest.grid.acid.patch.patterns[0], restored);
    assert(restored[1] == GATE_LONG && restored[2] == GATE_TIED);
    pointer(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 18, GRID_TOP + 9 * CELL + 18);
    settle(ACID_HOLD_MS + 50);
    pointer(SDL_MOUSEBUTTONUP, GRID_LEFT + 18, GRID_TOP + 9 * CELL + 18); settle(30);
    assert(latest.grid.acid.scale_page);
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, WIDTH, HEIGHT);
    cairo_t *c = cairo_create(surface); draw(c, &latest);
    assert(cairo_surface_write_to_png(surface, "/tmp/c100-acid-scale.png") == CAIRO_STATUS_SUCCESS);
    key(0, 9); assert(!latest.grid.acid.scale_page);
    keyboard_event(SDLK_i); assert(inspect_learning); draw(c, &latest);
    assert(cairo_surface_write_to_png(surface, "/tmp/c100-acid-learning.png") == CAIRO_STATUS_SUCCESS);
    keyboard_event(SDLK_i); assert(!inspect_learning);
    key(0, 8); settle(160); assert(latest.grid.running);
    draw(c, &latest); assert(cairo_surface_write_to_png(surface, "/tmp/c100-acid-performance.png") == CAIRO_STATUS_SUCCESS);
    cairo_destroy(c); cairo_surface_destroy(surface);
    runtime_stop(&runtime);
    SDL_CloseAudioDevice(audio_device);
    assert(!atomic_load(&synth.dropped));
    unlink(path); SDL_Quit();
    cairo_debug_reset_static_data(); FcFini();
    puts("ok live SDL grid, holds, scales, paused learning, MIDI activity, bank save/load, live audio callback and one simulated instrument");
}
