// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "grid.h"
#include "grid_usb.h"
#include <string.h>

// Nonzero keycodes ensure QMK calls the physical event hook. All are consumed
// before keyboard-specific corner reset/lighting code and before host reports.
#define ROW {KC_F13, KC_F13, KC_F13, KC_F13, KC_F13, KC_F13, KC_F13, KC_F13, KC_F13, KC_F13}
#define PROGRAM {ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW, ROW}
const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {PROGRAM, PROGRAM, PROGRAM, PROGRAM};
#ifndef GRID_RENDER_INTERVAL_MS
#define GRID_RENDER_INTERVAL_MS 10
#endif
static Grid instrument;
static Color frame[100];
static uint8_t storage[GRID_PATCH_BYTES];
static uint32_t rendered_at;
static bool was_ready;
static uint32_t storage_notice_until;
static bool storage_ok;
_Static_assert(GRID_PATCH_BYTES <= EECONFIG_USER_DATA_SIZE, "Patch exceeds user EEPROM");

static bool load_patch(void) {
    static Patch loaded;
    if (!eeconfig_is_user_datablock_valid())
        return false;
    eeconfig_read_user_datablock(storage, 0, sizeof(storage));
    return grid_patch_decode(&loaded, storage, sizeof(storage)) && grid_apply_patch(&instrument, &loaded);
}
void keyboard_post_init_user(void) {
    grid_init(&instrument, 0xC100u);
    load_patch();
    grid_usb_init(&instrument);
    rgb_matrix_enable_noeeprom();
    rgb_matrix_mode_noeeprom(RGB_MATRIX_CUSTOM_GRID_CANVAS);
}
#ifndef GRID_ROTATION
#define GRID_ROTATION 180
#endif
#if GRID_ROTATION != 0 && GRID_ROTATION != 90 && GRID_ROTATION != 180 && GRID_ROTATION != 270
#error "GRID_ROTATION must be 0, 90, 180 or 270 degrees"
#endif
// One physical-to-logical mapping serves both key events and their LED colors.
static unsigned logical_cell(unsigned col, unsigned row) {
#if GRID_ROTATION == 90
    return col * GRID_W + (GRID_W - 1 - row);
#elif GRID_ROTATION == 180
    return (GRID_W - 1 - row) * GRID_W + (GRID_W - 1 - col);
#elif GRID_ROTATION == 270
    return (GRID_W - 1 - col) * GRID_W + row;
#else
    return row * GRID_W + col;
#endif
}
bool pre_process_record_user(uint16_t keycode, keyrecord_t *record) {
    (void)keycode;
    unsigned cell = logical_cell(record->event.key.col, record->event.key.row);
    grid_key(&instrument, cell % GRID_W, cell / GRID_W,
             record->event.pressed, timer_read32());
    return false;
}
void matrix_scan_user(void) {
    uint32_t now = timer_read32();
    bool ready = grid_usb_ready();
    if (was_ready && !ready)
        grid_stop(&instrument);
    was_ready = ready;
    grid_poll(&instrument, now);
    grid_usb_drain(&instrument);
    // Persistence can cause flash stalls. Only explicit stopped saves/loads,
    // after releases have entered USB's queue, may touch EEPROM. Offline,
    // retain releases for reconnection but do not make local storage wait on USB.
    if (instrument.action != ACTION_NONE && !instrument.running &&
        (!ready || !instrument.tx_count)) {
        if (instrument.action == ACTION_SAVE) {
            grid_patch_encode(&instrument.acid.patch, storage);
            storage_ok = eeconfig_update_user_datablock(storage, 0, sizeof(storage)) == sizeof(storage);
            if (storage_ok)
                instrument.dirty = false;
        } else
            storage_ok = load_patch();
        instrument.action = ACTION_NONE;
        storage_notice_until = now + 700;
    }
}
bool rgb_matrix_indicators_user(void) {
    uint32_t now = timer_read32();
    if ((uint32_t)(now - rendered_at) < GRID_RENDER_INTERVAL_MS)
        return false;
    rendered_at = now;
    grid_render(&instrument, frame);
    if ((int32_t)(storage_notice_until - now) > 0)
        frame[0] = storage_ok ? (Color){220, 220, 220} : (Color){12, 12, 12};
    for (uint8_t row = 0; row < 10; ++row)
        for (uint8_t col = 0; col < 10; ++col) {
            uint8_t led = g_led_config.matrix_co[row][col];
            if (led != NO_LED) {
                Color c = frame[logical_cell(col, row)];
                rgb_matrix_set_color(led, c.r, c.g, c.b);
            }
        }
    return false;
}
void suspend_power_down_user(void) {
    grid_stop(&instrument);
}
