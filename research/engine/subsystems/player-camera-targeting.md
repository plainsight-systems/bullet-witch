# Player targeting camera

Targeting selection, camera context and retained raised animation are separate
source states. `62ed80` cycles Shooter+0x28's low nibble on right-stick click,
modulo `617500` (two ordinary modes). Mode1 or an admitted held trigger enables
targeting work. Mode0 without a trigger exits that work immediately. The
actor's separate +0x126c raised timer does not govern camera exit.

`62ebe0` restores camera context2 when mode0's short +0x38 delay has expired.
The delay starts at zero (`62e300`), can be set to .01 by explicit `62ea40`, and
is reduced by dt with a zero floor. `61a840` returns the authored
`witch/general/camera` payload, not an active-camera object. Its byte+0x14
controls automatic refresh of this short delay in `62ed80`; the installed
payload has zero there. Thus an ordinary second click restores camera context
on the next ordinary Shooter update, not after the six-second raised timeout.
With RT held, the click's active branch directly selects context2 instead.

`604600` stores camera-context and zoom nibbles in actor+0x123c; `6047f0`
selects a 28-byte `witch/actor/pw0/behind_camera` preset, adding12 when crouched.
The ordinary mode mapping is Shooter0→camera2 and Shooter1→camera1. Special
weapon zoom modes use camera0 and extra FOV entries, outside this two-mode API.

| Context | Standing/crouched preset | Height | Offset | FOV | Look rate X/Y |
|---|---|---|---|---|---|
| Ordinary | 2 / 14 | .7 / .25 | (.5,0,-3) | 44° | 100°/s / 100°/s |
| Targeting | 1 / 13 | .7 / .25 | (.5,0,-3) | 20° | 45°/s / 45°/s |

All four presets have zero pitch compensation. `630b40` selects rates at
+0x1a8 for ordinary and +0x1ac/+0x1b0 for targeting. This is separate from the
existing authored 70° pitch limit.

## Transition and ownership

`61a850` snaps the target camera's desired/current settings. If it is the
manager's active camera and transitions are requested, it creates `6a2910`
with duration .25 seconds and shaping factor4. `6a2b60` advances normalized
time t and uses weight `4t / ((1-t) + 4t)`. `409350` slerps orientation and
linearly blends position from the captured outgoing camera toward the live
incoming camera. Projection blends frustum extents, not angles. Collision
checks the displayed eye's swept path and restarts its remaining transition
from the corrected pose. Near/far use the incoming camera.

The reconstruction keeps this fixed-size transition in `PlayerCamera`, with
`set_context(context,crouched)` independent of time/input. Repeated selection is
idempotent; reversal snapshots the currently displayed view. The caller owns
mode admission and pause, and withholds `advance` while paused. Original
ordinary look/collision behavior remains in the live target camera. Special
zoom and replacement of the active camera by cinematics remain separate.

Pinned Unreal `PlayerCameraManager.cpp::DoUpdateCamera` (revision
`f45729c0b6f097038e72b52d366a62baeca3275b`, local reference snapshot under
`research/evidence/references/camera`) supplies the established ownership
comparison: retained outgoing/pending views, update the pending target, blend,
then publish. The actual curve, quarter-second duration, quaternion path and
frustum-space interpolation here come from Bullet Witch, not Unreal defaults.
Both C++ corpora were consulted for design and implementation: I.1 makes the
context/clock boundary explicit; MEM.9 keeps transitions allocation-free.

Evidence: exports75/117 Shooter,32 camera selector,36/37 camera publication,
100 matrix blend; authored values in `traversal/pw0-settings.json` and
`stage/general-camera-customizer.json`. All generated evidence remains ignored.

The original orientation helper calls the external `D3DXQuaternionSlerp`.
The shared portable primitive uses the near-parallel threshold from Microsoft's
[DirectXMath `XMQuaternionSlerpV`](https://github.com/microsoft/DirectXMath/blob/ecc6560c14fbba13149c72ddcafacb14cb84e599/Inc/DirectXMathMisc.inl):
1−0.00001 in float, with shortest-path sign. This is an explicit library
replacement, not an instruction-level claim about the unavailable D3DX DLL.
No new local vector/quaternion algorithms or target-specific assumptions were
introduced; interpolation and transforms use the engine math library.

## Verification

The parser resource suite passes, and runtime cases check the frustum-space
midpoint, unchanged clock on selection, repeated-selection idempotence,
authored targeting look rates, paused/zero-time continuity, ordinary exit,
crouched height and invalid-context rollback. Fatal scoped clang-tidy passes
both camera implementations and all three affected camera/parser test units.

An ignored standalone fixture reads the actual extracted customization bytes
through the production parser and camera owner. At the authored checkpoint it
reports ordinary 44°, midpoint 25.0188°, targeting 20°, return 44°, with unchanged
heading and eye position. Crouched targeting moves eye Y from 1.35 to 0.9, using
the actual .7/.25 preset heights over the .65 actor anchor. It confirms the
installed automatic-camera flag is false and releases all tracked memory.
The fixture uses an empty collision world to isolate preset selection; it is
not a native wall/transition occlusion proof. Evidence is
`interactive-animation/camera-context-actual.log`,
`camera-context-actual-command.json`, `camera-context-actual-build.log` and
`camera_context_actual.cpp`.
Native GameController/Stage integration is verified separately by the owner of
the canonical targeting state; these checks do not equate targeting exit with
immediate lowering of the weapon animation.
