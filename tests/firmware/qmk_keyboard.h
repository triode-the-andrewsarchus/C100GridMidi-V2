// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "grid.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define MATRIX_ROWS 10
#define MATRIX_COLS 10
#define PROGMEM
#define KC_F13 0x68
#define NO_LED 255
#define RGB_MATRIX_CUSTOM_GRID_CANVAS 1
#define EECONFIG_USER_DATA_SIZE GRID_PATCH_BYTES
#define USB_ENDPOINT_IN_COUNT 1
#define USB_ENDPOINT_IN_MIDI 0
#define USB_ACTIVE 1
#define TIME_IMMEDIATE 0
#define MSG_OK 0
typedef struct {
    struct {
        struct {
            uint8_t row, col;
        } key;
        bool pressed;
    } event;
} keyrecord_t;
typedef struct {
    uint8_t matrix_co[10][10];
} led_config_t;
extern led_config_t g_led_config;
typedef struct {
    uint8_t Event, Data1, Data2, Data3;
} MIDI_EventPacket_t;
typedef struct {
    int unused;
} MidiDevice;
extern MidiDevice midi_device;
typedef struct {
    uint8_t buffer[512];
    uint8_t *ptr;
    bool suspended;
    unsigned available;
} output_buffers_queue_t;
typedef struct {
    output_buffers_queue_t obqueue;
} usb_endpoint_in_t;
extern usb_endpoint_in_t usb_endpoints_in[1];
extern int fake_usb_state;
#define USB_DRIVER fake_usb_state
uint32_t timer_read32(void);
void osalSysLock(void);
void osalSysUnlock(void);
int usbGetDriverStateI(const int *driver);
bool bqIsSuspendedX(output_buffers_queue_t *queue);
bool obqIsFullI(output_buffers_queue_t *queue);
int obqGetEmptyBufferTimeoutS(output_buffers_queue_t *queue, int timeout);
void obqPostFullBufferS(output_buffers_queue_t *queue, size_t size);
bool recv_midi_packet(MIDI_EventPacket_t *event);
void midi_device_set_pre_input_process_func(MidiDevice *device, void (*callback)(MidiDevice *));
bool eeconfig_is_user_datablock_valid(void);
uint32_t eeconfig_read_user_datablock(void *data, uint32_t offset, uint32_t length);
uint32_t eeconfig_update_user_datablock(const void *data, uint32_t offset, uint32_t length);
void rgb_matrix_enable_noeeprom(void);
void rgb_matrix_mode_noeeprom(unsigned mode);
void rgb_matrix_set_color(unsigned led, uint8_t r, uint8_t g, uint8_t b);
