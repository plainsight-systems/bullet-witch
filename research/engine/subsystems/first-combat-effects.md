# First combat effect execution

October 27, 2026. Parent: [first combat exchange](first-combat-exchange.md).
Original foundation: [particle runtime contracts](particle-runtime-contracts.md).

The bounded original sprite runtime and native draw consumer are implemented.
This report distinguishes component verification from the remaining combined
mission integration and visual acceptance.

## Original coverage

The ordinary player/enemy weapon closure contains fourteen effect IDs and
27 distinct authored part slots: 0, 19, 42, 300–305, 790, 795, 801, 1220 and
1562. All are periodic sprite parts; none has a nested sequence/child effect.
They use shader 0, blend modes 1/2, five orientation modes (-1/0/1/2/4), local
and world positions, velocity/size/angular curves, random texture selection,
texture-border sampling and color curves. Their 41 texture identities resolve
to PNG payloads behind original DDS publications. Original texture construction
and its independently selected mip policy remain shared with other content.

An earlier closure incorrectly read `part+178` as an integer flag. The binder
reads a float: effect42, common part33, contains 0.06. Its motion therefore
requires real old-to-new segment queries, contact offset, velocity response,
0.5 attenuation and 0.2 angular perturbation. The collision child is absent.
Original `5491d0` returns the geometric triangle normal for source type5;
`405070 → 44b9d0` transforms that direction when required. Other receiver kinds
use the original constant response axis. Provider surface IDs must not stand
in for this game receiver classification.

Part178 of effect790 also uses squared camera-distance alpha fading. The
runtime preserves this separate from GPU distance fog. Part-level sorting is
inactive in this closure; unsupported positive sort modes reject preparation.
Instance-level depth sorting is active and implemented.

## Ownership and reference

The original manager owns active instances; instances own ordered part slots;
parts own particles. One effect RNG spans those owners. Its sequence depends on
instance/part/particle order. `53b360 → 53b430` sorts the active list itself,
which `52e8e0` subsequently updates. The implementation therefore preserves the
source depth bias, successive epsilon and comb-sort gap/equal-key behavior;
it does not maintain an unrelated draw-only sort or per-emitter RNG.

`SpriteProgram` owns prepared immutable curves, typed part data and texture
region descriptions. `SpriteWorld` borrows a stable program span and const
world clock and session-owned `ParticleRandom`. The particle stream is original
global `969100`, initialized to 1, with multiplier `0x41c64e6d` and increment
`0x3039`. It is distinct from game `SourceRandom` (multiplier 69069, increment 1).
Mission reconstruction never reseeds either stream. It owns generation-checked
instances, particle pools and draw
storage in the injected domain. Valid steady-state operations allocate nothing.
It accepts each admitted world frame once; paused frames consume frame sequence
without advancing simulation. Local particle/emitter ages do not own time.

`SpriteEnvironment` is a read-only force/contact capability. The original
force evaluator is `52ed80`: base force plus live radial force records, excluding
the source instance. `6084f0` explicitly clears the base vector after setup.
The ordinary rooted effects create no radial force records. This is unrelated
to the `545610/545800` harmonic-light construction in the graphics producer.
Future force producers must supply real observations rather than invent wind.

Read Unreal at `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Particles/ParticleEmitterInstances.cpp`:
`FParticleEmitterInstance::Tick`, `KillParticles`, `SpawnParticles` and
`CheckEmitterFinished`. Caller-supplied time, update/spawn phases, capacity
admission and draining/completion support the boundary. Its swap removal,
emission defaults and ordering are not substitutes for the recovered original.
No Unreal implementation is copied.

Core I.11 (`ri-raw`) identifies borrows; SL.con.3 (`rsl-bounds`) bounds inputs.
Performance MEM.9 (`guidelines/memory/MEM.9-embedded-allocate-at-init.md`)
preallocates runtime storage; CDSA.32 prepares immutable consumer data once.
Both corpora were read separately for header design and implementation.
Renderer design and implementation also read GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`) for retained native storage.

## Runtime algorithms

- `EffectPackView` validates seven-section framing, five fixed-stride tables,
  references and bounded records. It borrows immutable resource payload bytes.
  Prepared programs copy required data and retain no payload spans. The boss
  pack's empty nested section is twelve bytes, unlike sixteen-byte fixed-table
  headers. Duplicate IDs within a pack select the final original entry.
- Scalar sampling follows original `52ec80`, including its unusual positive-mode
  terminal word. The unsigned LCG preserves integer modulo and conditional
  second draws in guarded jitter, including zero integer-jitter draws.
- Binding initializes each part's period in order. Eligible updates perform
  initial burst, age/move/retire particles, then one strict-threshold periodic
  burst. Fixed pools preserve source surviving-particle order. Stop disables
  emission and applies the authored fade. Zero fade retires immediately.
- Spawn follows source emitter transform, shape distribution, basis modes,
  speed/angular jitter, lifetime, size and texture selection. The source Euler
  convention is the generic shared `math::euler_zxy_rotation`; no parallel
  vector or matrix library lives in effects.
- Motion samples original velocity/angular curves, applies observed force and
  the traced collision response. Color preserves the vector sentinel, intensity,
  alpha curve, instance fade, RGB tint bypass and camera-distance fade.
- Publication prepares original quad orientations, positions, color and UV;
  adjacent compatible texture/blend ranges coalesce without reordering.
  Semantic retirement need not retain the original allocator's four-frame
  release grace: native publication copies data and the host completes GPU work
  before resources can be recycled.

## Graphics and environment

Source setup `536b20` sets **greater-equal**, not less-equal, depth testing:
`D3DRS_ZFUNC=7`. It disables culling. Blend modes1/2 disable depth writes and
require final alpha greater than 2/255.

| Mode | Operation | Source factor | Destination factor |
|---|---|---|---|
| 1 | Add | Source alpha | Inverse source alpha |
| 2 | Add | Source alpha | One |

VS `83aab8` and PS `83acf8` consume world quad position, color and UV. Vertex
RGB is multiplied by the selected ColorRate. Vertex alpha is multiplied by
`clamp((FogFar - clip.w)/(FogFar - FogNear), 0.02, 1)`. The fragment shader
multiplies texture and interpolated color. Linear/wrap sampling preserves
`536b20`; linear mip filtering is retained from sampler initialization
`451770`, separately from optional original mip construction. The original
64-byte vertex contains unused lanes; the native packet stores only the
36-byte position/color/UV contract and retains exact batch order.

The recovered producer `4571e0` reads alpha RGB from live stage offsets
`a0/a4/a8`, additive RGB from `ac/b0/b4`, and fog near/far from `b8/bc`.
These are not independent global constants: `4829d0` maps AREA channels70/71
to the fog range for version>=102; channels141–143 and144–146 map to the two
RGB rates for version >= 105. Existing original transition/snap ownership applies.

The four sprite command indices are initialized by `6084f0` from `8550c0` to
40/44/45/45. The rooted blended shader0 path selects command2, main queue45
(offset384). `45e130` queues postprocessing at528, afterward. Native sprites
therefore render after world surfaces into the existing HDR scene target,
before `WorldPostprocess`; they do not bypass tone/bloom or draw over the HUD.
`608320 → 520e20 → 5108c0 → 52f8e0` supplies reversed camera projection.

`RenderScene` owns the update-only `EffectsPublisher`. Mission setup configures
bounded storage. Publication resolves resident texture identities, validates the
complete packet, then copies vertices and retains native textures. The private
Metal pass owns the accounted buffer and cached native states. Simulation owns
no GPU resource or renderer registry. The existing host completion boundary
prevents writes/destruction while GPU work consumes the buffer.

## Mission asset and simulation adapters

`CombatEffectAssets` resolves the four original `effect.<pack>.effect_pack`
aliases through mounted catalogs. The original `50ebe0` binds archive record
zero as the pack and records one onward as texture indices zero onward;
`41a310` is direct archive-record indexing. This is independent of the model
`table._tm` path. The owner preserves original pack slots and later-write ID
precedence, gathers the requested roots' referenced texture indices, and uses
shared `decode_world_texture`, optional `generate_texture_mips`, and
`WorldResources::upload_world_texture`. It retains archives and textures until
the mission retires its effect consumers and scene. Partial admission releases
already uploaded handles, including failures from loading-progress callbacks.

`CombatEffects` copies immutable programs during admission, borrows the const
world clock, separate session particle RNG, and query-only `WorldPhysics` observations, and publishes through
`EffectsPublisher`. Texture bindings are prepared by `(pack slot, local index)`;
frame work cannot open files or upload resources. Its copied base-force input
starts at the original zero value. Particle contact queries use the original
scenery bank, with source receiver kind supplied separately from the hit surface
and actor identity. Transformed mesh receivers (kind 5) use the geometric normal; raw stage mesh
receivers (kind 4) and other receivers
use the original fixed up-axis. The query excludes original object flag 8 before closest-hit selection.
Empty-world adapter tests do not establish native contact parity.

`StagePresentation` alone configures native sprite capacity and lends its
publication capability. `effect_view` reuses the existing reversed projection
and sampled AREA effect channels. It converts view dot-product rows into
basis-first affine form, while engine `make_sprite_camera` follows original
`510390`: copy view rotation, form fixed-up cylindrical axes from its third row,
then transpose both billboard rotations. Eye and view translation stay distinct.

## Verification

Production schema and runtime public tests pass, including malformed admission,
shared RNG behavior, capacity exhaustion, pause/stale/foreign-frame rejection,
thread ownership, stop/reuse, allocation stability, depth ordering, squared
camera fade and tint bypass. Strict `bw_effects` and native renderer builds pass.
Scoped pinned clang-tidy passes for runtime, preparation, native helper and
runtime tests. The integrating parent owns the final combined repository gate.

The decoder accepts all four purchased packs: 585 effects, 1,640 parts,
833 definitions, 623 scalar curves and 327 vector curves. Prepared program
admission covers all 14 rooted IDs/27 parts. Synthetic texture dimensions in the
standalone admission witness do not establish actual UV pixel parity. A later
real-catalog loader witness admits the same 14 roots and 27 parts using decoded
resident image dimensions: four packs and 41 selected textures, with mip
generation both disabled and enabled. The resource sink in that witness is a
spy, not a native texture uploader. Injecting failure on the fourth upload
releases all three previously admitted handles; teardown leaves no live domain
allocations.

Independent original x86 execution under Unicorn confirms:

- 82,236 bitwise float32 scalar results: all 623 curves, 33 indices, four fractions.
- All 27 rooted spawn cases, seed 12345: transforms, position/velocity, angular
  state, size/life, texture choice and final RNG state agree within 1e-5.
- All 27 motion cases across .016/.033/.1-second updates agree within 1e-5 with
  zero external force and no contact. This is not a collision parity witness.
- All 27 color cases at 37% life agree within 1e-5.
- The rooted axial case agrees with original 514080 for the identity camera.
- Five asymmetric camera bases exercise original `510390`; ordinary and
  fixed-up billboard matrices agree within 5.97e-8.

Original code executes directly; CRT sin/cos/sqrt imports use host math and the
x87 atan2 ABI. These witnesses do not validate CRT internals, arbitrary camera
coverage, the complete original draw path or whole-game visual parity.

A headless Metal draw/readback probe validates alpha blend, additive blend,
reversed-depth rejection, fog, alpha rejection, caller-buffer copy lifetime and
buffer accounting release. These five native cases use synthetic inputs; no
visible game was launched by this work.

Ignored scripts, captures, original payloads, hashes and diagnostics remain
under `research/evidence/reconstruction/first-combat-effects/`. Source phase
exports are in pass325; no raw decompiler output is tracked. Remaining combined
acceptance includes mission wiring of these resident bindings, actual
shots/impacts, original query filters, scene timing and native gameplay
presentation. Adapter public tests cover copied program ownership, pack
precedence, resident handles, authored color/fog, world pause, no frame
allocations, missing-content rejection and retired-scene rejection.

Weapon request ownership and contact sound resolution are documented in
[first-combat-audio.md](first-combat-audio.md). Retained ownership differs
between the two admitted guns and must not be inferred from backend activity.

## Weapon-owned muzzle presentation

The accepted-shot common exit (`615fc0`) replaces its retained sprite and
muzzle-light handles, attaches both to the muzzle child transform, and creates
light data from `20_muzzleLight`. Constructor `615600` sets flag `0x80`, selecting
visual word zero for ordinary fire; the alternate lane is visual word six.
The light constructor `5f67a0` copies independent diffuse/specular triples and
attenuation, captures the current `witch/light/<stage>` RGB multiplier, and
starts at float bits `3f7ffffe` for positive duration. Its update `5f6d80`
subtracts `delta / duration` and destroys the light at zero. Nonpositive duration
is persistent. `5f6070` is selection ordering, not a lifetime update.

The scene keeps registration and culling authority. A weapon owns a move-only
registration that can update or clear only its emitter. Scene teardown expires
that capability. Fixed capacity is prepared during mission setup; frame updates
allocate nothing and never replace unrelated lights or their selection history.
This adapts the existing synchronous renderer, not a second light registry owned
by a controller.

Ownership reference inspected: Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Renderer/Private/RendererScene.cpp` (`AddLight`, `BatchAddLights`,
`UpdateLightTransform`, `UpdateLightColorAndBrightness`, `RemoveLight`) and
`LightSceneInfo.h`. Components publish transform/color changes to scene-owned
light information and retire their registration; they do not own scene-wide
selection. We retain that boundary with copied source values and generation
checked fixed slots. Unreal's asynchronous render command queue and lighting
model are not transplanted into this synchronous source reconstruction.

The exact consumer distinction is retained: `5f70f0` and `5f6e80` scale the
stored diffuse by intensity before `546510`, while specular remains the captured
value. Both are initially multiplied by the independent stage light RGB.
`45ae00 -> 45b080` resolves `witch/light/<stage>`; its `629900` callback is the
ordinary fixup-list traversal. The complete AREA updater `4829d0` updates stage,
general and postprocessing fields, not this separate light record. The typed
stage snapshot therefore retains this constructor multiplier independently.

Fading exposes another original edge: influence-radius `pow` can produce NaN
when diffuse falls below attenuation constant times selection threshold. The
original `40b2a0` comparison admits an unordered sphere radius, while ordinary
noncoincident model-distance admission fails its `< radius` test. This internal
selection value is preserved; it is not rejected as malformed input, clamped
to a fabricated radius, or published to the GPU. Input colors remain finite.

### Weapon-presentation verification

Focused owner tests cover retained muzzle effects, source-duration light decay,
expiry, explicit retirement and stale emitter capabilities. The original x86
`5f6d80` witness agrees on successive decay frames, expiry and the persistent
nonpositive-duration branch. Scoped fatal lint passed for these files. The
runtime/resource checks passed after the integration changes available at that
point. These checks establish the copied scalar/lifetime contracts; they do
not claim visual acceptance of the complete encounter.

### Reopened attachment and per-projectile producer repair

Source `615aa0` constructs the retained muzzle child with visual `+10`;
`615fc0` parents retained flash19 and its light to that child, and `618410`
uses the same child for shot audio. Separately, each `616530` projectile emits
visual `+8` effect42 through `617110`, at visual `+14` locator1. Both WA0 and
WG1 activate this detached effect. The initial implementation conflated these
placements and omitted the detached emission. Retained flash/light/audio now
use barrel locator0; the per-projectile effect uses locator1 and natural
effect-world lifetime. Completed-pose follow updates only the retained child.
No speculative offsets or replacement effects are introduced. Native witness
remains required after this repair. Both header and implementation phases
consult Core I.4 and performance MEM.9 for copied named frames and fixed pools.
