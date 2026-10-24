# Lighting ownership and reference boundaries

The lighting subsystem belongs to the engine scene. Game code publishes authored
light definitions, receiver policy, model bounds and final transforms. It does
not select probe cells, pack shader coefficients or rank lights in a player
controller. The recovered numerical contracts remain the first specification.
This ownership correction alone does not resolve the reported scene brightness.

## Original behavior and engine boundary

The [source producer trace](lighting-scene-producer-contract.md) establishes:

- World setup owns character/enemy fields and registered light emitters. Stage
  activation supplies separate live field multipliers.
- Static ordinary geometry receives stage ambient as uniform spherical harmonics;
  sky receives white. These are explicit source policies, not defaults for an
  unclassified actor. The static callback also publishes a shared direct-light packet selected
  from view-visible light spheres and ranked at the camera; it is distinct from
  each spatial receiver's distance/radius admission. Only materials with the
  corresponding direct-light feature consume that packet.
- Each spatial receiver transforms its authored group-zero center with its
  submitted model-world transform. Local-light selection uses that world center
  and the unscaled authored radius. Body and attached weapon are separate
  receivers. The coefficients and surface normals use world axes.
- Sampling, selection and packing precede draw publication. Render consumption
  receives prepared values; it does not call into a mutable gameplay object.

`bw::scene::SceneLighting` consolidates these contracts. It owns decoded
`IrradianceField` instances registered in a fixed source-array order, copied
local lights, selection settings and stage parameters. Game registration maps
the original character/enemy resources to scene-local `LightingFieldId` values;
the engine does not know actor allegiance or game resource names. Each field
retains its independently published intensity. `LightingReceiver` supplies one
complete observation. `evaluate`
returns owned fixed-size coefficients and direct lights. Missing selected fields
reject explicitly; no stage-ambient substitution stands in for a missing actor
field. The current owner is serialized because local-light ranking reuses scratch.
A native backend converts the prepared packet to its device layout.

The owner also retains the authored directional-light definition and publishes
`DirectionalLighting` separately from receiver packets. The original command
`0x460660` adds `0.001` to the authored vector's Z component, normalizes and
negates it; its diffuse/specular components retain their authored values
([environment decoding](stage-content-decoding.md)). Parameter publication
prepares this direction once and validates the complete candidate before changing
either shared lighting or ambient state. Game code supplies the definition;
render extraction consumes the prepared shared packet. Fog and shadow-projection
geometry remain separate concerns. This moves existing recovered arithmetic into
its engine owner without introducing a brightness adjustment or new light model.

Fields and emitters are copied/decoded at scene preparation. Explicit emitter
replacement constructs a valid candidate before changing the live owner;
parameter publication validates the complete replacement first. `set_view`
prepares the source global light packet and narrower active-emitter list once
for a view; static and spatial queries require that publication, while sky has
no direct-light list. Per-model ranking consumes the prepared active list, not
every registered scene light. Replacing emitters also
recomputes the current view packet before publication, so removed lights cannot
survive in static draws. Successful
queries allocate nothing. Returned packets contain no references to scene,
actor or asset storage. The caller may reuse a packet across that model's draws.
This API does not claim a persistent entity registry or asynchronous renderer;
receiver membership and packet retention remain with scene extraction. Moving
registration to a future render scene must preserve this same complete-observation
contract rather than introducing a second player-specific lighting path.

## Pinned Unreal source

Inspected authorized source: EpicGames/UnrealEngine commit
`f45729c0b6f097038e72b52d366a62baeca3275b`, the existing ignored reference checkout.
The sources below require Epic repository access. Selected snapshots and hashes
are retained under ignored `research/evidence/reconstruction/character-light/unreal-reference/`.
No Unreal implementation text is incorporated into project production code.

The applicable reference is precomputed lighting and mesh-pass preparation.
Replacing legacy materials with Lumen, PBR or Unreal's complete renderer would
not establish fidelity to the authored Bullet Witch data.

| Boundary | Inspected implementation | Applicable contract |
|---|---|---|
| Scene ownership | `FScene` lights, precomputed volumes and indirect-light cache; `AddPrecomputedLightVolume`, `RemovePrecomputedLightVolume`, `BatchAddLights` | Renderer-side scene membership and explicit publication; game components publish proxies/transforms |
| Authored volume lifetime | `FPrecomputedLightVolume::AddToScene`, `SetData`, `RemoveFromScene` | Level data is retained while registered; scene installation and removal occur through render commands |
| Receiver observations | `FIndirectLightingCache::UpdateCachePrimitive` | Stable primitive identity/bounds and lighting eligibility drive cache ownership; controller code does not evaluate lighting |
| Publication | `FinalizeCacheUpdates`, `FinalizeUpdateInternal_RenderThread`, `FPrimitiveSceneInfo::UpdateIndirectLightingCacheBuffer` | Complete CPU preparation before render-thread publication; primitive shader data is separate from mutable gameplay state |
| Pass admission | `FBasePassMeshProcessor::ShouldDraw`, `TryAddMeshBatch`, `GetUniformLightMapPolicyType`, `Process` | Decide pass eligibility, lighting policy and shader variant before emitting the draw |
| Pass resources | `SetupSharedBasePassParameters`, `CreateOpaqueBasePassUniformBuffer` | View/pass light, fog, reflection, AO/depth and exposure resources are published separately from per-primitive coefficients |
| HDR presentation | `FViewInfo::UpdatePreExposure`, `FEyeAdaptationManager`, base-pass output and final tonemap | Distinguish raw scene light, scene-storage scaling, current exposure and output transfer |

Sources: [scene declarations][scene], [scene publication][scene-impl],
[volume lifecycle/interpolation][volume], [cache preparation][cache],
[primitive uniform publication][primitive], [uniform layout/packing][uniform],
[pass processor][basepass], [base-pass shader][basepixel],
[eye adaptation][exposure], [tonemap][tonemap].

Unreal's asynchronous cache waits for its preparation task before render-thread
finalization; it also guards shared dirty lists. This is evidence for a publication
boundary, not permission to assume the current serial Bullet Witch owner is
thread-safe. Its volume memberships borrow level-owned data, so removal and
retirement remain necessary. SceneLighting instead owns decoded data outright;
no deferred consumer borrows that data through the returned packet.

## Pass scheduling fallback if the original remains opaque

The four original model-chunk ordinals must not be equated to four Unreal passes
without evidence. Unreal supplies a useful decomposition, not that mapping:

1. Preserve each source draw's chunk/group identity, authored state and material
   variant. Resolve visibility and pass eligibility before submission.
2. Resolve the indirect-light policy from explicit receiver/material capability
   and available scene data. A global SH switch is not a replacement for this
   decision. The absence of a light field cannot silently select a brighter or
   darker material interpretation.
3. Select the shader variant from that resolved policy and original material
   definition. Bind per-instance coefficients separately from shared view/pass
   resources; material textures and constants retain their source meanings.
4. Resolve depth, color-write, cull and blend state for the admitted draw from
   its pass and source overrides. Seal all required inputs in the command.
5. Execute pass dependencies and ordering explicitly. Preserve known source
   order; do not adopt Unreal's opaque sorting, deferred layout or translucency
   fallback merely because those mechanisms exist.

Unreal's `TryAddMeshBatch` checks blend/pass eligibility, main-pass relevance and
material domain. `GetUniformLightMapPolicyType` separately considers lit/unlit
materials, available scene lighting and primitive eligibility. `Process` selects
the shaders, derives draw state from pass state and material overrides, then
builds commands with their lightmap element data. This is the concrete reference
for an unresolved scheduling boundary. It cannot identify which Bullet Witch
chunk should be admitted; that remains an original-source/data question.

If a specific producer or ordering decision remains opaque after bounded source
tracing, record its inputs and the chosen reference-derived rule beside the
implementation. Do not claim that a correct shader replay with reconstructed
inputs proves the producer or pass schedule. Full-frame comparison must include
static scene geometry, sky and character, not only the costume fragment.

## SH units and coordinate conventions

Unreal's `FPrecomputedLightVolume::InterpolateIncidentRadiancePoint` accumulates
incident-radiance SH from nearby samples with a radius-weighted kernel. The cache
normalizes by accumulated weight. This is not Bullet Witch's ordered oriented-grid
sampler; replacing the latter would change verified authored behavior.

For Unreal's three-band point cache, `UpdateBlock` packs coefficients divided by
π. `CalcDiffuseTransferSH3(N, 1)` applies the cosine-convolution factors π, 2π/3
and π/4 in the shader. Their combination therefore applies band factors
**1, 2/3 and 1/4** to the SH basis before multiplying the material diffuse color.
The volume-texture branch places its division by π in the shader instead.
Mixing these two packing conventions would introduce a duplicate factor.
Sources: [cache packing][cache], [SH basis/convolution][sh],
[consumer branches][basepixel].

Bullet Witch's recovered `pack_irradiance` already has these ratios: constant
0.2820948, first-band magnitude 0.325735, second-band products 0.273137 and
0.136569. The quadratic-Z constant/product terms are likewise the basis scaled
by 1/4. This comparison independently explains the algebra; it does not prove
all original live coefficient inputs. **Adding another division by π is not a
justified brightness correction.** Nor should Unreal's cache ringing reduction
be copied: that path adds ambient to reduce negative lobes and explicitly notes
its energy/hue limitation, so it would change the source appearance.

The normal must be expressed in the same axes as the coefficients. Preserve
Bullet Witch's established world-normal and TBN contracts. Unreal's normalized
material-normal expectations are not evidence to renormalize a legacy shader
whose exact source arithmetic does otherwise.

## HDR ownership and verification

Unreal base-pass output applies `View.PreExposure`; its tonemap removes that
storage factor through `OneOverPreExposure` and then applies current exposure.
The view-state eye-adaptation manager initializes buffers, retains multiple
frames and reads only completed GPU readbacks. This separation can guide an
opaque lifecycle/publication boundary; it is not a reason to add pre-exposure
to the already recovered legacy HDR path. The original final display equation
and its gamma remain the specification.

Verify the first differing boundary, not an appearance-fitted scale:

- Constant and single-band synthetic SH establish basis/order and exactly one
  diffuse-convolution normalization. They do not calibrate legacy art.
- Separate scene owners and consecutive receiver queries must retain independent
  fields/intensities/results; transformed bounds and unscaled radius must affect
  the actual selected packet. Failed replacement must retain the prior scene.
- Native captures must record admitted pass/chunk, effective shader, resource
  identities, constants, model/normal transforms, light packet, raw HDR value,
  luminance input and final display settings. Test an actual executed branch.
- Original same-scene imagery determines whether the visible complaint remains;
  it cannot alone identify a missing producer. Do not modify global exposure,
  per-costume tint, AO or normal amplitude to fit that image.

## Narrow reference for an opaque metering producer

The pinned Basic Exposure implementation establishes a concrete alternative for
metering coverage/publication when the original GPU producer remains opaque:

- `PostProcessing.cpp` constructs a six-stage scene downsample chain from the
  selected half/quarter/eighth-resolution scene-color input. Basic exposure reads
  the last stage. It does not meter an arbitrary independently cleared target.
- `FTextureDownsampleChain::Init` first downsamples, then invokes
  `AddBasicEyeAdaptationSetupPass` once when log-luminance-in-alpha is requested.
  That setup preserves RGB and writes scaled/biased log2 luminance into alpha in
  **PF_FloatRGBA**. It removes `View.PreExposure` before measuring luminance.
  Later downsample stages retain that alpha channel.
- `DownsampleCommon` derives its filter offsets from **input texel extent**, not
  output texel extent. The high-quality branch averages four bilinear samples
  offset by ±one input texel. Sampling clamps to the valid input viewport. This
  is a four-filtered-sample blur, not an exact discrete box average for every
  odd-size source, so its limitations should not be hidden.
- `BasicEyeAdaptationCS` partitions the final mip into disjoint rectangles for
  one 16×16 thread group. Each thread uses integer texture loads to accumulate
  its rectangle's weighted log sum and weight. A synchronized shared-memory
  reduction combines all 256 partial sums, divides once, undoes scale/bias and
  applies exp2. This avoids a final 4→1 reduction that samples only clamped
  corners through destination-sized offsets.
- `ComputeWeightedTextureAverageAlphaSubRegion` applies a metering texture and
  optional black-bucket influence. `GetEyeAdaptationParameters` supplies a white
  texture when no mask is authored. A new legacy fallback must name whether it
  adopts uniform weights or a recovered mask; do not copy black rejection,
  middle-grey targets, photographic exposure compensation or clamps as tuning.
- `AddBasicEyeAdaptationPass` receives the current metering texture/viewport and
  **previous** exposure buffer separately. Only its owning view updates/swaps
  the multi-frame result; other views read it. Readback is queued after the
  dispatch, and only completed results feed the CPU observation.

Sources: [postprocess orchestration][postprocess], [downsample graph][downsample],
[downsample shader][downsample-shader], [basic metering shader][meter-shader],
[exposure setup/publication][exposure]. The source snapshots pin all these files.

A bounded fallback may adopt this weighted-sum/weight reduction and explicit
input-view/result ownership while retaining Bullet Witch's recovered luminance
weights, log-domain convention, adaptation timing, authored exposure key and
final gamma. That is a reference-derived replacement of an **unresolved metering
producer**, not an original-parity claim. It does not justify a new exposure
multiplier or changing material light. If the original reduction resource,
coordinates and scheduling become established, compare that behavior before
replacing it. Any difference must remain explicit.

Useful numerical acceptance is a constant input, a bright interior patch with
dark corners, non-square and odd dimensions, and a black/bright transition with
known frame ownership. Compare measured log sum and weight to an independent CPU
coverage oracle, including filter and viewport policy. A constant-only test
cannot detect corner-biased sampling. For the actual scene, retain raw HDR,
measured luminance and adapted exposure alongside the final image. Improvement
of that image still needs original-footage comparison and user validation.

This study establishes a reusable engine ownership model and a concrete pass
preparation reference. It does not establish a numerical cause for the current
brightness failure. C++ design/implementation queries read Core I.1/R.20 and
performance MEM.9; preparation/ownership and allocation-free evaluation follow
those contracts.

[scene]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/ScenePrivate.h
[scene-impl]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/RendererScene.cpp
[volume]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Engine/Private/PrecomputedLightVolume.cpp
[cache]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/IndirectLightingCache.cpp
[primitive]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/PrimitiveSceneInfo.cpp
[uniform]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/LightMapRendering.cpp
[basepass]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/BasePassRendering.cpp
[basepixel]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Shaders/Private/BasePassPixelShader.usf
[sh]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Shaders/Private/SHCommon.ush
[exposure]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessEyeAdaptation.cpp
[tonemap]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Shaders/Private/PostProcessTonemap.usf

[postprocess]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessing.cpp
[downsample]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Renderer/Private/PostProcess/PostProcessDownsample.cpp
[downsample-shader]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Shaders/Private/PostProcessDownsample.usf
[meter-shader]: https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Shaders/Private/PostProcessEyeAdaptation.usf
