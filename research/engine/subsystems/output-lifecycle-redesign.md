# Output lifecycle redesign

October 24, 2026. Implements the output-owner portion of the lighting redesign;
see L35–L42 in [the acceptance map](lighting-redesign-acceptance.md).
The current authored equations, target sizes and accepted presentation clock
remain the baseline. This work changes lifetime and publication contracts.

## Recovered lifetime

Original `461f00` skips adaptation while tone mapping is disabled. The static
ring, sampling accumulator and previous luminance surface survive. `571180`
clears the 256 history floats, write index, full flag and adaptation value; it
does not change the configured window or external sampling accumulator. Its
event preparation caller is separate from ordinary scene replacement. The
retained instruction/pseudocode exports under the lighting audit's ignored
`output-lifecycle/` directory establish these distinctions. A history-window
change has the same bounded history reset and preserves sampling time.

The engine contract separates three operations:

- `ExposureController::reset_history()` clears samples/adaptation, preserving
  window and sample timing. `WorldPostprocess::reset_exposure_history()` exposes
  that operation to the eventual event owner and keeps the measured image.
- `release_scene_targets()` retires scene-size color/depth storage and temporary
  postprocess images after completion. It retains the single-pixel measurement,
  its validity and the exposure controller across scene replacement.
- `reset()` ends output lifetime: release every target, invalidate measurement,
  clear history and sample timing. Destruction has the same resource endpoint.

Tone toggles only change parameters. A separate measurement-valid flag records
whether reduction was actually encoded, so an allocation alone or a tone-disabled
submission cannot manufacture a predecessor. A command-completion check protects
readback and reuse. There is one exclusive owner; consumers do not borrow mutable
history or sample-clock state. Existing framework completion remains required
before target release or lifetime reset; no extra synchronization is introduced.

## First-use target inference

The original reads its preceding reduction unconditionally before queuing the
new one. The initial surface is allocated without an explicit upload/clear;
actual first bytes and earlier front-end/event history require original runtime
evidence. Reproducing undefined target contents or seeding a guessed brightness
would not establish fidelity.

Inspected pinned Unreal Engine
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp`:
`FSceneViewState::FEyeAdaptationManager::GetOrCreateBuffer` explicitly initializes
new multi-frame exposure storage and `GetLatestReadbackBuffer` returns only ready
readbacks. Its eye-adaptation parameter construction selects `ForceTarget` on
camera cuts/previous-transform resets rather than blending with invalid history.
The retained reference and SHA-256 manifest are under the ignored
`character-light/unreal-reference/` directory.

Adopt explicit history validity and valid-first-target initialization, retaining
the existing current-frame GPU reduction on genuinely first use. Do not adopt
Unreal's numeric initialization (ones), exposure equations, or camera-cut reset
policy. This is a documented target inference, **not** a recovered Bullet Witch
first-frame algorithm. Once a real predecessor exists, original preceding-image
ordering applies. Tone toggles, event history resets and ordinary scene changes
do not re-enter first use. Original shader output bounds still apply to the
actual first reduction; no fake measurement, CPU warm-up or additional readback
is introduced.

## Clear publication and cost

`463b40` copies authored scene clear color into frame state. Native
`begin_scene` receives an explicit finite RGB triple and clears alpha to one;
black is the backward-compatible default. The view owner supplies this value.
Clear happens before scene drawing and feeds the same HDR/output pipeline.

History remains 256 inline floats. Adaptation retains bounded O(window) source
summation and no steady-state allocation. Output intermediates remain retained
across frames; scene release keeps only the 1×1 R32 measurement among targets.
One four-byte preceding-frame readback remains source-required, after the existing
completion fence. This is a cost description, not a throughput measurement.

Both C++ corpora were queried and full applicable rules read for design and
implementation: Core E.4 (`re-design-invariants`) requires valid-state checks
before mutations; performance GPU.9
(`guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md`) preserves
explicit target lifetimes/reuse; GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`) keeps intermediate images resident
and isolates the required readback; MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) supports fixed history
storage. No new allocator, asynchronous graph or lighting equation is inferred.

## Verification

Implemented the separated reset/release contracts, preserved tone-toggle state,
explicit measurement validity and authored RGB clear publication. The shared
controller now uses the same history-only reset for event preparation and
history-window changes. Native completion guards apply before both target reuse
and completed-measurement readback.

The strict host build of `resource-tests` and `memory-tests` passed. CTest
`resources` passed (0.03 seconds); the full native `memory` scenario passed
(0.48 seconds) after updating two historical fixtures to the now source-correct
material admission contracts. No tolerance was weakened: shadow reception uses
the audited source-enabled variant rather than an inspection no-shadow variant;
masked color explicitly selects the masked phase rather than assuming a depth
selector silently enables a color discard. The color test also exercises opaque
coverage and final indirect-alpha rejection independently.

The CPU lifecycle cases cover event reset versus lifetime reset at a partial
sampling interval and preserved configured history length. Native completed
pixel cases cover:

- Tone disable/re-enable preserving a real predecessor and filled history while
  the visible scene changes.
- Event reset preserving the measured image; scene target release retaining it;
  full lifetime reset selecting the actual new first image.
- Nonblack HDR clear reaching display and invalid clear rejection before resizing.
- Tone configuration changes preserving the outstanding GPU completion guard.
- Two instances sharing one model/draw retaining independent receiver packets,
  deliberate broadcast and atomic rejection of an absent instance in a batch.

These cases use authored fixtures and the production Metal owner, without game
assets or a visible window. An earlier isolated output-only diagnostic also
passed; the subsequent full native scenario supersedes that limited run.
Focused fatal clang-tidy passed all four changed translation units (and their
headers). Named receiver and coverage test helpers keep the native scenario below
the function-size limit without suppressing diagnostics. The final
repository-wide build/lint/policy gate remains with redesign
integration; these focused passes do not claim that gate or whole-scene parity.
Source runtime first bytes, physical display transfer and real event production
remain explicit boundaries. No original footage or live PC frame was captured.
