# Original backward-evade visual comparison

The latest physical-controller review accepts the pause menu but still reports
Alicia turning around when evading after running toward the camera. This remains
an open visual comparison. Matching displacement, reference-bone matrices or
source equations alone does not establish the intended visible result.

## Original PC reference candidates

- [XSEED Games: Bullet Witch (PC) Gameplay Livestream](https://www.youtube.com/watch?v=XAkp-wx6zgY),
  published April 23, 2018. This is the publisher's own Windows PC session and
  the preferred primary visual reference.
- [Bullet Witch: PC Gameplay](https://www.youtube.com/watch?v=EPp2CeXFFUU),
  published by Gematsu April 11, 2018. A second PC gameplay reference candidate.

## Footage actually inspected

The publisher livestream was opened in a dedicated Chrome tab and inspected as
paused video frames, including frame stepping around the action. At **11:41–11:42**
Alicia is aiming/firing down the road. At **11:44** the camera is wider and the
weapon remains raised; at **11:45** she is inverted in a backward somersault toward
the camera. This establishes the original backward animation, but the preceding
state is weapon-raised locomotion, not the user's neutral run toward the camera.
Frames at **38:06** and **38:11** show raised-weapon play and running away,
respectively. They are not the requested transition either.

The shorter Gematsu PC clip was sampled at five-second intervals from **0:06**
through **1:26**. Visible samples show ordinary running away, raised-weapon
movement, firing, reload/weapon changes and magic/cinematic cuts. No inspected
sample establishes neutral running toward the camera immediately followed by an
evade. Seeking later samples produced repeated or black frames, so those samples
are excluded from behavioral evidence. This was bounded frame inspection, not a
claim to have watched every intervening frame or to have ruled out an evade.

No remote video was downloaded. The user's running game received no inputs and
was not stopped or restarted. Original dress inspection and its limits are
recorded separately in [secondary-motion verification](secondary-motion-native-verification.md).

The publisher's [May 1 PC changes article](https://www.xseedgames.com/2018/05/01/whats-new-with-bullet-witch-on-pc/)
confirms that the port changed frame-rate support, weapon handling and introduced
a restored dash feature. That is sufficient reason to prefer PC reference footage
for this reconstruction. It does not describe backward-evade facing, and the dash
feature must not be confused with the ordinary directional evasive leap.

## Remaining exact comparison

Locate an uninterrupted PC sequence with Alicia running toward the camera, then
entering the ordinary evasive leap without a camera orbit. Record the frame before
entry, early turn, takeoff and landing; distinguish horizontal model facing from
the torso's somersault rotation. Record whether the gun is drawn/firing because
preceding Runner locomotion has a separate firing policy. A neutral ordinary run
is the current runtime consumer. Do not use a PC feather dash or a camera cut as
an evade witness.

Compare that sequence against the existing native all-four-direction/two-camera
captures in ignored reconstruction/traversal evidence, especially the backward
case's before/start frames. If the original visual behavior differs, localize the
first divergent consumer/state before changing direction, clip or axis policy.
If the original also turns, report that observed fact separately from whether the
user wants a future movement improvement. Current source evidence and the exact
remaining boundary are in [direction reference](player-direction-reference.md).

## Small correction and deferred boundaries

An independent execution witness of the original turn helper demonstrates a
narrow discrepancy: near exactly opposite headings, the source chooses its
fixed positive-Y fallback rotation, whereas scalar shortest-angle remainder can
choose the opposite arc. The shared direction-math correction preserves that
source branch; it does not change the backward clip, target heading, turn rate or
root-motion trajectory. It therefore cannot be advertised as removing the
user-reported turn toward camera heading. See the original-machine-code witness
and implementation status in [direction reference](player-direction-reference.md).

The source dispatcher also installs the evasive state after updating the outgoing
state, while the current bounded controller can advance it in that input step.
The complete pose-clock ordering is not established by that fact alone. Do not
add an arbitrary frame delay: retain this as an action-transition ordering issue
for the fuller animation/state integration. The exact neutral-run visual witness
remains unresolved after this bounded footage search; preserve the source target
rather than changing it to match a preferred appearance.
