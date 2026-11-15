# Contributing

Keep the instrument playable on a single grid and useful without incoming
musical notes. Preserve repeating phrase memory, hard scale constraints,
recoverable base patterns and reliable note release.

## Source map

| Path | Purpose |
| --- | --- |
| `core/grid.*` | Shared instrument interface, bounded MIDI queue and panic handling |
| `core/acid.*` | Sequencing, physical gestures, lane policies, timing, voices and RGB |
| `core/phrase.c` | Scale-constrained phrase generation and persistent mutation |
| `core/learning.c` | Transport-independent statistical vocabulary |
| `core/acid_patch.c` | Validated, versioned binary bank storage |
| `simulator/` | Linux SDL/Cairo UI, ALSA worker, local audio and JSON persistence |
| `keyboards/keychron/c100_8k/keymaps/grid_logic/` | QMK matrix/RGB, rotation and USB MIDI integration |
| `tests/` | Portable engine, simulator, firmware adapter and USB descriptor checks |

## Working locally

Install the dependencies in [the README](README.md#try-the-simulator), then run
from the repository root:

```sh
make check
make test-sanitize
make check SEQUENCE_CC=12
```

`make test-firmware` uses QMK fixtures to check key input, RGB, holds and storage
at all four rotations. The separate `make test-usb QMK_HOME=/path/to/qmk_firmware`
target requires the pinned Keychron checkout and ARM toolchain; it builds real
firmware and checks the actual descriptor resolver. Neither target flashes.

Run host checks, sanitizers and the alternate sequence-CC configuration locally.
Live ALSA integration returns a documented skip when `/dev/snd/seq` is absent;
it should also be exercised locally with a working ALSA sequencer.

## Changes and validation

Reuse the USB, MIDI, rendering and persistence infrastructure. Keep allocations,
blocking I/O, file writes and GUI work out of the musical clock path. Rendering
must not consume random numbers or mutate musical state. Add deterministic tests
for changes to timing, generation, learning, physical gestures or note lifetimes.

Keep stored base patterns separate from temporary variation and working phrase
memory. Preserve existing bank migration or explicitly document a format change.
Update the control and build docs when user-visible behavior changes.

Do not commit local banks, raw firmware backups, toolchain checkouts or compiled
firmware. Existing source headers and third-party notices must remain intact.
