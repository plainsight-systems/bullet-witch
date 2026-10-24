# Alicia traversal acceptance

The agreed slice is locomotion and collision: selected-costume Alicia, her
attached weapon, authored movement, physical jumping and landing, static world
contact, a live follow/look camera, and traversal pause/resume. It enters from
One Shot Kill's completed load using the sole explicitly approved initial-
placement shim. Historical checkpoint-coordinate probes below use independent
fixtures; they do not establish checkpoint restoration. Full
mission-start Lua, instruction messages, BGM, enemies, objectives and traps
remain separate mission behavior. Moving rigid bodies, impulses and destruction
remain the dynamic physics-provider boundary.

## Earlier actual-content verification

The native Metal diagnostic uses production MissionLoading, PlayerActor,
PlayerController, StageInspection and WorldRenderer against the installed
One Shot Kill resources. It constructs each authored checkpoint placement
(0, 10 and 20), settles, walks with a partial analog input, idles, jumps,
lands, pauses while movement/look inputs are held, resumes, looks and runs.
Assertions require finite positions, ground support, displacement, an airborne
arc and landing, and unchanged position/velocity/camera during pause. Native
images show Alicia and her weapon following the physical state. All three
sequences passed before the subsequent hardware-action correction. They remain
contact/lifetime evidence, not acceptance of the corrected evade action. Views retire before mission cancellation;
all device charges return to zero after cancellation.

Each placement runs 736 simulation updates. An injected counting wrapper around
the native CPU backing observes no domain allocation or release in any
StageInspection update, including clip changes, contact, camera, view publication
and material curves. This does not cover Cocoa/Metal external heaps. Debug-build
mean update times were approximately 1.93, 1.13 and 0.66 milliseconds for the three
paths on the development Apple Silicon Mac. These include the composed CPU
update, exclude rendering/readback/pacing, and were recorded under concurrent
verification load. They are diagnostic measurements, not optimized throughput
or a frame-rate claim.

Separate actual-stage probes exercise three wall faces, three ramps, three
ceilings, low steps, tangential wall sliding and walking off a real edge. A
higher sampled fence edge remains blocked; it was not relabeled as a supported
stair or used to relax collision. Prepared geometry uses the source's distinct
46-degree contact and 45-degree floor thresholds and ordinary grounded probe
extension. Synthetic contract tests cover corner and rejection cases.

All 28 selected prop resources prepare successfully: 159 source shapes produce
2,038 local triangles and four analytic round primitives before instancing.
Representative placed container, barrel capsule and convex drum witnesses use
the actual authored body/local transforms. Independently recalculated geometry
agrees within 6.4e-6 world units; camera clearance is approximately 0.05 and
character clearance approximately 0.41. Render bounds never supply solids.

## Native application and checks

The native launch check exposed a zero-sized sprite viewport before a world
scene existed. The host now computes the shared canvas viewport unconditionally;
world preparation remains conditional. The corrected fullscreen title shows the
Start prompt and version text. The normal fullscreen route through Concept
Mission, One Shot Kill and Normal difficulty reaches Alicia in the loaded stage.
Native keyboard observation covers movement, camera yaw, jump/air/landing, and
pause/resume with movement/look held while paused. Immediate Space/Escape
down/up pairs also work: the host preserves press edges when both events arrive
in one pump. The view stays unchanged during pause and movement resumes after
unpausing. This run used injected native keyboard events; it does not claim a
physical controller observation.

The required build, host tests, pinned fatal clang-tidy, content policy and
original-root startup corpus pass. The existing startup corpus includes bounded
movement, retained-view publication, pause/resume and balanced ownership. These
results do not establish Xbox execution or complete original-engine parity.

Generated source probes, build commands, logs and captures remain ignored under
`research/evidence/reconstruction/traversal/`: `native-traversal-final.log`,
`offscreen_traversal_probe.mm`, its build script, checkpoint PNGs, static-stage
contact records, `native-window-acceptance.log`, native window captures and final
check logs. Placed-shape witnesses are under the
neighboring `stage/` evidence directory. Original game files remain read-only.

## Review findings and corrections

User review confirmed physical controller input works, then identified slow,
awkward jump recovery and an invisible loading interval. The earlier traversal
checks established movement and contact, but did not verify the original
landing exit marker or loading presentation. They were insufficient to close
those behaviors.

The first fix used a real generic Runner landing marker (frame 13 rather than
full 44-frame completion). That reduced recovery, but subsequent source tracing
proved this was the wrong hardware action. Its timing is retained as historical
research in [jump transition timing](player-jump-timing.md), not the current
player acceptance contract.

The loading request had been retained without a visible consumer. Stage PNG
expansion dominated measured loading time in the Debug codec build. The loading
presentation and decoder correction are recorded separately in
[loading presentation](loading-presentation.md).

## Subsequent user review

The user navigated the ordinary native menu route with the physical controller.
Loading is now visible: the original motif, portrait, Ancient Wall tip and
LB/RB/B icons are captured in
`research/evidence/reconstruction/loading/native-review/frame-035.png`.
Frame 045 shows costume 5 in the stage; frames 055 and 070 capture movement/jump
and grounded user control. The parent agent viewed these captures. See the
loading record for the final composed-fade verification boundary.

The user then reported sluggish start/flight and feet sinking. Source audit
found two concrete errors. Original LT invokes the directional evasive action,
which consumes full XYZ root motion; A is melee, not generic RunnerJump. The
reconstruction had wired A to the generic 22-frame/fixed-impulse jump. Separately,
original actor update overwrites constructor model Y=-0.5 with negative body
radius (-0.4 for living Alicia). Both corrections are implemented.

The corrected traversal uses held LT (Space keyboard alias), all four authored
evasive families, source camera-relative direction and steering, root-driven
start/middle, gravity only after the authored middle ends, and double-rate
landing with cancellation frame 25. It preserves each outgoing animation's
playback rate during blending. Source hardware preprocessing (10/9 radial
scale, 0.15 deadzone, 0.9 walk/dash boundary and separate walk/dash playback
rates) replaces the earlier policy thresholds. Direction-reference transforms
are sampled at current frame in the original; all 17 selected references are
constant. Setup validates every key before caching, rejecting varying future
references. See [original evasive action](player-evasive-leap.md).

Corrected costume5 native captures show the full somersault and restored
standing height. The capture-free full-resolution Metal run measured roughly
0.239 seconds of start, 1.012 seconds until landing and 0.213 seconds of recovery,
with matching wall/game transition time and no 0.1-second cap frames. This is
local production-path evidence, not original-game audiovisual parity. In the
subsequent physical-controller review, the user confirmed jumping is much better
and floor clipping is fixed. The existing original-root corpus now covers
all four directional root bases and the corrected start/recovery timing.

Evaluated world-pose inspection allowed original boot vertices to be compared
against exact terrain faces. Standing baseline is corrected; airborne samples
clear the floor. A brief end-to-stand blend still dips one sampled sole about
0.086 world units below the rendered surface (previously about 0.107 before the
outgoing-rate correction); a start sample dips about 0.042. Source local
transform blending matches the current mechanism, and initial root Y does not
explain the residual for these clips. No collider bias, height compensation or
IK was invented. This numerical transient remains a diagnostic limitation; the
user now accepts the reported floor-clipping correction. It is distinct
from capsule support, which retains roughly 0.01 clearance in sampled paths.
See [collision/mesh alignment](player-collision-boundary.md#source-correction-model-origin-follows-body-radius).

The native GPU probe isolated floor and character self-shadowing. Source caster
admission was corrected first; a subsequent receiver-footprint depth correction,
guided by pinned Unreal source, preserves the eight source taps and nearby thin
occluders. The user now reports that shadows look much better. Pixelation on
Alicia and slight movement in distant mountain shadows remain original-game
comparison points, not authorization to change map resolution or stabilization.
See [stage shadow presentation](stage-shadow-presentation.md).

Current review accepts evasive timing, floor alignment and the shadow correction.
Forward/back evade facing remains rejected after the initial reference-bone fix.
The production-path probe records all four directions at two camera angles,
including rendered transitions and anatomical bone positions. Signed travel and
reference identity did not prove visible facing; the earlier completion claim
was incorrect. See [direction reference](player-direction-reference.md).
Outfit intersections have a separate demonstrated cause: costume 5 uses
secondary bones without motion tracks, and their original post-animation
body-contact pass was absent. The original secondary solver now runs after clean body evaluation and before
weapon/palette publication. All six costumes pass actual-content pose checks and
native motion captures: animated body bones remain unchanged, secondary bones
move, transforms remain finite, and steady updates allocate nothing in the
engine domain. This is not a proof that every garment triangle avoids every
body intersection. Physical-controller review still observes dress/body clipping. See
[secondary motion](costume-secondary-motion.md) and
[native outfit verification](secondary-motion-native-verification.md).

The user-approved pause menu now exposes Resume and Costume. Live replacement
prepares a separate body, preserves the frozen action and physical/camera state,
publishes body draws atomically, then retires the old appearance. The original-root
corpus exercises all six costumes and injected publication failure with balanced
ownership. The actual-content Metal probe also swaps all six costumes during a
frozen walking pose, captures each, and resumes walking with balanced teardown.
This caught and corrected overbroad texture-retirement invalidation. See
[retained draw replacement](model-draw-replacement.md). The subsequent physical-controller review confirms the pause menu works.
Resume and Costume are its intentionally bounded actions. The user still
observes dress/body intersections and backward-run evade turning; neither is
closed by these native ownership checks. Actual publisher footage shows a
raised-weapon backward flip at 11:41–11:45, but the exact neutral-run transition
was not found in the inspected samples. A source-machine-code witness proved a
near-180-degree rotation-side mismatch, which is corrected without changing the
clip, target or turn rate. This does not establish that the reported target-facing
turn is resolved. Original-game comparison remains open.
See [pause/costume](pause-and-costume-switching.md) and
[backward-evade visual references](player-evade-visual-reference.md).

## Fidelity boundaries

Recovered behavior and explicit reconstruction choices are documented in
[player collision](player-collision-boundary.md),
[placed collision](placed-physics-collision.md),
[animation bindings](alicia-animation-bindings.md),
[live camera](player-camera-follow.md) and
[view publication](live-view-publication.md). Analog speed scales authored clip time/root displacement using the recovered
nonfiring input policy. Trigger remapping, weapon-dependent firing movement,
combat and invulnerability are not implemented. Camera angular limits and
geometric solver adaptations remain explicit. Alicia animation coverage and the recovered outfit secondary-motion consumer
are now authorized follow-on work. Full shape deformation and gameplay consumers
remain explicitly tracked rather than implied by clip availability.
