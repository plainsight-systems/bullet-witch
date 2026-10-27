# Mission goal presentation

The primary mission goal belongs to the game HUD. Mission startup retains the
script-selected world position and publishes it through MissionGameplay and
PlayerCockpit. MissionGoalHud owns only the original visual lifecycle. It
receives a const camera/player observation and the already admitted world
interval. It neither advances a clock nor retains mutable world handles.

The source manager has eight registration slots and several goal kinds. Its
kind-zero registrations feed one primary goal receiver, rather than eight
independent primary markers. This slice implements that receiver used by
One Shot Kill. Additional goal kinds require their own source consumers; this
implementation does not silently generalize them into the same marker.

## Original implementation

Evidence is the hash-bound unpacked Steam executable
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
The relevant call graph is:

- `48ee60`: resolve registrations and republish the primary target on admitted
  world updates. Pause/cockpit admission precedes this work.
- `48ded0`: accept the current target; new identity starts presentation, while
  repeated publication retains its visual state.
- `48df30`: project the target, advance begin/live/withdraw/fade/retire phases,
  rotate the marker, calculate relative bearing and player distance.
- `48d410`: inverse camera plus reversed-depth perspective projection onto the
  logical 1280×720 canvas. Reconstruction reuses `make_view_transforms` and
  shared vector arithmetic.
- `48d960`: emit the primary marker, or the offscreen directional arrow.
- `48d7d0`: construct/clamp the directional arrow's elliptical edge placement.
- `4c5b40` / `4c5fb0`: background and right-anchored distance digits.
- `564900`: select a horizontal atlas cell in pixel coordinates, converted to
  normalized UVs by the resident-image surface. There is no row wrapping.
- `572660` mode one: opacity approaches its target by
  `min(6 * admitted_seconds, 1)` per update.

The main marker uses images 25 and 26 with opposite rotations on a three-second
cycle, plus image 27. Visible projection requires depth in [0,1), x strictly
between 32 and 1248, and y strictly between 32 and 688. Its base is projected
position minus (32,96). Image 27 is offset another (16,52). Distance background
432 is at projected position minus (0,148); digits 433 are 24×40 cells with a
14-pixel advance, emitted from the least significant digit leftward from
(projected_x+95, projected_y−137).

The original distance calculation includes the homogeneous component: target
w=1 and player observation w=0. Reconstruction preserves that contribution
rather than substituting a different distance measure. The display truncates
positive distance plus 0.01 to an integer.

Offscreen image 28 is centered on an ellipse scaled by 1.181 and clamped to
x=[128,1152], y=[72,648]. Its rotation follows the camera-relative planar bearing.
The bearing uses the original inverse-camera row: `atan2(target_x−eye_x,
target_z−eye_z) + atan2(camera_right.z, camera_forward.z)`. This preserves
pitch/roll behavior that a simple camera-yaw subtraction would change.
The two-second pulse adds the source uniform-light term to packed vertex color;
the original UI shader saturates that color before sampling. This is expressed
through the existing bounded sprite tint, without a new lighting pass or clock.

Withdrawing a target retains its last position during the original fade phases.
Mission teardown clears it immediately. PlayerCockpit gates updates using world
admission, not a zero-delta approximation that would still advance task phases.
System messages are separate immutable presentation snapshots: their playback
owner is MissionScriptRuntime and may advance outside world-pause admission.

## Ownership and verification

The HUD owns fixed arrays, publishes const command spans, and allocates no memory
per update. Target setters validate finite positions. Observations are borrowed
only for the synchronous update. The renderer surface owns textures and resolves
atlas dimensions; it does not interpret mission state. Core F.16 and performance
MEM.9 were read in both design and implementation phases.

Ignored executable witnesses are under
`research/evidence/reconstruction/mission-startup/goal-ui/`. `draw_witness.py`
executes original `48d960` for onscreen and offscreen cases, recording the actual
sprite requests. `digits_witness.py` also executes original `4c5fb0`, recording
its three digit requests for distance 123.45. Sine/cosine are replaced only at
the imported math boundary to avoid the emulator's unsupported instructions;
source sprite geometry and selection execute unchanged.

The in-process HUD contract test matches original marker positions/rotations,
distance digit order/size/advance, offscreen placement, target withdrawal and
invalid-target rejection. Runtime tests pass. These witnesses validate source
command construction, not a recorded visual comparison of a live mission.
