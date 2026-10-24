# Player input and locomotion policy boundary

The bounded input gap below has now been extended for the physical evasive action
in [player-evasive-leap.md](player-evasive-leap.md). That trace distinguishes
original A-button attack, trigger-based evasive leap, and generic scripted jump.
The older Runner selector findings remain valid for their stated consumer.

This is a bounded addendum to the actor/animation slice, not a claim that the
complete original hardware-input message decoder is reconstructed. Existing
passes74–82 were reviewed, followed by one export pass84 for Runner's movement
selector and its direct callees. No production changes accompanied this trace.

## Confirmed request-to-animation contract

Runner vtable at `0x8560f8` has slot+0xb4=`0x6678a0` and
slot+0xb8=`0x667000`. Update `0x666c20` supplies an explicit movement parameter,
direction, turn parameter and motion-family bits. `0x6678a0` calls the selector,
replaces only the low nibble of the caller's motion-family value with its result,
stores that motion ID in request+0x14 and publishes the clip with a0.3-second
transition. The instruction listing is important here: Ghidra's inferred
signature omits the final family argument and incorrectly aliases it with the
request pointer in pseudocode.

With the actor's firing-state field+0x126c inactive, selector `0x667000`:

- returns low nibble1 for zero/nonpositive movement;
- returns low nibble0 for positive movement;
- normalizes the supplied direction and retains it separately from movement
  magnitude;
- passes the positive movement parameter to animation speed helper `0x41f800`,
  which writes effective speed as base speed times that parameter;
- resets animation speed multiplier to1 for the idle selection.

The movement parameter is not simply assumed to be a normalized stick axis:
this routine accepts an upstream requested playback rate; an absolute value
above1000 is replaced with1. The native motion table `0x975b28` maps base0 to
`walk_l`, base16 to `dash_l`, and both families' low-nibble1 to `stand_l`.
Therefore the walk/dash family choice is made upstream of this selector, while
its direction/idle selection and playback-rate handling are now confirmed.
`0x664f40` supplies a concrete request boundary: message+0x20 is copied to
Runner+0x68 (motion-family bits) and message+0x24 to Runner+0x6c (movement rate).
It then preserves the existing movement request fields while replacing state.

Firing-state selection uses camera-relative directional sectors and distinct
shooting clips; that branch is outside this locomotion slice. The already
exported control routine `0x689fa0` projects camera forward onto the horizontal
plane and converts movement through the resulting basis. Its surrounding state
constructors identify `StateDodgeStart` (`0x688870`, RTTI `0x97d644`) and
`StateDodge` (`0x688660`, RTTI `0x97d624`). Its state-specific acceleration/cap
handling is therefore not evidence for ordinary jump air control or grounded
input policy. The earlier generic air-update label was too broad; the ordinary
Runner jump path retains captured horizontal velocity through `0x6644b0`.

## Jump boundary and remaining input gap

The existing explicit Runner jump request remains confirmed: `0x664fb0`
request+0x10 selects `StateJumpStart` through `0x65ba80`, captures horizontal
root-motion velocity and supplies the4.2 vertical impulse. Support loss selects
`StateJump` with zero impulse. This bounded trace did not establish the complete
physical-button-to-request chain or the original analog thresholds that choose
motion-family0 versus16. The current0.05 idle and0.5 walk/run thresholds must
remain labelled reconstruction policy. Neither constant is proven by this
selector, and its continuous playback-rate parameter must not be described as
proof that raw stick magnitude is passed through unchanged.

An actionable refinement is to retain separate desired direction, motion family
and playback-rate inputs in the game controller, applying rate through the clip
clock/root extraction rather than multiplying a guessed world speed. Exact
hardware-to-family/rate mapping remains the next bounded input-decoder task;
this finding does not justify silently replacing the currently documented
threshold policy during an acceptance run.

## Established implementation comparison

Read Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Components/CharacterMovementComponent.cpp`:

- `ComputeAnalogInputModifier` derives a clamped analog magnitude separately
  from direction.
- `CalcVelocity` applies analog magnitude to maximum input speed for ordinary
  velocity-driven movement, but exits before that path when animation root
  motion owns movement.

That separation is the appropriate guide while the original input mapping is
unresolved: keep direction, intent magnitude and root-motion ownership explicit.
Unreal does not provide evidence for a Bullet Witch0.5 walk/run threshold, nor
should its velocity algorithm overwrite authored root displacement. This
comparison supports the architectural boundary, not an inferred numeric match.

Evidence: pass84 source exports and the existing earlier movement/input passes
remain ignored under `research/evidence/engine/`. The inspected Unreal source
snapshot remains ignored under `research/evidence/references/camera/`.

## Native keyboard event retention

The platform event pump now preserves nonrepeat Space/Escape down edges
separately from the final held state, using the same event accumulation contract
already used for menu presses. A complete down/up pair drained in one pump must
produce one jump/pause press even though the final held state is false. These
stack flags are combined with the existing controller/held-state edge result,
then discarded; auto-repeat produces no new edge. Focus loss clears both flags
and held state. This is input delivery, independent of the unresolved original
walk/dash mapping. P.1 expresses the two edge states directly; MEM.9 keeps the
hot path's memory footprint fixed. No focus or title acceptance policy changes.

## Hardware walk/dash family resolved

The later hardware trace `pass-90-runner-hardware-jump` resolves the upstream
family gap for the ordinary non-firing player. At `0x6667e0`, magnitude m from
input +0x38 selects dash family 0x10 when m > 0.9 (constant `0x6c1d88`), keeping
playback multiplier m. At m <= 0.9 it selects walk family 0 and divides the
multiplier by 0.9. Selector `0x667000` then supplies the low nibble for moving or
idle, preserving those family bits. See instructions `0x6668f3..0x666923` and
`0x666a6e..0x666a79`; the decompiler incorrectly substitutes an undefined ESI
value for the family held in stack slot +0x18. Firing can first multiply m by
selected weapon parameter +0xb0 and is outside the current no-firing traversal.
The upstream 10/9 radial input scaling and 0.15 deadzone are documented in
[player-evasive-leap.md](player-evasive-leap.md). This supersedes the provisional
0.05 idle / 0.5 run policy.

## Native movement rejection and original scalar ownership

User review exposed `Invalid grounded motion input` after moving. The production
preprocessor and selector reproduced it with legal raw axes
`(-0.9980000257, -0.05700000003)`: normalized components became
`(-0.9983730316, -0.05702130497)`, whose recomputed float length was
`1.000000119`. The selector's `[0,1]` contract correctly rejected that value;
the producer had discarded the original independently capped scalar.

The recovered `5fc3f0` stores the initial length in packet `+0x38`. After radial
saturation it multiplies the vector by the reciprocal length and explicitly
writes `1.0` to that scalar. `6667e0` consumes packet `+0x38` directly; it does
not recover magnitude from the rounded vector. The reconstruction now returns
`ProcessedPlayerMovement {direction, magnitude}` and keeps this value through
the controller. The strict action range remains intact. Source float squared
length and reciprocal multiplication are retained, rather than repeatedly
normalizing until an incidental norm fits an added tolerance.

The exact legal-stick witness and a composed producer/selector sweep cover the
new contract. Generated before/after probes remain under
`research/evidence/reconstruction/interactive-animation/`. Core F.21/I.1/E.4
make the related output and admission invariant explicit; MEM.9 keeps processing
a fixed-size allocation-free operation. This correction does not establish
acceptance of the separately reopened weapon-switch or aiming presentation.

Verification after the correction: runtime fixtures and fatal scoped lint pass.
The dense legal-stick producer/selector sweep passes. A native production Stage
sequence holds the failing analog sample, its opposite and both full diagonals
for two seconds each, repeating across all four admitted weapons. Every case
remains supported and completes without the former exception. Native audio and
rendering remain connected during that sequence; the final image was inspected.
This is verification of the movement correction, not acceptance of the still
open aim or normal weapon-switch presentation.
