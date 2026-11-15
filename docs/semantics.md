# Phrase-memory semantics

Scale, vocabulary and memory are separate. `scale` is a hard 12-bit eligibility
mask shared by all patterns of the instrument. Twelve nonzero user weights are
distinct from the mask, allowing a future
Preferred/Allowed/Disabled UI. The vocabulary combines built-in probabilities
with learned pitch classes, signed intervals and recurring interval motifs.
Memory consists of 16 base pitches and 16 working pitches per pattern.

Factory generation uses bass MIDI 36–59 and C natural minor. Allowed register
bounds must span at least twelve notes and leave an octave of MIDI headroom in
both directions. Scale changes choose the nearest valid in-range pitch for every
pattern's base and working values, with lower-note ties, preserving contour
without octave drift.
Octave is applied afterward from E's own buffered position. The tonal root is
explicit: in the Scale Editor, hold a piano note for 450 ms to set it across all
patterns. That note is enabled if necessary, shown white and labeled with `*` in
the simulator. A tap still toggles eligibility. Root choice changes future
selection, leaving existing pitches intact; NEW generates an immediate phrase
in that tonal center. Disabling the root picks the nearest remaining allowed
pitch class. No key is guessed from a scale mask, since the same mask can serve
several tonal centers.

Initial generation first fills with a tonal reference and chooses 4–7 distributed
anchors including a root on step one. Anchor placement favors sounding Gate
positions and quarter-note positions, without imposing the same layout each time.
Anchor choices favor root (50%) and allowed fifth (30%), with the remaining
choices drawn from the blended pitch pool.
Very short Gate lanes use fewer anchors. It then fills between anchors. Configurable starting strategy weights
are 30% motif-note reuse, 25% nearby movement with signed-interval scoring,
20% weighted pitch pool, 15% intentional leap and 10% motif transformation.
Repeat proposals revisit the current pitch or a position four steps back,
retaining short patterns instead of sampling unrelated notes. Nearby-note and
pitch-pool proposals favor root/fifth, particularly at anchors and strong beats.
A soft register preference keeps base pitches near the lower tonic; octave
excursions remain possible, while E provides deliberate octave movement.
Preceding/following sounding Gate positions and the next anchor affect filling,
including direct anchor jumps. Leap candidates favor important tonal degrees or learned destinations
and do not receive the ordinary stepwise penalty. Pattern seeds and current
RNG state are retained independently.

On every A step boundary, even at a rest, D's effective policy decides whether
the corresponding **A-indexed memory cell** can mutate. Green starts at 25%;
Yellow at 10%. Structural anchors use one third of Green's probability (8% at
the default) and half of Yellow's (5%). Green anchor edits usually choose another
tonal anchor, so phrase returns remain recognizable through extended evolution.
Probability
and magnitude are separate. Yellow biases repetition/local movement and bounds
actual changes after scale quantization to four semitones. Red and Pitch Lock
avoid mutation entirely. Successful replacements become the persistent memory;
loop playback never regenerates unrelated pitches independently.

A/B/C temporary evolution uses 0/5/15/35% flip probabilities per lane visit.
Removing a Gate on a quarter-note anchor is half as likely. Each lane samples
its edited base value on its own scheduled visit; working values repeat until
that lane next resolves the position. None restores base values immediately.

E resolves inherited policy cyclically and samples any random choice when its
own position advances. It stores −1/0/+1 at that E-indexed position. The combined
Pitch Lock
freezes all eight octave positions as well as pitch, and consumes no octave
random draws. The CC lane keeps advancing/emitting while locked. Explicit E edits
update base recovery values; random policies use zero for base recovery. The lock
does not stop any lane playhead. Policies inherit only within the
active lane length, so excluded steps do not influence the first step.

# Learning

Every valid incoming nonzero-velocity Note On is observed even while paused.
All chord tones update the pitch-class and register profiles. A fixed window
of **15 ms**, measured from a group's first Note On, identifies near-simultaneous
chords. Only single-note groups become melodic observations. Chords break the
melodic interval/motif chain; no arbitrary input ordering becomes a melody.
Note Off and velocity-zero Note On do not add observations.

The learner stores signed intervals −24..+24 and up to 24 bounded motifs of
2–4 notes, represented by 1–3 signed relative intervals. Larger melodic
intervals do not enter the histogram, though register and pitch class still do.
When the table is full, replacement considers both normalized accumulated and
recent support, so an old motif cannot survive indefinitely on raw count alone.
Motifs compete by weighted sampling rather than always choosing a single winner.
Three/four-note motifs must match the preceding melodic intervals before supplying
their next interval; two-note motifs supply signed interval vocabulary. Both
aggregate and per-motif confidence limit influence. A single observation supplies
no motif influence. A local repeated contour is used when no learned continuation
fits.

A long-term profile accumulates events. A recent profile and motif support decay
by 0.985 per musical note (a half-life of about 46 incoming notes), rather than
wall-clock polls. This keeps event replay
deterministic and preserves learned information when input stops. Pitch and
interval confidence increase with independent sample counts and decrease when
recent evidence disagrees with accumulated evidence. Influence caps are 0.80,
0.75 and 0.65 for pitch, interval and motif respectively. Add-one smoothing
preserves unseen choices. Learned distributions blend **25% accumulated and 75%
recent evidence**, with the remainder always supplied by the musical prior.
Register statistics use the same recent bias and influence pitch-pool choice
weakly, after octave folding
into the configured bass bounds. They cannot move the bass register itself.

Input never rewrites phrase memory. Learned vocabulary affects the next scheduled
mutation or explicit regeneration and never permits a disabled pitch class.

Generated events are not fed into the learner. The ALSA adapter rejects its own
client as a source. The core additionally remembers 32 recent generated note/
channel events and rejects matching input within a configurable 35 ms echo
window. A trusted integration may disable that window or mark observations as
self-generated explicitly. Unlabeled, delayed MIDI routed through third-party
clients cannot be identified with certainty; avoid routing output back to input.

# Timing and articulation

At 24 PPQN all six lanes advance every six clocks, at sixteenth-note speed.
Each wraps at its own inclusive endpoint, with capacities 16/16/16/16/8/8.
Changing an endpoint affects the next step without resetting other lanes.
A 15-step Gate and 7-step Accent return to the same relative positions after
105 steps; other lane lengths can extend the complete combined cycle.
The initial MIDI Clock after Start plays step one. Continue preserves positions.
MIDI Stop silences immediately; physical Play/Pause and Reset wait for the next
six-clock boundary. The internal phase continues while paused. Pattern switching
waits for Gate's next wrap at its selected length; paused switching uses a
96-clock boundary. Switching and Reset release tracked notes before advancing.

The CC lane emits one 0/64/127 message every sixteenth, including rests, before
any new note attack. Its controller is fixed at build time (`SEQUENCE_CC`, default
74) and its MIDI channel follows the note output channel. It has no inheritance
and is unaffected by Pitch Lock. Backpressure skips new CC/attacks while retaining
room for note releases.

Gate taps toggle Off/On on release; a 450 ms hold sets Long once, using the
same duration as the existing Reset/root gestures. A yellow tap turns Off;
a red tap cancels its source tie. Holds can start from either Off or On.
Gate memory stores Off, On or Long. Effective continuations are derived, leaving
their original Off/On/Long values intact. A yellow Long holds through its red
continuation and ends exactly two sixteenth boundaries after the attack, unless
Slide deliberately carries it onward. The continuation never attacks or changes
the held pitch, octave or velocity. D/E and the CC lane still advance normally.
Ordinary note length remains configurable as 1–100% of A's current step interval.
Ties use clock counts, not that articulation percentage. A clock-refreshed
watchdog releases a held tie if clocks disappear; transport, reset, pattern
switching and disconnect also clear its runtime countdown.

The newest edited Long has priority if two stored Longs overlap. A covered Long
is dormant; pressing its red cell restores it and its own continuation. Other
conflicting Longs act as short On positions until space is available. Active
yellow/red pairs are immune to Gate evolution. Wrapping uses A's active length;
shortening/expanding retains hidden base values. A one-position Gate lane can
still play Long: it attacks every other visit, displaying yellow on its single
cell. Starting/resetting on a red continuation produces no attack until the next
sounding start. Edits take effect on subsequent visits; canceling a live tie
allows the restored step to attack at the next boundary.
Accent selects higher velocity and optionally emits a configured CC (127/0).
A slide holds through rests to the following sounding note, sends a new pitch
before releasing the old pitch after configurable overlap, and avoids reattacking
a repeated pitch. An orphaned slide has a safety tail of one complete A cycle
plus the configured overlap.
At most one primary voice plus one departing overlap is tracked. MIDI overlap
alone does not guarantee audible portamento; receiving synth behavior varies.
No pitch-bend or portamento CC slide mode is implemented yet.

All MIDI release queues survive repeated stop, load, reset and disconnect
operations. If the USB device reconnects, pending releases drain before normal
playback is resumed manually. MIDI input/output and grid editing stay bounded
and continue under transmit backpressure.
