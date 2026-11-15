# Validation

The instrument and simulator use one grid, six independently sized lanes,
a global scale/root and a combined pitch/octave lock. The latest hardware build
includes hold-to-tie and 180° physical rotation.

## Automated coverage

| Area | Coverage |
| --- | --- |
| Layout | All 100 coordinates; 80 step keys across capacities 16/16/16/16/8/8 |
| Polymeter | Independent endpoints, shrink/expand, length one, Gate 15 / Accent 7 realignment after 105 steps |
| Timing | Six-clock sixteenths, quantized transport/reset/patterns, internal phase, MIDI Start/Continue/Stop |
| Gate ties | Tap toggles; 450 ms holds; simultaneous holds; release consumption; timer rollover; reversible covered values; circular ties and live cancellation |
| Policies | Cyclic inheritance within active lengths, hidden-step exclusion and all-Off defaults |
| Scale and locks | Global hard scale, nonempty mask, all-pattern requantization, root gestures, independent CC playback while pitch/octave are frozen |
| Generation | Seeded determinism, repetition, deliberate leaps, root/fifth preference, compact register, gradual persistent mutations and base recovery |
| Learning | Recent/long-term blending, paused learning, signed intervals, motifs, chord grouping, echo filtering and deterministic replay |
| Voices | Accents, same-pitch legato, slide overlap, exact clock-counted ties, missing-clock watchdog and 12,000-clock balanced-release stress |
| Storage | Binary/JSON roundtrips, hidden covered values, format-1/2 migration and invalid-bank rejection |
| QMK adapter | Matrix events and RGB at all four rotations, holds, bounded USB duplex, disconnect and EEPROM restoration |
| Simulator | Actual SDL input/rendering, scale page, save/load, threaded engine, diagnostics and pointer alignment at scaled/letterboxed sizes |
| Audio | Three CC cutoffs, accents, sustained oscillator, glide, tie duration, channel filtering and bounded output |
| USB | Actual QMK descriptor resolver, high/full-speed endpoints, restart/reset, callbacks and HID/string passthrough |
| Memory safety | AddressSanitizer and UndefinedBehaviorSanitizer |

Run from the repository root:

```sh
make check
make test-sanitize
make check SEQUENCE_CC=12
make test-usb QMK_HOME=../qmk_firmware GRID_ROTATION=180
```

The first three run in CI. Live ALSA integration skips when `/dev/snd/seq` is
unavailable; its successful local run received **352 incoming events** while
sending **19 attacks with balanced releases** and **50 CC messages**. Hardware
and USB descriptor builds require the pinned Keychron checkout and ARM toolchain.
No test target flashes a device.

Deterministic bassline checks cover 200 seeds and 64 Green evolution loops.
Root/fifth pitch classes account for about 84% of sounding positions with
factory Gate patterns, while other allowed degrees survive. After 1,000 C inputs
then 100 G inputs, the blended learned weights favor recent G (0.344) over C
(0.284). These are tuning checks, not perceptual quality guarantees.

The hold-to-tie/rotation revision passed all checks on 2026-11-15. Its
architecture comparison introduced no new clone groups or dependency cycles;
maximum measured function complexity remained 15.

## Hardware evidence and limits

The 180° image was flashed to one C100 8K on 2026-11-15. It re-enumerated with
bidirectional USB MIDI, and a MIDI Stop write succeeded. The user confirmed
musical operation of the preceding firmware revision. See
[hardware evidence and flashing](hardware.md) for details.

Exhaustive real-device timing, rollover, all-key/RGB orientation and the latest
hold gesture/rotated ergonomics still need performer validation. External
portamento depends on the receiving synth; MIDI overlap alone does not guarantee
audible glide. Swing, metric-position learning, pitch-bend slide and a dedicated
undo/history UI are not implemented.
