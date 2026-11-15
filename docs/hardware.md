# Hardware and flashing

## Supported target

This project targets the **Keychron C100 8K**, a 10×10 matrix with 100 RGB keys,
AT32F405 MCU and an AT32 DFU bootloader. Other C100 variants or revisions are not
assumed compatible. The application USB identity is **3434:042c**; its bootloader
appears as **2e3c:df11**.

The pinned [Keychron board source](https://github.com/Keychron/qmk_firmware/tree/57e682caa0a821e6de1b589e6f983f266151ab02/keyboards/keychron/c100_8k)
defines the matrix, NKRO support and two SNLED27351 LED drivers. The firmware
reads matrix coordinates before Keychron's corner lighting/factory-reset
handlers, making all 100 positions available. RGB uses the board's native
matrix-to-LED mapping. No host RGB service, VIA or Vial is required.

The USB descriptor and high/full-speed transport adaptations come from
[Keypad-keychron-c100-midi-pad](https://github.com/roguefort-dev/Keypad-keychron-c100-midi-pad/tree/310f834a1dced3f25d957da1a40ba56b1e633388).
See [third-party notices](../THIRD_PARTY_NOTICES.md).

## USB MIDI and orientation

Each unit runs its own sequencer. A USB host/router supplies power and routes
its MIDI output to a synth, and optional notes/clock to its MIDI input. The
keypad is not a USB host. It does not forward incoming notes, clocks or CCs.
Avoid routing generated output back into the same input.

The firmware exposes MIDI input and output alongside its retained HID
interfaces. Use USB serial identity or stable physical-port aliases when routing
multiple units; VID/PID alone cannot distinguish them. Multi-unit serial
uniqueness has not been validated.

`GRID_ROTATION=0|90|180|270` rotates key input and RGB together. The repository
default is **180°**. This setting describes the physical board's clockwise turn
from the factory orientation; the musical controls keep the layout shown in
[the control reference](controls.md).

## Build and install

1. Build the exact target with the pinned QMK source and ARM toolchain in
   [the build guide](build.md#firmware-build). Run `make check` and `make test-usb`.
2. Keep a known recovery image or full-flash backup. Confirm the bootloader entry
   procedure for your exact unit, then connect it in DFU mode. The reference
   project describes a top-left-held-at-plug-in procedure; this has not been
   independently verified for every revision.
3. Identify the intended device with `lsusb` and `dfu-util -l -d 2e3c:df11`.
   Confirm alternate 0 is internal flash at `0x08000000`. If multiple matching
   devices are connected, select the intended physical path with `dfu-util -p`.
4. Stop MIDI playback and flash the built image from the repository root:

   ```sh
   dfu-util -d 2e3c:df11 -a 0 -s 0x08000000:leave \
     -D keychron_c100_8k_grid_logic.bin
   ```

5. Confirm the device returns as **Keychron C100 8K** (`3434:042c`) and its MIDI
   input/output appear. Check controls, colors and orientation before using it
   in a performance. See [validation](validation.md) for automated coverage.

Build and test targets never flash automatically. Flashing replaces the running
firmware. Save any banks/settings you want to retain; raw flash backups and
local banks are intentionally ignored by Git.

## Observed on-device validation

On **2026-11-15**, one unit was identified in AT32 DFU mode and its complete
262144-byte internal flash was backed up before each of two authorized flashes.
The first image was used musically and the user confirmed it worked. The second
installed hold-to-tie and 180° rotation.

The second image was 74660 bytes including its DFU suffix (74644-byte payload),
SHA256 `dde557f922319a440b543cfc63f724d3f53dbfabe34b664fb6841c75d827353c`.
Both downloads completed successfully. On this unit `dfu-util` then returned
an error during its final leave-status query as the device rebooted. Successful
application re-enumeration was checked afterward; do not treat a transfer error
before completion as equivalent evidence.

The same USB port and serial returned at **480 Mbit/s** with AudioControl and
MIDIStreaming interfaces, 512-byte MIDI IN/OUT endpoints and an ALSA bidirectional
MIDI port. A raw MIDI Stop write succeeded. Backups remain local; unit serials
and workstation-specific port addresses are not part of this repository.

Exhaustive matrix/LED orientation, rollover, timing jitter and latest-gesture
validation remain open. Stress testing should include clock plus incoming notes
and generated output, blocked host output, pause/reset, suspend, unplug/reconnect,
persistence and timer rollover. No firmware can deliver Note Off through an
unplugged cable; receivers should handle disconnect or provide panic recovery.
