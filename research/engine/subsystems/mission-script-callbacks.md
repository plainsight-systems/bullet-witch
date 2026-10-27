# Mission callbacks and checkpoint producers

## Source boundary

This is the mission-owned event registry and serial script dispatcher. It does
not implement combat outcomes or claim that checkpoint restoration exists.
Recovered instructions take precedence over inferred decompiler types.

The original registry `0x481360` has 64 records, stride `0x6c`. Function names
occupy 64 bytes before the triggered flag. `_SetTrapCallback` (`0x47c820`)
registers cause three with player-only scope; `_SetTimerCallback` (`0x47c710`)
registers cause zero. Timer registration uses `MOVSS` to store float interval
and remaining time (`0x4813ee`); the decompiler's apparent integer conversions
are incorrect for this branch.

Condition scan `0x481a10` stable-sorts descending cause with `0x4811e0` and
`0x482410`. Traps therefore precede timers, with stable order within each cause.
The trap query checks both native placement registries against the physical
player position. A triggered record remains ready until consumed. Mode zero
is one-shot. Mode one repeats while its condition remains true. Mode two uses
an inside latch for trap entry and clears it only after a later outside scan.
The currently implemented bindings cover player traps and timers, not every
native enemy-count/death/health condition.

Timer update `0x481ea0` subtracts the world context interval only in the normal
world update branch of `0x43eea0`; a flagged timer no longer decrements. Consume
`0x481ef0` removes mode-zero records before invoking Lua and resets a mode-one
timer from its original interval. Overshoot is discarded. A mode-two timer is
not reset by this native branch; it remains expired and can trigger again on
a later scan.

`0x43eea0` takes one name through `0x481ef0`, prepares that global coroutine,
and pumps it until completion before taking another. Conditions can become
ready during the current callback's wait. The source does **not** run 64 Lua
callbacks concurrently. The previously suggested address `0x481e00` is inside
the condition scan, not a separate dispatch function.

## Reconstructed ownership

`MissionCallbacks` owns fixed records and exposes registration, world-time
update, condition scan and consumption. Its trap query has only const query
capability; it cannot alter the world or clock. `MissionScriptRuntime` supplies the
physical position and the immutable simulation interval published by
`MissionGameplay`. User pause suppresses mission script pumping; scripted
world pause stops timer time but does not invent another clock for Lua waits.

`MissionDriver` owns one active callback handle in the retained mission VM.
Its reserved child capacity is one. A callback retains its pending native
continuation while other conditions await dispatch. On completion the handle
is retired before the next ready name starts. Native errors remain failures;
no unsupported gameplay result is reported as completed. Core I.1 and
performance MEM.9 were queried/read in both header and implementation phases:
capabilities are explicit, record storage is inline, and sorting uses bounded
insertion sort without temporary heap allocation. Lua entry retains the
existing bounded-VM allocation contract; this is not an allocation-free claim
for arbitrary Lua execution.

## Checkpoint and audio

`_SetCheckPoint` (`0x47bae0`) updates the checkpoint identifier then invokes
mission owner `0x43c520`. That producer captures mission state, current player
health/magic, progression and weapon/spell levels. `0x493a20` writes base save
data. `0x4932c0` appends mission data, background-object state (`0x446e70`),
enemy state (`0x447bb0`) and the enemy spawn-trigger table (`0x4823c0`). The
last operation copies `0x100` bytes at owner offset `0x1e0c`; these are enemy
spawn fired flags, **not** the 64 Lua callback records or suspended Lua stacks.

The reconstructed checkpoint stores supported player/population values and a
const borrow of the immutable background placement baseline. Its lifetime is
bounded by mission loading. Mutable background physics, binary serialization
and full restore are separate boundaries. Capturing another scripted
checkpoint replaces the current snapshot; it no longer rejects simply because
the opening checkpoint already exists.

`_StopBgm2` (`0x47d0b0`) requests `0x44c190`; music update `0x44bdf0` captures
the current gain and linearly reduces it to zero over the supplied duration,
then stops the source. It consumes the control-service interval. CM01's first
checkpoint uses three seconds, then registers a five-second timer for its next
track. The original one-second PlayBgm fade-in remains unchanged.

## Verification and remaining boundary

The in-process callback suite exercises trap-before-timer order, stable equal
cause order, one-shot removal, repeated/entry modes, paused-world timer input,
triggered timer freezing, reset without accumulated overshoot, capacity failure,
slot reuse and invalid-input rejection. That suite passes; scoped fatal clang-tidy
passes the registry, driver, bindings and new test. The existing purchased-content
integration route now injects trap containment observations to exercise unchanged
CP610/620 scripts, their serial waits, system text, timer/music transition and the
explicit goal boundary. This is not geometry or visual acceptance. Its execution,
concrete goal presentation and repository-wide checks are reported separately by
the mission integration owner. The goal
callback's enemy removal, mission-clear/result flow and persistence are not
implemented by this callback mechanism. On reaching the goal callback's first unsupported
native producer, the driver enters `awaiting_gameplay` with that producer's
name. Its native continuation remains suspended; no actor removal, mission
clear or save is fabricated. Script dispatch stops, while world presentation,
movement and pause remain available. This is the explicit initialization-only
scope boundary, not successful completion of the goal callback.

## System-message verification

The original system-text receiver is separate from the acknowledged mission
instruction dialog. Script producer `0x478560` routes through `0x4ac140` to
`0x464620`/`0x4644a0`; `0x464670` advances the text states and `0x464960`
advances its linear opacity controller (`0x572660`). `0x4c1dc0` looks up timing
properties on the selected language record, not its parent page. Cancellation
`0x464660` enters end-wait without manufacturing a fade. With zero end-wait it
remains visible through the first update and retires on the second. Draw
receiver `0x4642f0` displays initial/playing/end-wait states, excluding the
start-time and begin-wait states.

A read-only census using the production archive/text parsers found one English
page each for `sys.0050`–`sys.0055`, each with `playtime=3`; only `0050` has
nonempty English text. No captured asset bytes are checked in. An independent
x86 emulator witness executed the original state and opacity functions for
230 frames each with ordinary lifetime, cancellation at frame60, and immediate
cancellation. Property/text retrieval was supplied as a bounded fixture with
the same three-second timing. All 690 active/opacity observations matched the
production owner; maximum printed opacity difference was below `5e-11`.
Generated witness code, observations and asset census remain ignored under
`research/evidence/reconstruction/mission-startup/system-message/`.

The in-process regression suite additionally verifies conflicting page and
non-English timing properties, zero-time admission, authored start/begin/end
waits, indefinite pages, index fallback and invalid interval rejection. This
checks source receiver timing; it does not claim visual font/layout acceptance.

The display admission boundary is also explicit in original HUD draw
`0x4993b0`: cockpit/goal drawing is inside the visibility-bit and pause gate;
`0x4ac0d0` system-text drawing follows outside that inner gate. Script
`_DispCockpit` (`0x4783c0` → `0x499790`) toggles only the cockpit visibility bit.
System text must therefore remain an independent overlay when cockpit art is
hidden or pause UI is shown. Sharing prepared font resources and one final
sprite submission does not give cockpit visibility authority over system text.

The reconstructed presentation now preserves that independent admission: each
frame submits gameplay/pause, then any modal, then system text. The surface
appends the latter to the retained sprite list. Native replacement and append
share complete geometry, texture-identity and combined-capacity validation;
failed append preserves both base and previous overlay. The existing native
compositor test verifies ordered pixels, invalid/capacity rollback and overlay
texture retirement while retaining the base. Runtime and native memory tests
pass. No scene graph, additional clock or parallel UI state machine was added.
