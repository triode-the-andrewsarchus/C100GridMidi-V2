# Third-party notices

The USB descriptor and high/full-speed MIDI transport adaptations in
`keyboards/keychron/c100_8k/keymaps/grid_logic/usb_descriptor_midi.c`,
`usb_midi_transport.c`, `usb_midi_transport.h`, and the corresponding
`tests/usb/` fixtures and `tests/test_usb_descriptors.c` are from
[roguefort-dev/Keypad-keychron-c100-midi-pad](https://github.com/roguefort-dev/Keypad-keychron-c100-midi-pad)
(commit `310f834a1dced3f25d957da1a40ba56b1e633388`).
Copyright 2026 roguefort-dev. GPL-2.0-or-later, as indicated in those files.

QMK and the Keychron board implementation are external build dependencies,
licensed by their respective authors. This project's firmware and portable
engine are GPL-2.0-or-later. See LICENSE. No code from c100ctl, Orca, Signls or
Monome is included.
