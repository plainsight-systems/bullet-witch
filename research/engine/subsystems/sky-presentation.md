# Sky presentation reconstruction

The static inspection has decoded and resident sky geometry/texture data, a
source-derived camera-relative transform, and the original nonwriting,
depth-disabled submission state. The black backdrop was traced to an omitted
runtime spherical-harmonic feature bit, which selected the wrong cached color
program. The corrected view retains the source white harmonic coefficients;
material ambient remains zero. Native capture verification follows that fix.

## Original instance and pass

The retained `pass-15-sky-transform`, `pass-18-sky-object` and
`pass-28-stage-instances` exports establish:

- Stage construction chooses the ordinary sky constructor `0x44fc40` when its
  model has no skeleton or material-animation owner. `sky04_c` has neither.
- That constructor initializes the common scene object and model render owner,
  then an empty animation controller. It contains no sky-specific constant
  ambient or illumination assignment.
- The sky draw body at `0x56b440` (exported under the inferred name `0x5e3660`)
  composes authored yaw/height and adds the active camera eye. It sets a constant
  white spherical-harmonic input before submitting the model. There is no scale
  correction or far-plane projection override in this body.
- Sky commands occupy the list after the depth-state bucket installed by
  `0x460ed0`. That bucket disables depth testing and writing. The portable view
  represents it as always-pass/no-write; ordinary world geometry follows.
- Spherical-harmonic upload targets pixel constants 9–15. The initial inspection
  omitted feature 0x200 and selected pixel 362, which reads illumination 16 and
  ambient 17 but ignores harmonics. The actual source constructor enables that
  feature, selecting pixel 363 and its harmonic polynomial. A white harmonic
  input therefore never required substituting ambient 1.

Original `cm01_00` Lua `start()` does not call a sky, light or material setter.
It configures background objects, the player pose, mission traps/goals, visibility,
pause/resume, fades/messages and BGM. Native setup before that callback still
needs its own evidence; absence of a Lua setter does not prove initialization is
complete in the static inspection.

## Installed sky material comparison

The ignored read-only `sky_material_comparison.cpp` enumerates `model.sky*.mesh`
from the installed stable catalog and uses the production CPU model decoder.
It does not render, mutate installation files or substitute default constants.
Its retained `sky-material-comparison.log` contains 11 sky namespaces and 32
materials. Every material omits `AMBI`; 26 have `LUMI=(0,0,0,1)`.
The primary materials of `sky01`, `sky02`, `sky03`, `sky03_e`, `sky04_a`,
`sky04_b`, `sky04_c`, `sky05` and `sky81` share that zero-illumination pattern.
Six accent materials differ: three in `sky02` use RGB 0.025; three in `sky03_e`
use RGB 100, 8 and 28 respectively. Their alpha remains 1.

This comparison ruled out treating the zero constants as unique to `sky04_c`.
It exposed the contradiction that led to checking the omitted runtime feature;
zero material illumination was not evidence for an intentionally black sky.

Shader identities must remain tied to their source registry. For example, the
embedded PE registry's program 13 is a 159-instruction lit/shadowed/fogged pixel
program, not a base-texture/no-fog program. An installed shader-package index 13
would be a different identity until its bytes and lookup key are compared.

## Diagnostic camera evidence

The same production initial-camera constructor and registered static collision
produce checkpoint 10 eye `(-7.98534,1.35,54.8025)` and checkpoint 20 eye
`(76.5207,0.8,71.3654)`. Their authored yaw values are -0.28 and -61.25 degrees;
both diagnostic views retain the original 44-degree perspective. The upward
variant rotates checkpoint 10's view 45 degrees without executing a checkpoint,
actor movement or save mutation.

Each ignored offscreen capture advances 90 starting-view frames followed by 180
exterior frames using the same native renderer, steady clock and exposure
history. The PNG, raw half-float HDR target and pose log are retained under
`research/evidence/reconstruction/stage/offscreen-stage-checkpoint*`.
The pre-correction views show textured industrial geometry, heavily clipped
ground and a black sky. They are preserved diagnostic evidence, not accepted
original-game image parity. Corrected captures use different filenames.

## Material key encoding

The original file lookup `0x42fbb0` formats its key as
`%08x%01x%02x`: all 32 feature bits followed by byte 0 and byte 1 of the material's
second selector word. Cache lookup `0x42f970` hashes and compares the same fields;
compilation `0x42f4a0` also reads only those two bytes for texture variants.
Thus `sky04_c` selector `(0x06600000,0x21690001)` has texture variant `(1,0)`;
its upper 16 bits are not an alternate shader program selector in this path.
The incomplete inspection feature mode produced key `06e00000100`, whose
embedded pair is vertex448/pixel 362. Constructor `0x42fb85` writes packed bytes
`(0,1,1,1)` to shader owner offsets 0x1adfc–0x1adff. Byte 0x1adfd enables feature 0x200
in `0x42f970`; no other direct offset writer appears in the executable
instruction census. The known state executors change the neighboring bytes,
not this one. Thus the source sky key is `06e00200100`, vertex448/pixel 363.

The sky's white harmonic constructor multiplies RGB1 by 3.544907569885254;
packing multiplies the constant term by 0.2820947766304016. Original float
rounding yields 0.9999998807907104 in the W component of each of the first three
packed vectors. The next three vectors are zero, and the seventh is `(0,0,0,1)`.
The shared draw explicitly supplies these seven vectors and the enabled feature.
The native shader evaluates the actual polynomial; AMBI remains zero and LUMI
remains the authored value. The existing original-root corpus asserts this
contract alongside eye-relative sky placement and disabled depth.

The same omitted runtime feature affected ordinary stage materials. Their
original virtual submission hook `0x5e46d0` takes RGB from the first three stage
customization floats, appends alpha1, and calls the same constant-harmonic
constructor and uploader. The selected stage authors RGB `(1,1,1)`; the following
float0.5 belongs to another field and is not ambient alpha. The game view now
supplies authored RGB harmonics for ordinary objects and source white for sky.
Their material ambient and illumination constants remain independent.
The selected cached world families with feature0x200 retain the recovered
polynomial, including families whose directional/shadow terms remain explicitly
omitted by inspection policy.

## Corrected native verification

After restoring the runtime feature and per-object coefficients, production
captures `slice3-checkpoint10.png`, its `initial090` starting-view capture, and
subsequent checkpoint/upward diagnostics show textured sky and ground together.
The earlier black-sky and clipped-ground images are retained separately.
No exposure, material ambient or light value was adjusted to improve appearance.

The actual `StartupHost` window was also launched at checkpoint 10, using the
same `WorldOutput` adapter used by the application. It presented 4,097 ordinary
world draws plus the sky. `slice3-host-checkpoint10.png` was captured after the
probe reported 60 frames; the sibling log records eye
`(-7.98534,1.35,54.8025)`. The screenshot shows clouds, mountains/trees,
industrial buildings and textured ground, confirming that the application host
receives the same output policy and irradiance data as the direct native probe.
These are ignored research artifacts under the stage reconstruction directory.

The strict integrated build, six existing CTest entries, original-root corpus,
and fatal clang-tidy for all 96 compilation entries pass. Native numerical
coverage includes constant sky irradiance with zero material ambient, a
quadratic coefficient evaluated at the geometric normal, transformed-normal
response, and ordinary irradiance added to baked vertex light before texture
modulation. Full shadow/visibility and gameplay-frame parity remain outside this
static view; the source-backed sky contribution itself is now implemented and
visually verified.
