// SPDX-License-Identifier: GPL-2.0-or-later
#include QMK_KEYBOARD_H
#include "grid_usb.h"
#include "midi.h"
#include "qmk_midi.h"
#include "usb_driver.h"
#include "usb_endpoints.h"
#include <string.h>
extern usb_endpoint_in_t usb_endpoints_in[USB_ENDPOINT_IN_COUNT];
static Grid *instrument;

bool grid_usb_ready(void) {
    osalSysLock();
    bool ready = usbGetDriverStateI(&USB_DRIVER) == USB_ACTIVE;
    osalSysUnlock();
    return ready;
}
static void receive_bounded(MidiDevice *device) {
    (void)device;
    MIDI_EventPacket_t event;
    for (unsigned budget = 0; budget < 32 && recv_midi_packet(&event); ++budget) {
        uint8_t bytes[] = {event.Data1, event.Data2, event.Data3};
        uint8_t cin = event.Event & 15;
        if (cin == 15)
            grid_midi(instrument, bytes, 1, timer_read32());
        else if (cin >= 8 && cin <= 14)
            grid_midi(instrument, bytes, (cin == 12 || cin == 13) ? 2 : 3, timer_read32());
    }
}
void grid_usb_init(Grid *grid) {
    instrument = grid;
    // Parse each USB event immediately. QMK's default adapter first drains all
    // USB packets into a small byte FIFO; bursts can overflow before parsing.
    midi_device_set_pre_input_process_func(&midi_device, receive_bounded);
}
static bool try_packet(const MidiMessage *m) {
    uint8_t packet[] = {m->bytes[0] >> 4, m->bytes[0], m->bytes[1], m->bytes[2]};
    output_buffers_queue_t *q = &usb_endpoints_in[USB_ENDPOINT_IN_MIDI].obqueue;
    osalSysLock();
    // Do not call usb_endpoint_in_send: upstream retries indefinitely after
    // an incomplete write, even with TIME_IMMEDIATE. Own one whole buffer.
    if (usbGetDriverStateI(&USB_DRIVER) != USB_ACTIVE || bqIsSuspendedX(q) || obqIsFullI(q)) {
        osalSysUnlock();
        return false;
    }
    if (obqGetEmptyBufferTimeoutS(q, TIME_IMMEDIATE) != MSG_OK) {
        osalSysUnlock();
        return false;
    }
    memcpy(q->ptr, packet, sizeof(packet));
    obqPostFullBufferS(q, sizeof(packet));
    osalSysUnlock();
    return true;
}
void grid_usb_drain(Grid *grid) {
    for (unsigned budget = 0; budget < 16; ++budget) {
        const MidiMessage *m = grid_tx_peek(grid);
        if (!m || !try_packet(m))
            return;
        grid_tx_ack(grid);
    }
}
