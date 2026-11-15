// SPDX-License-Identifier: GPL-2.0-or-later
#define main simulator_application_main
#include "../simulator/main.c"
#undef main
#include <assert.h>
static Snapshot state;
static SDL_Window *window;
static double scale;
static int offset_x, offset_y;
static void pointer_event(Uint32 type, int x, int y) {
    SDL_Event event = {.type = type};
    int window_x = lround(x * scale + offset_x), window_y = lround(y * scale + offset_y);
    if (type == SDL_MOUSEMOTION) {
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = window_x;
        event.motion.y = window_y;
    } else {
        event.button.windowID = SDL_GetWindowID(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.x = window_x;
        event.button.y = window_y;
    }
    assert(SDL_PushEvent(&event) == 1);
    assert(!handle_events(&state));
}
static Command take_command(void) {
    assert(runtime.command_count == 1);
    Command c = runtime.commands[runtime.command_head];
    runtime.command_head = (runtime.command_head + 1) % 64;
    runtime.command_count = 0;
    return c;
}
static void verify_grid_targets(void) {
    cell_pressed = false;
    pointer_event(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 8 * CELL + 15, GRID_TOP + 7 * CELL + 15);
    Command c = take_command();
    assert(c.kind == CMD_PRESS && c.x == 8 && c.y == 7);
    pointer_event(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 5 * CELL + 15, GRID_TOP + 9 * CELL + 15);
    c = take_command();
    assert(c.kind == CMD_PRESS && c.x == 5 && c.y == 9);
    pointer_event(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 3 * 61 + 28, 678);
    c = take_command();
    assert(c.kind == CMD_STEP);
    pointer_event(SDL_MOUSEBUTTONDOWN, GRID_LEFT + 464, 210);
    c = take_command();
    assert(c.kind == CMD_TEMPO && c.x == 120);
    pointer_event(SDL_MOUSEMOTION, GRID_LEFT + 464, 180);
    c = take_command();
    assert(c.kind == CMD_TEMPO && c.x >= 149 && c.x <= 151);
    cell_pressed = false;
    pointer_event(SDL_MOUSEBUTTONUP, GRID_LEFT + 464, 180);

}
static void verify_keyboard_target(void) {
    pointer_event(SDL_MOUSEBUTTONDOWN, KEYBOARD_X + 8 * WHITE_KEY_W, KEYBOARD_Y + 20);
    Command c = take_command();
    assert(c.kind == CMD_MIDI && c.midi[0] == 0x90 && c.midi[1] == 61);
    pointer_event(SDL_MOUSEBUTTONUP, KEYBOARD_X + 8 * WHITE_KEY_W, KEYBOARD_Y + 20);
    c = take_command();
    assert(c.midi[0] == 0x80 && c.midi[1] == 61);
}
static void verify_size(int width, int height) {
    window = SDL_CreateWindow("Coordinate test", 0, 0, WIDTH, HEIGHT,
                              SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    assert(window);
    SDL_Renderer *renderer = create_canvas(window);
    assert(renderer);
    SDL_SetWindowSize(window, width, height);
    SDL_PumpEvents();
    SDL_GetWindowSize(window, &width, &height);
    scale = fmin((double)width / WIDTH, (double)height / HEIGHT);
    offset_x = (width - lround(WIDTH * scale)) / 2;
    offset_y = (height - lround(HEIGHT * scale)) / 2;
    // The colored rendered target must occupy the same physical position that
    // produces the expected grid command, including letterbox offsets.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 255, 0, 0, 255);
    SDL_Rect target = {GRID_LEFT + 8 * CELL, GRID_TOP + 7 * CELL, 38, 38};
    SDL_RenderFillRect(renderer, &target);
    int rw, rh;
    SDL_GetRendererOutputSize(renderer, &rw, &rh);
    int px = lround(((target.x + 15) * scale + offset_x) * rw / width);
    int py = lround(((target.y + 15) * scale + offset_y) * rh / height);
    SDL_Rect sample = {px, py, 1, 1};
    uint32_t pixel = 0;
    assert(!SDL_RenderReadPixels(renderer, &sample, SDL_PIXELFORMAT_ARGB8888, &pixel, sizeof(pixel)));
    assert((pixel & 0x00ffffff) == 0x00ff0000);
    verify_grid_targets();
    verify_keyboard_target();
    if (offset_x > 0)
        pointer_event(SDL_MOUSEBUTTONDOWN, -50, 200);
    if (offset_y > 0)
        pointer_event(SDL_MOUSEBUTTONDOWN, 300, -50);
    assert(!runtime.command_count); // Letterbox space cannot activate a control.
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
}
int main(void) {
    assert(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) == 0);
    pthread_mutex_init(&runtime.commands_lock, NULL);
    grid_init(&state.grid, 1);
    verify_size(1160, 1040);
    verify_size(580, 520);
    verify_size(870, 780);
    verify_size(1450, 1040);
    verify_size(1160, 1300);
    pthread_mutex_destroy(&runtime.commands_lock);
    SDL_Quit();
    puts("ok rendered targets and mouse events agree at 50%, 75%, 100%, wide and tall letterboxed sizes");
}
