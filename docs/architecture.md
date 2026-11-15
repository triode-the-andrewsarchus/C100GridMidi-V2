# Architecture

The portable musical engine is shared by the autonomous QMK instrument and a
single-grid Linux simulator. USB MIDI, matrix events, RGB and persistence remain
separate from musical generation. There is no spatial sequencer mode.

## Components

| Component | Responsibility |
| --- | --- |
| `core/grid.*` | Shared interface, bounded MIDI queue, acknowledgement and panic releases |
| `core/acid.*` | Coordinates, controls, policies, independent lane lengths/playheads, quantized actions, voice lifetimes and RGB |
| `core/phrase.c` | Seeded mixture generator, phrase anchors, hard scale quantizer, persistent mutation and base recovery |
| `core/learning.c` | Transport-independent, bounded statistical vocabulary |
| `core/acid_patch.c` | Explicit little-endian, checksummed serialization and validated application |
| QMK adapter | Early physical matrix events, rotation, native LED lookup, USB transport and explicit stopped EEPROM access |
| Simulator | ALSA worker, nonblocking snapshots, SDL/Cairo controls, local acid audio and atomic JSON persistence |

Each `Grid` owns all musical state. Firmware has one static Grid per unit; the
simulator runs exactly one Grid on its worker. There are no shared mutable
generation scratch buffers or global RNGs. Rendering neither consumes random
numbers nor advances music. Nonblocking snapshots keep GUI rendering and file
I/O off the timing path.

The core performs no allocation, USB calls, file operations or rendering while
advancing clocks. Arrays bound phrase generation, motifs, mutations, echoes and
MIDI output. A full transmit queue skips new attacks while preserving room for
voice releases.

## Musical state and recovery

The allowed-note mask is global in `AcidPatch`. Changing it requantizes base and
working pitches in all eight patterns. Root choice applies to all patterns and
guides future generation without silently transposing memory.

Each `AcidPattern` contains six independent lengths, manually edited lanes,
base/working pitches and octaves, anchors and saved/current RNG state. Temporary
Gate/Accent/Slide variation lives in `working[3][16]`; disabling variation
restores the edited base. Learning belongs to the instrument, so pattern changes
retain vocabulary. Vocabulary and playheads are session state; loading a bank
restores its phrase/RNG memory stopped, resets playheads and retains vocabulary.

Scale, vocabulary and phrase memory remain distinct. Generation combines five
candidate strategies with tonal anchors and root/fifth preferences. Learned
weights blend recent and accumulated evidence using separate confidence levels
for pitch classes, signed intervals and motifs. Incoming notes never directly
overwrite phrase memory. See [musical semantics](semantics.md) for the algorithm.

All lanes advance once per six MIDI clocks and wrap at their own endpoints.
Capacity and row tables provide a common layout for input, RGB and simulator
labels. One lock freezes both pitch and octave buffers. The independent CC lane
continues advancing and emits before note attacks.

## Voice and Gate ties

At most one primary voice and one departing slide overlap are tracked. A new
slide pitch attacks before the old pitch releases; repeated pitches remain held.
A third note retires the older overlap first, protecting A→B→A slides.

Gate edit memory stores Off, On or Long. A shared resolver derives red
continuations without overwriting their original values. Per-pattern priority
makes overlapping and wrapping ties reversible. A two-boundary runtime countdown
suppresses continuation attacks and closes an ordinary Long on the clock;
Slide may carry it into the next sounding note. A clock-refreshed deadline
bounds missing-clock holds. Panic handles reset, pause, pattern changes and
disconnects through the existing MIDI queue.

Gate gestures use the shared 450 ms hold threshold. Per-step timestamps and
bitmasks permit simultaneous holds. Tap changes wait for release, so extending
an On gate never temporarily disables it. A processed hold is consumed once.
Panic/reset/pattern switching, scale entry and length arming cancel pending Gate
gestures. Gesture state is transient and is not serialized.

## Hardware adapter and storage

Early QMK event hooks consume all matrix coordinates before board-specific
lighting/factory-reset handlers. A single physical-to-logical transform serves
both key input and RGB color lookup. `GRID_ROTATION` supports 0/90/180/270° and
defaults to 180°. The core, banks and upright simulator need no rotation.

The USB descriptor and high/full-speed transport adaptations retain their
original attribution. Bounded receive and whole-packet nonblocking transmit
remain separate from the musical engine. QMK's process stack is 4 KiB for
bounded generation scratch and validated decoding.

Bank format 3 stores the global scale and all eight patterns, including Gate
values and tie priority, within 2048 bytes. It migrates earlier acid formats;
spatial patch formats are not supported. The EEPROM user-data version remains
2 so old acid banks reach the decoder. Save/Load stop and release notes before
flash access; there are no automatic flash writes during performance. The
simulator wraps the same bank in JSON using temporary-file/fsync/rename storage.

## Local acid preview

`simulator/synth.c` uses a lock-free MIDI queue and overflow panic. One sustained
saw voice feeds a driven four-pole resonant low-pass filter with separate
amplitude/filter envelopes and overlap-triggered portamento. Trapezoidal filter
stages solve feedback; oscillator/filter processing runs at twice the 48 kHz
output rate. Velocity accents increase level and filter-envelope depth. The
compiled sequence CC selects smoothed base cutoffs of 350/800/1800 Hz.

Delayed Note Offs for a previous slide pitch cannot release the destination.
The callback allocates nothing, waits on no locks and consumes events while
muted. This synth is simulator-only; it does not change external MIDI or banks.

## Influences and attribution

[Marbles](https://mutable-instruments.net/modules/marbles/) informed retained
random loops and learned prominence; [meloDICER](https://www.vermona.com/en/products/modules/product/melodicer/)
informed weighted selection; [T-1](https://torsoelectronics.com/t-1/) informed
immediate generative editing and recoverable variation. The generator is
original C code, not a port of those products. Reused code is listed in
[third-party notices](../THIRD_PARTY_NOTICES.md).
