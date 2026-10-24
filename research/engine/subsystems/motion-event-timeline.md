# Motion event timeline

Implementation in progress, October 23, 2026. The generic animation owner extracts
source event transitions; game receivers own sound, facial, visibility and other
action effects. Decoding a channel is not implementation of its gameplay effect.

## Established source

Bullet Witch `0x6ad4c0` retains the previous float frame, stores the new float
frame, truncates both toward zero and visits ten channels in fixed order.
`0x6acea0` admits at most 128 records per channel, marks crossings, then invokes
entry/update/exit in authored record order. `0x6ad170` only constructs temporary
readers and returns; full instruction inspection shows no dispatch side effect.
The earlier coverage shorthand describing it as connecting adapters is corrected
here. The decompiler also misidentifies the end-frame virtual call as a receiver
method; instructions `0x6ad081–0x6ad088` prove it uses the channel reader.

An ordinary crossing is previous < start <= current. An entered interval updates
immediately and exits when end == -1 or end <= current. Existing intervals update
even when the truncated frame has not changed. Pause therefore belongs to the
caller, which must not tick the timeline just to republish a pose.

When current < previous, the source treats this as wrap, not reverse playback.
It first exits active records, then marks starts in the head [0,current] or tail
(previous,infinity); records dispatch in source order, and every selected wrapped
record enters, updates and exits in that call. This is not a chronological
head/tail sort and must not be silently changed to a generic reverse-play policy.

Pinned Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`AnimSequenceBase.cpp::GetAnimNotifies/GetAnimNotifiesFromDeltaPositions`, was
inspected in the retained reference snapshot. It provides the useful separation
of interval extraction and effect receivers and explicit loop traversal. Bullet
Witch's inequalities, source order and wrap lifecycle remain authoritative;
Unreal's reverse-play behavior, loop cap and temporary event allocation are not
adopted.

The proposed prepared decoder validates numeric compatibility tags and bounds,
retains immutable borrowed payloads and exposes explicit start/end frames.
The runtime uses fixed channel state, synchronous borrowed receivers and explicit
reset/interruption. Unknown tag payload semantics remain opaque, not guessed from
mnemonics. Exact reset/readers and typed consumers are being traced before code.

## Prepared layout and lifecycle

`MotionEvents` now validates the 44-byte parameter header and ten file-relative
channel pointers, each present section's 12-byte header, matching numeric tag,
count and full record range. It keeps source order in one domain-owned timing
vector while payload views borrow the immutable publication. It rejects more
than 128 records rather than silently ignoring an unplayable source channel.
Signed timing fields are retained, including negative/sentinel values; the
runtime crossing predicates determine whether an event is selected.

| Channel tag | Record bytes | Start offset | End offset |
|---|---:|---:|---:|
| SE_0, VO_0, EFF0 | 52 | 40 | 44 |
| FAC0 | 48 | 36 | 40 |
| CAN0 | 36 | 32 | Constant -1 |
| INT0, CAR0 | 40 | 32 | 36 |
| KAK0, HIT0 | 52 | 44 | 48 |
| VIS0 | 40 | 32 | Constant -1 |

These offsets follow the reader vtables at `0x85932c` through `0x859470` and
helpers exported in pass107. Numeric tag spelling remains a compatibility label,
not a claim that every channel's effect is understood.

`EventTimeline` borrows prepared channels and owns 128 Boolean active states per
channel. The constructor validates capacities and initial clock. Advance rejects
nonfinite/out-of-int32 clocks before changing state or invoking receivers. It
retains the source's float clock, then truncates for ordinary/wrap dispatch.
Interruption uses the original float pair, as `0x6ace00` does, and invokes a
separate `interrupt` phase rather than ordinary `exit`.

`0x6ad250` interrupts active records in channel order. `0x6ad3f0` clears bindings
and initializes previous=-1/current=0; game motion selection then installs its
selected clock. `0x5f7d00` separately resets receiver state, including latches from
instant events that no longer have active intervals. The engine's explicit
`reset(receiver, initial_frame)` composes active interruption, receiver reset and
clock selection. Destruction is passive; effect/resource owners must perform
this operation before teardown. Shared immutable catalogs are never reset.

Initial frame zero is not an ordinary crossing from zero. A previous negative
integer frame suppresses that entire dispatch, matching the native guard. A
repeating clip's observed backward clock triggers the source wrap behavior;
multiple unobserved loops are not reconstructed from one pair. Playback owners
must split/advance clocks deliberately if their scheduling can skip whole loops.
The engine does not reinterpret a rewind as backward animation playback.

## Proven receiver boundaries

The motion owner binds channels 0, 2, 9, 7, 4, 8, 3 and 1 in `0x5f7c70`.
`INT0` and `CAR0` do not get ordinary receivers there; the former is also read
by motion-transition setup. Keep them addressable rather than inventing callbacks.

- Cancellation: `0x5f85c0` sets the latch on entry; update/exit are no-ops.
  `0x5f7f40` clears it on reset. This supersedes the old earliest-marker-only
  decoder when a game action consumes the full timeline; it does not require
  changing every existing action in the same edit.
- Visibility request: `0x5f8470` reads signed record+36 at the authored frame.
  Positive values activate a registered matching listener or create an owned
  model-group-bit operation through `0x5f7800`; nonpositive values release the
  matching negative identifier. `0x5f7f00` resets listeners and releases owned
  operations. This is retained ownership, not a permanent global Boolean toggle.
- Facial request: `0x5f8720` reads a packed integer at +32, derives an expression
  index from the ten-million digit group, a weight from the next three digits,
  and timing from the lower four digits. It selects shape-controller bank 2 for
  expression indices 0/1, otherwise bank 1; `0x5f7f90` resets both. The weight is the integer three-digit field divided by 100, confirmed from
  the executable constant. Existing shape controllers receive rate 1 and a
  transition duration; newly selected shapes receive rate 1/duration. Complete
  shape binding belongs to the facial owner; this receiver does not claim vertex
  deformation.
- Voice request: `0x5f90c0` forwards record+32 to the actor's audio owner under
  its source previous-frame guard. Sound/effect/force/hit channels have additional
  spatial/actor-specific consumers; exposing their raw prepared payload is not
  implementation of those effects.

The receiver is a synchronous game-owned capability. It must not throw, reenter
or mutate the bound timeline. Unknown/unimplemented channels must stay explicit
at that owner, rather than becoming fabricated effects based on their mnemonic.

Source evidence: pass05d dispatcher/model update; pass71 transition setup;
pass107 channel readers/reset; pass109 receiver entry/reset. Hash-bound reader
and receiver vtable snapshots are retained under ignored
`research/evidence/reconstruction/animation-events/`. The implementation uses
Core I.1 and SL.con.3 and Performance MEM.9 and CDSA.32, retrieved in the separate
design and implementation phases.

## Prepared game receivers

`PreparedMotionPresentation` owns typed commands alongside the catalog's
`MotionEvents`. Its ordinary channel view removes the two unbound channels while
retaining every decoded publication in the catalog. It borrows the timing records
and original bytes; catalog/resource lifetime exceeds each actor's receiver and
timeline. The immutable prepared allocation can move with a catalog entry before
publication, but live timelines and receivers have stable, noncopyable identities.

`MotionPresentationReceiver` implements the cancellation latch, raw previous-frame
entry guards, and the first zero-duration facial-entry exception. Its sink receives
visibility listener targets, facial index/bank/weight/duration and voice IDs.
Visibility is listener-first: actual authored records use +1, -1, +1000 and -1000
(21 of each); 1000 is never treated as an unchecked body mesh group. Sink owners
must retain and reset the effects they create. An appearance transaction retains
the action runtime instead of replaying the event history. Reset interrupts active
intervals before clearing the game receiver's latches and scoped effects.

Unimplemented sound, effect, force and hit consumers are dispatched explicitly as
unsupported phases with source payload/context. This is an integration seam,
not sound playback, combat damage or particle implementation. FAC packed duration
zero and absent expression/shape resources require a deliberate facial-owner
policy before native execution; decoding alone does not prove them playable.

## Verification

The actual PW0 catalog decodes 164 parameter publications and 704 event records:
411 sound, 145 cancellation, 84 visibility, 36 voice, 22 facial, 5 KAK and 1 INT.
The same bounded census found no event records in the four weapon support sets;
this does not imply weapon actions have no effects or independent controller.
Original decoded buffers and numerical census stay in ignored
`research/evidence/reconstruction/animation-events/`; teardown reports zero domain
bytes live.

In-process engine tests exercise crossing order, wrap, active intervals,
interruption/reset, malformed records and allocation-free ticks. Game receiver
fixtures exercise the original entry guards, first-face exception, listener ID
preservation, packed facial fields, unbound channels, explicit unsupported
phases and reset. Resource tests and focused fatal lint passed; runtime receiver
checks pass. Final combined application/corpus verification is owned by the actor
integration and remains separate from native action-family acceptance.

### Active motion owner

`0x5f9b10` updates parameter selection through `0x5f94b0`, obtains the current
ring controller (the owner's +0x1c index), reads its frame through
`0x46d900`/`0x41d770`, and calls the event timeline once. Retained outgoing pose
clips do not each dispatch their own event stream. Explicit `0x5f9290` selection
resets receiver latches, binds the selected parameter through `0x6ad480`, and
sets both event clocks to the requested start. Nonpositive starts additionally
interrupt active records through `0x6ad250`; positive seeks omit that interrupt.
Automatic parameter changes in `0x5f94b0` preserve the existing clock pair.
This distinction belongs to the game motion owner, not the generic pose ring.

The model update has **two distinct** event owners for Alicia: `0x5e37f0`
dispatches base ring +0xa10 through owner +0xaf8, then upper ring +0x5360 through
owner +0x53d0. Shooter callbacks `0x62e400`, `0x62e570`, `0x62e6d0`, `0x62fa50`
and `0x6313b0` select the latter explicitly. “One current controller” applies
per owner, not once per whole actor. Additive layers have no demonstrated event
owner; outgoing retained pose clips still do not dispatch duplicate streams.
