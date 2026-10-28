# Facial shape animation and position publication

This boundary restores the original CPU shape pass before GPU skeletal skinning.
It does not change the original CPU/GPU partition or invent missing face geometry
for costumes that have no shape buffers.

## Source and reference

Original `0x419320` attaches shape-vertex and shape-offset resources in their
published order. `0x42bc00` constructs each offset descriptor through `0x420850`:
TS2V vertex count is at +12, channel count at +16, 16-byte name-reference records
are based at the file-relative pointer +72, and vertex-major half4 deltas start
at file-relative pointer +76. Each named channel receives an instance binding;
`0x42cda0` begins from the corresponding float3 rest-position stream and adds
weight times each channel's XYZ delta in source order. The fourth half is not a
normal/tangent delta. Its finite half conversion treats exponent31 as finite,
matching the existing skeletal channel decoder.

`0x42aec0` clears the model weights, advances each active shape controller, maps
its named channels to model indices (0xff means absent), and sums scalar weights.
`0x41e510` samples the named scalar curve at the controller frame, clamps by the
channel endpoint where applicable, uses source linear interpolation, then applies
controller weight and fade. `0x41d680` confirms the scalar-channel pointer layout.
The six PW0 shape publications have 16 named channels each. Five are one-frame
expressions and one spans 120 frames; their authored frame rate is about29.97003.
They are not one shape file per costume. PW0/1/2/4 contain shape streams;
PW3/5 contain none. Absence remains an explicit capability outcome.

Established implementation inspected: Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/SkeletalRenderCPUSkin.cpp`,
`UpdateMorphedVertex` and `ApplyMorphBlend`. It copies a rest vertex, accumulates
weighted morph deltas, then skins the result. This confirms the retained prepared
asset/instance result boundary. Its normal-delta and tangent rebuilding policy is
not adopted: the recovered Bullet Witch consumer modifies only positions.
Source snapshots and actual schema census remain ignored under
`research/evidence/reconstruction/facial-animation/` and engine pass116.

## Implemented mechanism and remaining integration

A bounded format view validates offset/name/delta extents. Prepared model shapes
own float3 rest positions and converted deltas in source vertex/channel order,
with names bound once. Instance shape output retains one float3 array per shape
stream. Prepared scalar clips bind names once and sample into a caller-owned
weight vector. Game-owned facial banks supply clocks, fade and weights; the
animation engine does not choose expressions or restart clocks on costume change.

The common render boundary accepts an explicit batch of model/buffer position
spans, validates the entire batch before mutation, then updates retained native
storage. Both color and shadow encoders read that same published snapshot before
skeletal skinning. No frame allocation, per-frame archive parsing, new normals,
or separate shadow deformation pass is introduced. Native ownership requires the
previous command buffer to finish before a position write, matching the existing
pose-publication owner. Failure preserves the previous published result.

Verification must cover independent source-weight/delta arithmetic, actual model
bounds and missing costumes, finite input rejection, no allocations during
sampling/deformation, and a native face close-up using the same color/shadow
snapshot. Passing metadata checks alone is not visible facial acceptance.

### Verification checkpoint

Independent in-memory fixtures pass scalar interpolation, half conversion,
rest-plus-delta arithmetic, malformed input rejection, previous-output retention
and no allocation during sampling/deformation. Native Metal fixtures pass retained
position deformation and restoration through the skinned color path, unchanged
allocation accounting, and whole-batch rejection for a late invalid destination.
Actual-content preparation sampled all six expressions against all six costumes:
PW0/1 have four streams, PW2/4 three, PW3/5 none; the supported models bind 16
channels. Nonzero expressions change authored vertices and all owners retire to
zero live domain bytes. Reproducible commands/results are under the ignored
`facial-animation/` evidence folder. These mechanism checks do not yet establish
visible facial event acceptance in a mission: game event integration remains
pending; the later native close-up witness below verifies deformation.

### Game bank policy

The player factory `0x45b490` requests seven names `@pw0_1` through `@pw0_7`.
`0x5e2620` strips `@` and writes **nonrepeat** into the resolved shape header;
the missing third resource remains an empty slot. The six-file list must not be
used as a dense expression-ID table. Raw archive repeat flags are not the final
player policy. Constructor `0x5dfe60` prepares three pools of four controllers;
`0x5f8720` uses bank2 for expression IDs0/1 and bank1 for the other FAC IDs.

On replacement, the current controller switches to rate1, receives the packed
frame-duration fade, and enters retirement. A positive incoming weight passes the
original presentation admission predicate, then obtains the first inactive slot;
FAC does not force replacement of an exhausted pool. New playback uses reciprocal
packed duration as its rate. Nonrepeating shape playback clamps at its final frame
and holds the expression until explicit retirement. Fade progress advances in
shape-frame units. `0x425470` inserts a new controller at the model's list head,
so weight accumulation visits newest first. `0x5f7f90` resets motion-owned banks1/2;
separate bank0 ownership is preserved. Automatic blinking/voice-driven bank0
selection is not inferred from the FAC path.

`FacialAnimation` prepares the seven-slot binding and reusable weights during
appearance setup. Callback admission reports missing geometry/resources,
suppression, exhaustion and invalid commands explicitly. Its bank-state transfer
preserves clocks across the intentionally improved in-mission costume change;
costumes without shape streams retain logical clocks while producing no geometry
updates. Source culling admission is an explicit caller decision. The original EA0 corpus establishes positive FAC records with zero duration
(e.g. its firing locomotion clips). Those are valid nonrepeating requests: the
source reciprocal becomes positive infinity and its next positive model step
clamps to the final frame. The shared owner represents that result explicitly
without retaining infinity; a zero-delta publication keeps the existing frame.
A zero-duration repeating request remains rejected because the original repeated
subtraction cannot converge. No admitted player or ordinary soldier producer
requests that combination.

### Native close-up witness

The bounded offscreen probe prepares the actual mission owners and native stage
renderer, freezes the skeletal pose, and sends only explicit facial commands and
position publications. PW0 and PW4 captures visibly show eyelid closure and mouth
changes; all six expression images differ from their corresponding neutral PNG.
PW5 reports absent shape capability and all six expression PNGs are byte-identical
to neutral. Front and rear views were captured; the front neutral, closed-eye and
mouth examples were inspected directly. The view uses a diagnostic close-up and
the existing color/shadow pass with authored stage lighting; it is not original
camera parity or a screenshot comparison with Windows. Reproducible command,
source, logs and image hashes are in `facial-animation/native-*` evidence.

The original model order is also confirmed: `0x56b850` advances/sums shapes
through `0x42aec0`; virtual slots +0x28/+0x2c resolve to controller cleanup
`0x5e5280` and skeletal composition `0x5e5220`; `0x5e37f0` then dispatches base
and upper motion events. An event-created expression therefore affects the next
model update. The integrated actor witness now starts the actual stand motion and advances its
production event receiver without injecting facial commands. Native Metal
close-ups show open eyes at frame 9, closed eyes at frames 12 and 17, and reopened
eyes at frame 40; all facial unresolved counters remain zero. This checks the
original event-to-bank-to-shape publication path and next-model-update ordering.
Source, commands, logs and captures are under ignored
`reconstruction/actor/native-event-*`. It does not establish original Windows
screenshot parity or implement unrelated sound/event effects.


### Shared ordinary NPC consumer

Ordinary mission actor preparation `4806e0` builds eight **nonrepeating** names
`@<owner>_1` through `@<owner>_8`; the `ea` family uses `ea0` publications.
Missing resources retain their authored index. This is distinct from realtime
event actor preparation `48af90`, whose unprefixed names repeat; that producer
must not be used as evidence for ordinary enemy policy. The already recovered
`5e2620` strips `@` and writes the repeat flag before controller admission.

Prepared expression keys and model shape deltas are shared immutable resident
state. Each NPC owns its own three facial banks, weights and deformed position
buffers. The existing per-instance shape upload reaches the same Metal skinning
path as Alicia; no second morph algorithm or per-frame archive lookup is added.
Motion reset retires only the source motion banks. This implementation still
requires the full original-content encounter and native acceptance; isolated
facial tests are not evidence of rendered combat fidelity.
