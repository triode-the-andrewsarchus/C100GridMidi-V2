# Build and routing

Run all `make` and simulator commands from the repository root, beside the
`Makefile`. The simulator requires Linux, C11, Make, pkg-config, SDL2, Cairo,
json-c, ALSA and pthreads. UI tests also use Fontconfig. On Debian or Ubuntu:

```sh
sudo apt install build-essential pkg-config libsdl2-dev libcairo2-dev \
  libjson-c-dev libasound2-dev libfontconfig1-dev
```

```sh
make check
make test-sanitize
make simulator
./.build/grid-simulator --demo --play
```

The simulator runs **one 10×10 grid**, one transport and one pair of MIDI ports.
The diagnostics beside the grid show its phrase memory and learning. It needs
no external MIDI notes to generate music and no physical keyboard to simulate.

```sh
## Internal playback and optional local audio.
./.build/grid-simulator --demo --play --bpm 128

## External Start/Continue/Stop/Clock and MIDI routing.
./.build/grid-simulator --external-clock --in 20:0 --out 24:0 --channel 1

## Offline rendering and headless operation.
SDL_VIDEODRIVER=dummy ./.build/grid-simulator --no-midi --no-audio \
  --play --run-ms 2000 --capture /tmp/acid.png
./.build/grid-simulator --headless --no-midi --no-audio --play --run-ms 2000
```

Options: `--patch file.json`, `--in client:port`, `--out client:port`,
`--channel 1..16`, `--bpm 30..300`, `--external-clock`, `--note-length 1..100`,
`--overlap-ms 0..500`, `--accent-cc 0..127`, `--seed N`, `--no-learning`,
`--headless`, `--no-midi`, `--no-audio`, `--run-ms`, `--capture`, `--demo` and
`--play`. Explicit configuration overrides loaded bank settings. Default storage
is `grid.json`. `--demo` starts with a fresh bank; `--play` starts transport.
Turning the tempo knob selects internal timing; EXT/INT toggles clock source.

The ALSA worker receives bounded batches and transmits queued notes and CCs
concurrently. Port addresses and subscription errors appear in the GUI and
stderr. There is no MIDI thru. Avoid routing generated output back into input;
own-client events and short matching echoes are filtered, but delayed indirect
loops are not always identifiable.

The optional local synth is a fixed monophonic acid preview with a band-limited
saw, driven four-pole resonant low-pass filter, a decaying filter envelope and
velocity accents. Overlapping notes glide (~60 ms to settle) without restarting
the envelopes; ordinary attacks retrigger. The CC lane selects moderate base
cutoffs: **0/red = 350 Hz, 64/yellow = 800 Hz, 127/green = 1.8 kHz**. The
filter envelope opens above those base values, with stronger sweeps on accents.
Cutoff changes are smoothed. The preview follows the compiled `SEQUENCE_CC`
number and works on every output channel with the same sound. There are no
additional synth controls or saved synth settings. It approximates acid behavior,
not the TB-303 circuit, and does not affect external MIDI output. Press **I** for
interval/motif details; Escape stops the instrument. Save/Load, BASE and NEW
mirror the physical Scale page. Grid L16/L8 buttons arm endpoint selection and
show END? until a step is chosen. No permanent rows are reserved for editing.

## Build-time CC assignment

The independent eight-step CC lane sends 0, 64 or 127 every sixteenth, including
rests. Its controller number is fixed at build time, default **74**:

```sh
make simulator SEQUENCE_CC=12
make check SEQUENCE_CC=12
```

The make setting becomes `ACID_SEQUENCE_CC` in the portable core. Values outside
0–127 fail compilation. Changing `SEQUENCE_CC` causes host targets to rebuild;
QMK receives the same setting. This lane's controller is separate from the
optional accent CC configured through `--accent-cc`.

## Firmware build

Firmware targets **Keychron C100 8K / AT32F405**. Use the Keychron `2025q3`
checkout pinned to `57e682caa0a821e6de1b589e6f983f266151ab02`; upstream QMK
or a similarly named board is not a substitute. Prepare a sibling checkout:

```sh
git clone --branch 2025q3 --single-branch https://github.com/Keychron/qmk_firmware.git ../qmk_firmware
git -C ../qmk_firmware checkout 57e682caa0a821e6de1b589e6f983f266151ab02
git -C ../qmk_firmware submodule update --init --recursive
```

Install the QMK CLI, its Python requirements and an ARM embedded toolchain.
The pinned checkout's `docs/newbs_getting_started.md` and `util/qmk_install.sh`
describe platform setup. Use a Python virtual environment if your distribution
requires one; ensure `qmk` and `arm-none-eabi-gcc` are on `PATH`.
Then, from **this repository**, build and check:

```sh
make firmware QMK_HOME=../qmk_firmware SEQUENCE_CC=74 GRID_ROTATION=180
make test-usb QMK_HOME=../qmk_firmware GRID_ROTATION=180
```

`QMK_HOME` defaults to `../qmk_firmware`. The retained target name is
`keychron/c100_8k:grid_logic`. The image is copied to
`keychron_c100_8k_grid_logic.bin`; these commands build and do not flash.
A 4 KiB process stack accommodates bounded generation and validated decoding.
USB tests compile against the actual QMK descriptor implementation and retained
high/full-speed adapter. One physical keyboard operates as the full instrument;
a USB host/router connects its MIDI to a DAW or synth.

## Physical orientation

`GRID_ROTATION=0|90|180|270` sets the physical board's clockwise rotation from
its factory orientation. Both key events and RGB are transformed together.
The repository defaults to **180** for this build. Override when building:

```sh
make firmware QMK_HOME=../qmk_firmware GRID_ROTATION=0
```

The logical simulator stays upright. Rotation does not change patches or MIDI.
`make test-firmware` checks all four angles regardless of the selected build.

## Storage and migration

The atomic JSON wrapper and firmware use the same 2048-byte checksummed bank.
Format 3 stores one global scale and eight patterns, including three-state Gate
values and tie-overlap priority. Each contains six lengths,
all lane values, base/working pitch and octave memory, intensities, weights,
anchors, the combined lock and RNG state. MIDI and generation settings are
stored globally. Explicit bytes avoid compiler padding and endianness issues.
Invalid banks are rejected before application.

**Acid format-2** banks load with unchanged binary Gate values and default tie
priority. Earlier **acid format-1** banks also load automatically, including files supplied
with `--patch grid-a.json`. The selected old pattern's scale becomes global and
all patterns requantize to it. Removed rates become fixed-sixteenth full-length
lanes. The first eight octave steps and resolved values survive; the retired
second octave row is replaced with a CC lane initialized to 64. Either old lock
enables the combined Pitch/Octave Lock. Save writes the new format. Spatial V1
programs are not accepted.

Firmware retains the 2048-byte EEPROM user region and user-data version 2 so
old acid banks can reach the migration decoder. Save/Load stop and release notes
before touching flash. There are no automatic flash writes during performance.
The learner and playheads are session state: load restores the bank stopped,
resets playheads, and retains current learned vocabulary. There was no existing
undo/history subsystem to integrate.
