# Shadow caster owner selection

The original selects registered owners with two independent tests. It does not
apply a universal mesh radius, distance cutoff, or final shadow-map clip test.
This contract comes from executable control flow and constants; it does not use
Unreal as a replacement because the relevant original algorithms are readable.

## Evidence and scope

Original executable SHA256 is
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
Existing passes 05c/05d, 51/52, 70 and 115 supply owner constructors, dispatch,
shadow setup and model-state transitions. A read-only Ghidra 12.1.4 export of
`5e4ee0`, `5e1b10`, `5f0900`, `5e8fa0` and `40b2a0`, including instructions
and immediate call relationships, is retained under ignored
`research/evidence/reconstruction/lighting-redesign/shadow-owner/`. The authored
summary here contains no raw decompiler output. That directory also retains
synthetic original-executable Unicorn witnesses and their results.

## Registered owner contracts

All offsets below are hexadecimal bytes. Vtable addresses identify dispatch,
not proposed engine class names.

| Owner | Primary / adapter vtable | Projected callback | Intersection callback |
| --- | --- | --- | --- |
| Static model | `850e28` / `850e48` | `4ece50`: size1 | `5ce4c0`: score0, always admitted |
| Placed background object | `850ea4` / `850ecc` | `5f0900`: world sphere | `5e8fa0`: flags and volume |
| Alicia | `850d9c` / `838278` | `5e4ee0`: local owner sphere | `5e1b10`: pose flags and model group0 |
| Weapon model | `850d68` / `838278` | `5e4ee0`: local owner sphere | `5e1b10`: pose flags and model group0 |

`5e2f80` obtains the inverse active-camera transform and
`q = 2 * camera.near / (camera.top - camera.bottom)`. Each registered adapter
must return a projected size **at least** AREA85 / stage `+104`. Only then does
its owner's virtual slot `+14` receive the light volume. Any return other than
negative maximum finite float is accepted, including a negative score. The
shadow submission later visits only accepted owners. Registration alone is not
caster eligibility. The authored traversal-enable bit remains a separate gate.

Static geometry reports size1 and intersects unconditionally. Static submission
still has its hidden-owner check and selectors0/1; chunk3 is not a shadow draw.
The `_background` filename category does not turn a static model into the
separate placed-object owner.

For placed objects, `5f0900` uses world center `+a40` and radius `+a3c`.
`5f0490` initializes both `+a38` and `+a3c` from group0's authored radius and
transforms the authored group0 center by the object's model matrix at `+a0`.
For either sphere projection, transform the center to active-camera depth `z`;
return maximum finite float when `z <= 0`, otherwise `radius * q / z`. This is a
radius, not diameter or Euclidean distance. No additional world-scale radius
multiplication occurs in these callbacks.

Alicia/weapon projected bounds are separate from their intersection bounds.
Constructors `5dfd50/5dfc60` initialize local center `(0,.9,0,1)` and radius
`.95`; the callback transforms that center through owner matrix `+50`.
`603a60` is an explicit bound override. Do not replace this projected sphere
with the mesh sphere. Intersection instead obtains **model group0** radius
through `56b3e0/42a760`, authored center through `42a720`, and model-world
transform through `56a8f0/42a570`. Group0 data holds center at `+30` and radius
at `+40`; missing data returns the original zero/default bound, not a fabricated
character radius.

Actor intersection predicate order is exact:

1. Pose-ready byte `+4ac0` must be nonzero.
2. Force-visible byte `+5334` admits immediately with score100.
3. Otherwise disable byte `+5335` rejects.
4. Otherwise query the independent group0 sphere against the light volume.

The projected-size callback returns maximum finite float when force-visible is
set. Constructors clear all three flags; model update `5e37f0` sets pose-ready
after model/pose update. Common **magic** entry `613570` force-enables and exit
`6137c0` clears force/disable; Thunder/Tornado/reaction owners and script toggles
also write these flags. Ordinary locomotion is not evidence for unconditional
force-visible. Runtime model state must publish these facts; the generic query
has no Alicia-specific default or implied ready state.

## Five-plane volume and distinct selection camera

`40a860` constructs four outward side planes and one far plane from a rigid
camera. For near-plane corners `(left,top,near)`, `(right,top,near)`,
`(right,bottom,near)`, `(left,bottom,near)`, rotate their direction vectors into
world space. Side normals use normalized cross products of pairs `(3,0)`,
`(1,2)`, `(2,3)`, `(0,1)`; plane constant is `-dot(normal,eye)`. The far normal
is camera forward and its constant is
`-dot(forward, eye + forward * far)`. Store `far-near` separately.

`40b2a0` rejects only when any signed plane distance exceeds sphere radius.
Contact is admitted. There is **no near plane**. The returned accepted score is
`(far-near) + farPlaneDistance - radius`. Its sign is not admission. A sphere
inside the side planes but before the near plane remains accepted.

`457620` builds this selection camera separately from `452460`'s final warped
raster/receiver projection. The existing `shadow.cpp` cone-fit origin, basis,
height-clipped corners, offset and near/far are not this camera. Use:

- Adjust the original stage direction by `+.001` in Z once. Let `d` be that
  vector pointing toward the light. Build the rigid light basis with forward
  `-normalize(d)` and world up `(0,1,0)` through the source basis routine.
- Light eye is `activeCamera.eye + normalize(d) * 10000`.
- Construct eight active-camera corners at the camera near plane and authored
  shadow far distance. Transform them to world, then this light camera.
  There is no height clipping on this selection path.
- For every light-space point, project X/Y by `5000 / Z`; retain both X/Y
  minima/maxima and maximum Z. Selection extents are these four extrema,
  near5000 and far=maxZ. Build the five-plane volume above.

The final warped shadow projection remains unchanged and is used by both
casters and receivers. Reusing its clip rows for this owner-selection query
would change original behavior.

## Placed-object alternate path

`5e8fa0` first requires owner flags bit `0x1000`. Initial BGOB construction does
not turn that bit on merely because the object registers. Normally it queries
its world sphere against the light volume. If all of the following hold, and
an active camera exists, it uses a different source path:

- light volume depth range is at most2000;
- low three owner flag bits equal7;
- original radius `+a38` is greater than10.

Let `F = max(activeCamera.far,1000)`. Reject when
`distance(camera.eye,sphere.center) - sphere.radius > F`. Otherwise construct
the active camera's five-plane volume with far replaced by `2*F`, retaining
its near/extents, and query that sphere there. Missing active camera falls
back to the light-volume query. This is not a blanket long-distance exception.

## Design and acceptance

The generic engine boundary is `scene/visibility/shadow_casters`: explicit
camera, independent world spheres and model state, with separate static/model/
placed policies and explicit admission results. It uses shared math/frustum
operations and fixed storage; no callbacks or runtime allocation are required.
Both design and implementation query the Core and performance MCP corpora;
Core I.1 makes state/coordinates explicit and MEM.9 keeps steady-state storage
bounded. Original algorithms, not a modern heuristic, determine this boundary.

Required coverage includes static threshold equality, behind-camera projected
admission, exact size threshold, independent intersection bounds, pose-ready /
forced / disabled precedence, before-near acceptance, contact and outside-far,
placed enabled/mode flags and the alternate camera volume, and selection-volume
planes against original execution. Runtime actor flags and instance identity
must remain owner supplied. Offscreen native verification and real-frame review
belong to the integrated redesign, not this source-only witness.

## Implementation and verification status

The visibility header, implementation and in-process resource tests now express
these three owner contracts. The existing math five-plane implementation is
reused. Invalid selection fits fail explicitly; no enlarged fallback volume or
camera-distance substitute is published. Queries require previously validated
camera/volume values and validate consumed bounds/thresholds. Their supplied
world spheres retain owner-specific transforms and radius policy.

Fourteen synthetic original-executable owner/volume cases passed: projected
front/zero/negative depth, actor force, before-near acceptance, far contact and
rejection, side rejection, and ready/force/disable precedence. Two additional
`457620` executions capture the actual selection volume at origin and a
translated camera. Those runs stub only the separately produced final raster
matrix to identity and stop at `5e2f80`, before owner registry traversal.
The stub does not feed selection-camera calculations. The captured planes are
literal independent expectations in the resource test, with float arithmetic
tolerances of `1e-5` for plane normals, `.02` for plane offsets and `.01` for
depth range, against the original 10000-unit light construction.

The public-contract suite compiled and passed independently with the project
warning flags. Pinned fatal clang-tidy passed both translation units and the
included header; no suppression was added. Full resource/native suites and
`check.py all` remain the integrating lead's gate. No visible game launch,
commit or push was performed by this subagent.

The source flag distinction remains an integration obligation. Common magic
`613570` and Thunder aiming/strike write the **body** model at actor `+50`;
weapon attachment owners are separate and those routines do not force the
weapon. Tornado's additional explicit force write targets its newly created
retained tornado model, while the body uses common entry. Rose Bomb bypasses
common entry and must not inherit its force flag merely because it is a spell.
Source exit paths clear body flags. No generic `magic => all models forced`
classification is supported. Future scripted visibility changes and missing
spell/effect/world owners must publish their actual model state when added.
