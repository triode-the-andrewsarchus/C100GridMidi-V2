// SPDX-License-Identifier: GPL-2.0-or-later
#include "qmk_keyboard.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
// Compile the real adapter in this translation unit so tests can inspect its
// private instrument, without adding test-only production accessors.
#include "../keyboards/keychron/c100_8k/keymaps/grid_logic/keymap.c"
led_config_t g_led_config;
MidiDevice midi_device;
usb_endpoint_in_t usb_endpoints_in[1];
int fake_usb_state = USB_ACTIVE;
static unsigned locks, packets_sent, rx_head, rx_count;
static uint32_t fake_now;
static bool valid_storage;
static uint8_t eeprom[GRID_PATCH_BYTES], last_packet[4];
static Color leds[100];
static MIDI_EventPacket_t rx[128];
static void (*receiver)(MidiDevice *);
uint32_t timer_read32(void) {
    return fake_now;
}
void osalSysLock(void) {
    assert(!locks++);
}
void osalSysUnlock(void) {
    assert(locks-- == 1);
}
int usbGetDriverStateI(const int *driver) {
    assert(locks);
    return *driver;
}
bool bqIsSuspendedX(output_buffers_queue_t *q) {
    return q->suspended;
}
bool obqIsFullI(output_buffers_queue_t *q) {
    return q->available == 0;
}
int obqGetEmptyBufferTimeoutS(output_buffers_queue_t *q, int timeout) {
    assert(locks && timeout == TIME_IMMEDIATE && q->available);
    q->ptr = q->buffer;
    return MSG_OK;
}
void obqPostFullBufferS(output_buffers_queue_t *q, size_t size) {
    assert(locks && size == 4 && q->available);
    --q->available;
    ++packets_sent;
    memcpy(last_packet, q->buffer, 4);
}
bool recv_midi_packet(MIDI_EventPacket_t *event) {
    if (!rx_count)
        return false;
    *event = rx[rx_head++];
    --rx_count;
    return true;
}
void midi_device_set_pre_input_process_func(MidiDevice *device, void (*callback)(MidiDevice *)) {
    assert(device == &midi_device);
    receiver = callback;
}
bool eeconfig_is_user_datablock_valid(void) {
    return valid_storage;
}
uint32_t eeconfig_read_user_datablock(void *data, uint32_t offset, uint32_t length) {
    assert(offset + length <= sizeof(eeprom));
    memcpy(data, eeprom + offset, length);
    return length;
}
uint32_t eeconfig_update_user_datablock(const void *data, uint32_t offset, uint32_t length) {
    assert(!instrument.running && offset + length <= sizeof(eeprom));
    memcpy(eeprom + offset, data, length);
    valid_storage = true;
    return length;
}
void rgb_matrix_enable_noeeprom(void) {}
void rgb_matrix_mode_noeeprom(unsigned mode) {
    assert(mode == RGB_MATRIX_CUSTOM_GRID_CANVAS);
}
void rgb_matrix_set_color(unsigned led, uint8_t r, uint8_t g, uint8_t b) {
    assert(led < 100);
    leds[led] = (Color){r, g, b};
}
static unsigned physical_cell(unsigned x, unsigned y) {
    // Inverse mapping: where a logical cell lands when the board is turned.
    switch (GRID_ROTATION) {
    case 90: return (9 - x) * 10 + y;
    case 180: return (9 - y) * 10 + 9 - x;
    case 270: return x * 10 + 9 - y;
    default: return y * 10 + x;
    }
}
static keyrecord_t key_event(unsigned x, unsigned y) {
    unsigned cell = physical_cell(x, y);
    keyrecord_t event = {.event = {.key = {.row = cell / 10, .col = cell % 10}, .pressed = true}};
    return event;
}
static void press(unsigned x, unsigned y) {
    keyrecord_t event = key_event(x, y);
    assert(!pre_process_record_user(KC_F13, &event));
    event.event.pressed = false;
    assert(!pre_process_record_user(KC_F13, &event));
}
static void hold(unsigned x, unsigned y) {
    keyrecord_t event = key_event(x, y);
    assert(!pre_process_record_user(KC_F13, &event));
    fake_now += ACID_HOLD_MS; matrix_scan_user();
    event.event.pressed = false;
    assert(!pre_process_record_user(KC_F13, &event));
}
static void test_gate_tie_buttons(void) {
    AcidPattern *p = &instrument.acid.patch.patterns[0];
    hold(1, 0); assert(p->steps[0][0] == GATE_LONG && !p->steps[0][1]);
    Color frame[100]; grid_render(&instrument, frame); assert(frame[2].r > frame[2].g);
    press(2, 0); assert(p->steps[0][0] == GATE_ON && !p->steps[0][1]);
    hold(1, 0); // Persist this tie through the existing EEPROM test.
}
int main(void) {
    for (unsigned row = 0; row < 10; ++row)
        for (unsigned col = 0; col < 10; ++col) g_led_config.matrix_co[row][col] = 99 - (row * 10 + col);
    keyboard_post_init_user(); assert(receiver && !instrument.running);
    instrument.acid.patch.internal_clock = false;
    // Early hooks consume all 100 coordinates, preserving the exact 80-step layout.
    static const unsigned states[] = {2, 2, 2, 4, 6, 3};
    for (unsigned lane = 0; lane < ACID_LANES; ++lane) {
        unsigned last = acid_lane_capacity[lane] - 1, row = acid_lane_rows[lane] + last / 8;
        uint8_t before = instrument.acid.patch.patterns[0].steps[lane][last];
        press(8, row);
        assert(instrument.acid.patch.patterns[0].steps[lane][last] == (before + 1) % states[lane]);
        press(9, acid_lane_rows[lane]); assert(instrument.acid.length_select == (int)lane);
        press(8, row); assert(instrument.acid.length_select == -1);
    }
    test_gate_tie_buttons();
    // Receive stays bounded and continues with a blocked transmit endpoint.
    usb_endpoints_in[0].obqueue.available = 0;
    rx[0] = (MIDI_EventPacket_t){15, 0xfa, 0, 0};
    for (unsigned i = 1; i < 65; ++i) rx[i] = (MIDI_EventPacket_t){15, 0xf8, 0, 0};
    rx_count = 65;
    receiver(&midi_device); assert(rx_count == 33 && instrument.midi_in_count == 32);
    receiver(&midi_device); receiver(&midi_device);
    assert(!rx_count && instrument.midi_in_count == 65 && instrument.tick == 11);
    assert(instrument.tx_count); grid_usb_drain(&instrument); assert(!packets_sent);
    // USB loss stops the voice and retains releases for reconnection.
    fake_usb_state = USB_ACTIVE; matrix_scan_user(); fake_usb_state = 0; matrix_scan_user();
    assert(!instrument.running);
    for (unsigned i = 0; i < instrument.tx_count; ++i)
        assert((instrument.tx[(instrument.tx_head + i) % GRID_TX_CAP].bytes[0] & 0xf0) == 0x80);
    // Hold Reset opens scale without resetting, then release leaves it latched.
    keyrecord_t event = key_event(0, 9);
    pre_process_record_user(KC_F13, &event); fake_now += ACID_HOLD_MS; matrix_scan_user();
    assert(instrument.acid.scale_page && !instrument.acid.pending_reset);
    event.event.pressed = false; pre_process_record_user(KC_F13, &event);
    assert(instrument.acid.scale_page);
    event = key_event(2, 5);
    pre_process_record_user(KC_F13, &event); fake_now += ACID_HOLD_MS; matrix_scan_user();
    event.event.pressed = false; pre_process_record_user(KC_F13, &event);
    assert(instrument.acid.patch.patterns[7].root == 2 && (instrument.acid.patch.scale & 4));
    press(9, 0); matrix_scan_user(); assert(valid_storage && !instrument.dirty);
    uint16_t scale = instrument.acid.patch.scale;
    assert(acid_scale(&instrument, 1)); press(9, 1); matrix_scan_user();
    assert(instrument.acid.patch.scale == scale && instrument.acid.patch.patterns[0].root == 2);
    eeprom[10] ^= 1; assert(!load_patch()); eeprom[10] ^= 1;
    keyboard_post_init_user(); assert(instrument.acid.patch.scale == scale);
    assert(instrument.acid.patch.patterns[0].steps[0][0] == GATE_LONG);
    fake_now += 1000; rgb_matrix_indicators_user();
    Color frame[100]; grid_render(&instrument, frame);
    for (unsigned i = 0; i < 100; ++i) assert(!memcmp(&frame[i], &leds[99 - physical_cell(i % 10, i / 10)], sizeof(Color)));
    fake_usb_state = USB_ACTIVE; usb_endpoints_in[0].obqueue.available = 16;
    matrix_scan_user(); assert(!locks);
    printf("ok acid firmware rotation %d: matrix, RGB mapping, holds, USB duplex, disconnect and EEPROM\n", GRID_ROTATION);
}
