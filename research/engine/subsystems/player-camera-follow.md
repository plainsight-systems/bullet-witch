# Live player camera

The live camera owns view orientation independently from Alicia's locomotion
facing. Scripted placement resets both the initial heading and retained offset;
ordinary movement changes only the camera anchor position. The renderer receives
a completed view, and platform input supplies normalized right/up look axes.
Ordinary and right-stick targeting contexts now use the authored standing and
crouched presets; see [targeting camera](player-camera-targeting.md) for mode
ownership and transitions. Special weapon zoom, vibration and cinematic camera
replacement remain separate.

## Recovered behavior

Bounded Ghidra pass75 exports `0x68b510` (player-control look), `0x630b40`
(Shooter look) and `0x619e90` (camera update), their immediate callees and callers.
The original image identity is verified by the existing engine export tooling.
Both look consumers read the actor's settings at actor+0x121c and edit its
independent anchor transform at actor+0x1218. The ordinary mode2 selects
behind-camera payload+0x1a8: 100 degrees per second for both axes, multiplied
by input and elapsed seconds. Mode1 uses its separate 45-degree-per-second targeting rates. Mode0 special
weapon zoom remains outside this two-context camera. The input routine ignores the update only when both
absolute components are below0.05; this does not rescale the remaining range.

Look reconstructs an orthogonal basis, choosing forward versus up as the
reference vector around a ten-degree threshold. Its up-vector restriction
uses payload+0x1b4, authored70 degrees. The original component clamp precedes
normalization, so it can overshoot the nominal angular limit slightly within
an update. The portable controller expresses orientation as yaw/pitch and
applies a hard authored angular limit. This numerical reconstruction is an
explicit departure, guided by the mature implementation below; it does not
claim instruction-identical matrix-limit behavior.

Camera update restores current height toward authored height by speed*dt.
Each current local offset advances along the normalized desired offset at
that speed; the source caps absolute component magnitude and restores the
desired sign. Three ordered segment queries with clearance0.05 and exclusion
mask0x2000 resolve world-vertical height, local shoulder and local rear offset.
A hit persists the shortened component for the next frame. Shoulder vertical
compensation is forwardY squared times the authored pitch coefficient. The
ordinary preset's two coefficients are zero. Vertical anchoring uses world Y,
not the pitched camera up vector. No positional lag is present in this source
path beyond recovery of shortened offsets.

## Reference and design

Before implementation, inspected Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`:

- `Engine/Source/Runtime/Engine/Private/PlayerCameraManager.cpp`,
  `APlayerCameraManager::ProcessViewRotation`: explicit retained view rotation,
  delta rotation and limits. Adopt this state contract and hard angular limit
  for the bounded numerical reconstruction above.
- `Engine/Source/Runtime/Engine/Private/GameFramework/SpringArmComponent.cpp`,
  `USpringArmComponent::UpdateDesiredArmLocation`: independent target rotation,
  anchor, arm offset, collision result and published camera transform. Preserve
  that separation. Do not adopt its sphere sweep or optional lag because the
  recovered Bullet Witch camera has ordered segment queries and offset recovery.

Pinned source snapshots remain ignored under
`research/evidence/references/camera/`. These references guide structure;
production code is independently written from recovered behavior.

`PlayerCamera` borrows immutable collision storage for its lifetime, owns fixed
settings/state, and advances on the game thread. Normalized inputs, finite
nonnegative elapsed time and finite poses are explicit preconditions. Invalid
input throws before publication. Pause is controlled by the caller withholding
advancement. A teleport resets orientation and current offsets, then resolves
clearance immediately. Per-frame work has fixed math and three existing static
segment queries; there is no successful-path allocation. Both MCP design and
implementation use I.1 (`ri-explicit`) for these contracts and MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) for fixed retained state.

## Verification

The in-process runtime target passes independent look/follow, authored-rate
integration across update counts, hard pitch restriction, invalid-input atomic
publication, teleport reset, retained collision shortening and recovery, and
rigid view validation while turning. Parser, camera owner and camera test each
pass fatal clang-tidy. These are numerical and ownership checks; the integrated
interactive traversal remains the application acceptance check.
