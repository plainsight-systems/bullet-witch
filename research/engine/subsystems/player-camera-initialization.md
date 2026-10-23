# Player placement and initial camera

## Scripted placement

The recovered One Shot Kill script `cm01_00` places Alicia in `start()`, not
`load()`. At lines31–36 of the recovered script, checkpoint0–9 uses
`_SetPlayerPosition(-61.91, 0.65, -97.57, -0.28)`. Checkpoint10–19 uses
`(-8.5, 0.65, 57.8, -0.28)`; checkpoint20+ uses
`(73.65, 0.1, 72.37, -61.25)`. This mission has no script-camera calls.

The native adapter `0x47c470` copies XYZ without scaling and converts the
fourth argument from degrees to radians. Rotation helper `0x435110` builds
rows `(cos,0,-sin,0)`, `(0,1,0,0)`, `(sin,0,cos,0)`, `(0,0,0,1)`.
It dispatches the full pose through the player virtual at slot+0x1c.
The prior inspection view used the authored XYZ but independently chose
height1.6 and heading+Z. Those choices are not the original camera baseline.

## Original camera owner and initial preset

Player constructor `0x5fe970` registers the actor in the player list and sets
vtable `0x852378`. That table's pose slot resolves to `0x604a40`. The setter
updates actor translation, posts the actor pose and copies the authored yaw
basis to its camera anchor at player+0x1218. The anchor is a transform object
created by `0x5fe070`, attached to the player's child list. Camera constructor
`0x619cc0` creates the separate camera at player+0x1214, parented to this anchor.

The player constructor obtains `witch/actor/pw0/behind_camera` from
`customizer.default`. It initializes the camera-mode nibble to2 and invokes
`0x6047f0` with transition disabled and snap enabled. The selector computes a
0x1c-byte preset index from mode bits and an alternate-state flag. With those
constructor values it chooses record2. Initializer `0x602390` explicitly clears
the alternate-state bit at player+0x1238 and sets mode2/zoom0, so this choice does
not rely on assumed zero-filled allocation. The authored values are:

| Input | Value |
|---|---:|
| Vertical offset |0.7|
| Camera-local lateral offset |0.5|
| Camera-local extra vertical offset |0|
| Camera-local rear offset |-3|
| Positive/negative pitch compensation |0 / 0|
| Vertical field of view |44 degrees|
| Offset adjustment speed |5|

Setup `0x61a850` stores desired offsets and, because snap is enabled, copies
them directly into current offset state. It also builds the frustum using the
selected preset's angle and the camera's general settings. The independent
projection trace owns the exact viewport/aspect and final view-matrix mapping.
The settings dispatcher `0x629900` is a once-only registered-settings callback
walk; it is not evidence for substituting guessed camera distances.

## Collision and frame boundary

Camera update `0x619e90` composes its local basis with the parent transform,
then applies the height, lateral and rear offsets in separate stages. Each
stage uses a segment collision query with 0.05 clearance through `0x5f3e10`.
This is not evidence of a sphere sweep. A collision can shorten
an offset and updates the retained current offset. The camera also advances
current offset state toward the configured target using elapsed time. It ends
by applying optional camera vibration through `0x5fc290`; the frame rendering
path separately constructs the inverse view and projection.

Thus the source-backed unoccluded initial view is offset0.5 to Alicia's right,
0.7 above and3 behind, following her scripted yaw. If its composed anchor has
the initial actor position/yaw and none of the collision queries shorten these
offsets, the eye computes to approximately
`(-61.395345, 1.35, -100.567521)`. This conditional calculation is evidence for
verification, not a replacement hardcoded camera position. Exact collision
results, the first update's parent transform, later input/follow transitions and
animated character attachment behavior must be preserved or verified before
claiming the full original gameplay camera.

The bounded implementation uses the game camera owner consuming
scripted actor pose and retained authored camera settings, with the renderer
consuming its resulting view/projection. Adding another fixed eye to
StageInspection would leave the same missing ownership in place. The source-backed initial snapshot now has explicit implementation boundaries
described below; it does not execute mission start or simulate the actor.

## Evidence

The exact recovered script is in ignored `research/extracted/scripts-recovered/`.
Existing native exports contain `0x47c470` and `0x435110`. Engine passes32–36
retain the player constructor, pose setter, camera configuration and update
exports. The hash-bound CSTM extraction and numerical preset witness are in
ignored reconstruction/stage evidence as `customizer.bin`,
`customizer-tree.json` and `player-camera-initial.json`. Tree JSON offsets denote
payload starts, already16 bytes past their node headers.


## Effective projection and matrix convention

The effective preset is **44 degrees vertical**, aspect **16:9**, near **0.01**
and far **1000**. This is not inferred from a conventional game camera:

- `0x6047f0` supplies the selected preset angle after its degree-to-radian
  conversion. `0x61a850` halves that radian angle, takes its tangent and
  multiplies by the near plane to obtain vertical frustum half-height. Horizontal
  half-width is that height multiplied by `0x5fa9a0`.
- `0x5fa9a0` is a direct load of the float at `0x82f3e4`,
  `1.7777777910232544`. This source getter uses a constant aspect, not current
  drawable dimensions. The portable view should retain that authored ratio;
  viewport fitting is a separate target presentation responsibility.
- The near/far values are the first two floats in `witch/general/camera`,
  extracted from CSTM payload offset21152. They are 0.01 and1000.
  `0x619db0` publishes that node, `0x619cc0` retains it at camera+0x1a0, and
  `0x61a850` copies those values into the frustum. The frame entry `0x45e130`
  also refreshes far from the same general camera settings.
- The constructor `0x485b00` initially uses 45 degrees, near0.01 and far200.
  Those constructor values are superseded by the selected player preset and
  general settings; they are not the effective baseline.

The camera's composed world transform at offsets0x50–0x8c stores right, up,
forward and translation as successive four-float rows. Local points multiply
this row-vector transform. `0x40a5b0` copies that transform, inverts it with
`0x4094b0`, then multiplies inverse-view by projection using `0x406690`.
Its frame record contains world at+0x20, inverse-view at+0x60, projection at+0xa0
and the combined world-to-clip matrix at+0xe0. Shader-facing dot-product rows
must account for this row-vector CPU layout rather than copying its row vectors
as if they were column-vector output equations.

For frustum bounds `(l,r,b,t,n,f)`, the actual projection builder writes:

```text
P = [ 2n/(r-l)         0              0          0 ]
    [     0        2n/(t-b)           0          0 ]
    [ (l+r)/(l-r) (b+t)/(b-t)      n/(n-f)       1 ]
    [     0            0         -nf/(n-f)      0 ]
clip = world_position * inverse(camera_world) * P
```

This is positive camera-forward Z and zero-to-one **reversed depth**: near maps
to1, far to0, with clip W equal to view Z. The independent camera-space helpers
`0x439dc0` and `0x48d410` construct the same coefficients. The world camera
command path `0x44f710` calls the common builder with depth enabled; `0x45fb70`
also uses it for its camera passes. This is stronger evidence than the unrelated
D3DX perspective import, whose one direct call is elsewhere in the executable.
The main-world setup `0x460920` queues comparison value7 (GREATEREQUAL),
and the recovered static vertex programs pass clip Z directly without a depth
remap. Main target construction `0x462cc0` writes clear depth0 and enables depth
clear for each of its three targets; selector `0x463800` returns those same
targets and `0x4079b0` forwards the retained value to D3D Clear. Thus projection,
GREATEREQUAL and clear0 form the recovered main-world depth contract.

These findings are retained in native passes34,36,37, plus the earlier
`pass-05f/functions/0040a5b0.c` and its instruction witness. The raw general-camera
CSTM words are recorded in ignored `general-camera-customizer.json`. No camera
or projection C++ changed during this trace.


## Shared render view contract

`include/bw/render/view.hpp` and `src/render/view.cpp` implement the rigid,
symmetric subset used by this initial camera. The original production builder
inverts the composed camera transform and multiplies by its frustum projection;
the shared implementation specializes that operation to an explicitly validated
orthonormal basis and symmetric vertical field of view. It does not normalize
invalid input, select a gameplay preset, infer an eye or read drawable state.
Finite preconditions and representable output are checked before publishing the
fixed-size result. This narrows the accepted domain without changing the original
valid rigid-view equations; arbitrary scale/shear and asymmetric frusta are not
claimed by this contract.

Material coordinates are a distinct output. In
`pass-08-world-materials/functions/0042ffc0.c`, the original draw upload keeps the
model basis unchanged and subtracts context eye XYZ from model translation before
publishing vertex constants16–19. The context writer in
`pass-08-world-environment/functions/0045fac0.c` stores eye XYZ at+0x90 and removes
translation from its inverse camera matrix. Therefore `camera_relative_world_rows`
subtracts the eye while retaining global axes; `view_rows` rotates into camera
coordinates and `clip_rows` adds perspective. A model-space caller still owns
composition with its model transform. Rotating material rows with the camera
would incorrectly rotate light/normal/reflection relationships.

Both design and implementation consulted the C++ MCP corpora: I.1 makes
coordinate and failure contracts explicit; MEM.9 keeps the calculation in fixed
arrays without successful-path dynamic allocation. Numerical cases in the
existing resource-test executable cover an independently chosen rotated,
translated camera, frustum corners, reversed near/far depth, behind-camera W,
material-axis preservation and invalid input rejection. They establish the math
contract, not gameplay-camera collision or image fidelity.

Shared-view verification: the strict resource-test build and existing executable
passed, and both new translation units passed fatal clang-tidy. These scoped
checks precede the final integrated camera/native checks.


## Static camera clearance reconstruction

The initial clearance does not require a PhysX scene. The original camera uses
its own registered collision triangle query. Passes38–42 retain the complete
registration, triangle selection and closest-contact chain:

1. Ordinary stage construction registers its primary collision through
   `0x45bdb0` → `0x5f53f0`. A combined `.hit` takes priority; otherwise `.chit`
   and `.ehit` populate the two static sets. These are distinct from render
   mesh triangles and from the retained `.phit` SDK graph.
2. `0x5f4ea0` classifies each part. `wa_` and `de_` become separate area
   registrations. Flag0x40 selects ordinary category1; otherwise flag0x20
   selects special category2, which does not enter the regular triangle set.
   Character-collision `pp_`/`hp_` parts are excluded from that set. Name
   matching is ASCII case-insensitive. Ordinary category0/1 parts retain
   world-space geometry with object flags0x10001. `pw_`, `pc_`, `np_`, `ne_`
   and `nc_`/`cc_` add masks0x1200,0x3200,0x200,0x2100 and0x2200 respectively.
3. `0x5f3e10` requests exclusion mask0x2000. `0x54b1d0` also excludes the
   disabled high bit. This excludes camera-marked parts without pretending
   those parts were absent from general collision registration.
4. The requested segment is extended by `clearance / 0.01` and represented
   about its midpoint. Bounds reject unrelated objects. `0x55e630` dispatches
   the query's default front-face triangle test `0x54fe50`: positive determinant
   at least1e-6 uses the ordinary barycentric ray test. Negative determinant
   rejects. A nonnegative smaller determinant admits the coplanar branch only
   when the exact plane-distance expression is zero.
5. Coplanar helper `0x5503b0` tests the triangle's three edges through
   `0x54c270`. It normalizes the directions, clamps the closest point to each
   edge and accepts squared separation at most1e-6. Parallel directions select
   the edge midpoint. The earliest line parameter wins, subject to the source
   distance tie condition. Segment bounds then reject contacts outside the
   centered half-length. Normal contacts also use the nearest accepted hit.
6. `0x5491d0` obtains the normalized winding normal. A degenerate normal uses
   minus query direction. Clearance is divided by dot(minus direction,normal)
   when its absolute value exceeds0.0001. The resulting travel is clamped to
   zero; it shortens the request only when less than its original length.

The three camera calls use0.05 as an incidence-adjusted separation margin. It
is not a sphere radius. Primitive triangle flags do not add a filter in this
query's recovered branch. The static implementation preserves part/category
and face behavior, using a bounded linear part traversal plus AABB rejection
instead of reproducing the original grid. It retains immutable collision byte
views rather than copying triangles. This changes broad-phase cost, not the
selected narrow-phase algorithm. Dynamic actor/destructible registrations are
not present in this initial static snapshot; they remain an explicit future
scene owner.

## Implemented boundaries and verification

- `formats::CustomizationDefinition` validates/indexes the shared CSTM tree;
  consumer-specific decoders resolve bounded payloads. StageDefinition retains
  the generic customization publication and does not own player camera policy.
- `formats::PlayerCameraSettings::parse_initial` decodes the recovered ordinary
  initial preset and general frustum settings. The metadata index uses the
  supplied memory domain; no proprietary bytes are copied into code.
- `scene::StaticCollision` retains registered collision part references and
  answers the source segment-clearance operation without per-query allocation.
- `game::make_initial_player_camera` takes an explicit scripted actor pose,
  applies its original yaw basis and performs vertical, lateral, then rear
  clearance. Its returned renderer pose/frustum is independent of its inputs.
  The parent transform is the explicit actor pose for this bounded initial
  snapshot; animated attachment, later camera input, smoothing and vibration
  are not claimed.
- The in-process resource/runtime suites add synthetic tree rejection, source
  preset selection, registration filters, front/back/coplanar contacts, extended
  clearance and yaw/ordered-camera composition. Actual acceptance results and
  the collision-adjusted native eye are recorded after execution, separately
  from these contract cases.

The reference-first choice is the traced original implementation above. A
sphere sweep from another engine would change this camera's baseline; the
source algorithm is therefore preserved. The renderer's shared matrix contract
is independently compared with pinned production GLM source in the projection
research. Both C++ MCP corpora were consulted in design and implementation:
R.2 supplies bounded borrowed-view interfaces, and MEM.6 injects all retained
metadata allocation while leaving the camera/query successful paths free of
heap allocation.


## Actual static-view witness

The headless production-view diagnostic rebuilt against the integrated owners
and parsed the retained stage customization. It reported height0.7,
offset `(0.5,0,-3)`, vertical angle0.767945 radians, aspect1.77778, near0.01
and far1000. Registering the original ordinary-static collision inputs produced
2150 parts. The production initial-camera calculation returned eye
`(-61.3953,1.35,-100.568)` and forward `(-0.0048869,0,0.999988)`; the result agrees
with the unshortened source-offset witness for this static registration.

The same production StageInspection and Metal presentation path produced
`research/evidence/reconstruction/stage/offscreen-stage-original-camera.png`
(ignored), with4097 world draws and one sky draw. Its sibling `.png.hdr.bin`
retains the independently captured pre-display production surface pass as
1024×576 row-major little-endian RGBA16Float; `.png.hdr.json` records numerical
samples. The image visibly contains the cave floor, gate and exterior from the
lower original initial camera. The exterior remains bright in the first-frame
display output; this observation is not approval of complete original visual
fidelity. The actor pose is still an explicit source bootstrap rather than an
executed mission `start()` or live player controller. No new normal-menu GUI
capture was made during this headless verification.


The final format guard uses `0x54a460` and its pinned PE table at0x83f968:
vertex widths are4,3,4,7 floats for selectors0–3. The current typed collision
view supports selector1 (float3) only; spatial registration rejects the other
encodings before publishing any part. Legacy combined `.hit` uses the different
part header read by `0x54a410` and is also explicitly unsupported. The selected
stage uses `.chit`/`.ehit`; no legacy combined constructor is invented. These
exports are retained in pass43. Registration validates finite selected vertices
once, while every query remains allocation-free.
