# World renderer foundation: source and device contracts

October 23, 2026. This record supports the
[renderer drill-in](../../../docs/architecture/world-renderer-reconstruction.md).
CPU decoding, native geometry upload, executable shader equations and a faithful
textured level are distinct milestones.

## Established implementation inspected before design

Inspected Unreal's `FMetalRHICommandContext::RHIDrawIndexedPrimitive` in
`Engine/Source/Runtime/Apple/MetalRHI/Private/MetalCommands.cpp`, pinned commit
[`f45729c0b6f097038e72b52d366a62baeca3275b`](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Apple/MetalRHI/Private/MetalCommands.cpp).
It prepares state before drawing, derives index count from topology, converts
start index into a byte offset using index width, and forwards signed base vertex
to Metal. The reconstruction adopts these API translation and range obligations;
it does not copy Unreal's pipeline cache, bindless machinery or instancing policy.
Native buffer allocation extends the already inspected temporary-allocation and
completion-before-retirement contracts in [device memory](device-memory-ownership.md).
Standalone retained buffers are appropriate for this first immutable geometry
owner; asynchronous retirement and pooling remain explicit later changes.

The expanded material phase also inspected `FMetalBlendState` in
`Engine/Source/Runtime/Apple/MetalRHI/Private/MetalState.cpp` at the same pinned
revision. It derives immutable attachment descriptors from separate RGB/alpha
blend factors and operations. We retain two native pipeline objects for the
recovered opaque/source-alpha policies; the complete descriptor determines
behavior. This is setup work, not repeated pipeline creation per frame. Our
source-alpha factors come from Bullet Witch's state table, not Unreal presets.

Design and implementation separately consulted full Core Guidelines E.6
(`#re-raii`) and performance GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`): resource construction rolls back
through owners, and unchanged geometry remains device-resident between draws.

## Newly recovered original material execution

Reproducible Ghidra exports use the existing hash-checked analysis project:

```sh
python3 research/tools/engine_slice.py pass-08-world-materials \
  0042f970 00547370 0042ffc0 00430a10 00430b10
python3 research/tools/engine_slice.py pass-08-world-shaders \
  0042ecf0 0042fbb0 00546dc0 00547120 005471e0
```

Generated outputs remain ignored in `research/evidence/engine/`. These are
static consumer findings, not original-game captures.

- `0x4308b0` sends the two `knsS` words to `0x42f970` and binds its selected
  vertex/pixel pair. That selector combines the file flags with runtime flags:
  `0x200`, `0x200000`, `0x400000`, and fog-mode `0x800000`/`0x1000000`.
  Its owner also chooses the S/T pair and may disable the pixel shader. Original
  shader names cannot be selected solely from the material's raw flags.
- `0x547370` resolves `CF1I` semantics into register identities: PARF→pixel2,
  CCOL→pixel3, FRSA/FRSB/FRSR/FRSL/FRSM→pixel4–8, LUMI→pixel 16,
  AMBI→pixel 17, REFP→pixel 18, ENVP→pixel 19, SPCC→pixel20;
  TOF0/TOF1/TOFB/TOFI→vertex4–7. `0x546dc0` applies per-instance multiplier
  and bias before uploading these values. Texture semantic mapping sends BTX0
  to sampler0, BTX1→1, BLDT→2, NMAP→3, BMAP→4, AMOC→5,
  LUMX→6 and LENV→7; shape resources have separate slots.
- `SSSM`, executed by `0x547120`, sets sampler MAXMIPLEVEL to zero for the
  resolved entries. It is not permission to invent filtering/address modes.
- `0x4307d0` enters DGRP only when the corresponding group-mask bit is enabled.
  `0x430850` enters DVOL when its visibility query accepts it, or when no
  visibility object is attached. Rendering every group is a development
  selection, not recovered visibility acceptance.
- `0x42ecf0` constructs shader feature macros and compiles `knsS.vs.fx`,
  `knsS.ps.fx`, `knsT.vs.fx`, `knsT.ps.fx`, `knsT1.ps.fx` when caches do
  not resolve. These source filenames establish distinct passes; they do not
  establish that the source files are shipped.

The first main-stage material carries flags `0x00420882`, variant low bytes
`1,0`. Registry identities `00820882100`, `00c20882100` and `00e20882100`
share S vertex2/pixel0, but their T pixel programs are respectively81/186/221.
The T vertex program is456. T programs contain normal mapping, light/shadow and
fog inputs; substituting a simple base-texture shader is not faithful translation.

## Original static depth shader subset

Registry vertex2 has13 instructions; pixel0 has8. After decoding the SM3 tokens,
the vertex equation is `p = float4(position.xyz,1) * positionScale +
positionOffset`, then four row-dot-products with `localScreenMatrix`. TEXCOORD1
carries the resulting clip z,w. Pixel output is replicated
`z/w + depthBias.x + depthBias.y * max(abs(ddx(z/w)),abs(ddy(z/w)))`.
Constant registers are vertex0/1 and12–15, pixel68. Identity and hashes are in
the existing shader registry manifest. Pixel output is a color-encoded biased
depth value; it does not replace hardware depth with that bias.

These equations permit native depth-path verification with explicit constants.
Using that output as a visible inspection view remains diagnostic and does not
satisfy the textured-world acceptance milestone. Full material constants,
environment, pass targets, sampler state, culling and actual visibility must
be established before claiming the stage's original color rendering.

## Static textured surface subset

The shared `world_material_requirements` classifies exact source-traced effective
selectors and exposes semantic input requirements to game composition and every
native backend. Unknown variants reject; a missing registry variant is never
replaced by the nearest shader. The standalone surface shader uses the recovered
family equations for geometric or tangent-space normals, one or two base
textures, vertex illumination/color, directional diffuse/specular, reflection,
Fresnel, environment cubemaps and exponential/height fog. Packed declarations
remain original: stride24 sky, stride28 geometric normals, stride36 normal maps,
and stride40 layered UVs or vertex-color modulation.

The ignored full census is `research/evidence/reconstruction/stage/full-materials.jsonl`.
The hash-bound read-only `world-renderer/dump_shader.py` evidence helper decodes
cached SM3 instructions and register tables from the verified executable. It is
research tooling, not a runtime shader translator. Exact registry comparisons
establish the following original no-shadow family coverage, including the
renderer-global SH feature corrected in the final static-view pass:

| Surface behavior | Vertex / pixel program provenance | Main-stage draws |
|---|---|---:|
| Geometric normal, Fresnel reflection | 454 / 222 | 402 |
| Normal map, Fresnel reflection | 456 / 223 | 541 |
| Normal map, no reflection | 456 / 304 | 55 |
| Normal map, reflection without Fresnel | 456 / 214 | 55 |
| Geometric normal, no reflection | 454 / 303 | 44 |
| Vertex illumination without directional/reflection | 454 / 357 or 456 / 368 | 47 |
| Layered bases, normal map, no reflection | 463 / 305 | 16 |
| Geometric normal, reflection without Fresnel | 454 / 213 | 15 |
| Normal map and cubemap reflection | 456 / 228 | 10 |
| Normal map, cubemap and vertex modulation | 458 / 229 | 3 |
| Layered bases, normal map and reflection | 463 / 215 | 1 |

The no-shadow families cover 1,189 of 1,228 main-model draws. Sky uses its
separate original 448/363 family, runtime spherical-harmonic feature, and packed HDR base texture. The remaining
39 draws have no exact selected no-shadow variant. Direct inspection of their
actual cached programs establishes a separate bounded contribution policy:

- Thirty-seven draws use layered/Fresnel pixel 90 (selector `00820a82201`).
  Shadow depth and jitter samplers 12/13 affect only directional diffuse/specular;
  a separate local-light loop consumes registers 44–65. With authored directional
  and local-light contributions explicitly omitted, the surviving equations are
  the actual layered base, material/indirect modulation, vertex/ambient/constant
  illumination and fog. No invented no-shadow selector is compiled.
- Two draws use pixel 319 (`04a00a82100`). It has **no shadow texture**: its
  additional contribution is a local-light list, followed by original material,
  reflection and fog equations. Omitting that list leaves the same recovered
  material/vertex illumination terms and zero reflected light contribution.

`WorldLightingPolicy::without_direct_lighting` is accepted only for these
source-traced identities. Submission rejects directional energy under this
policy. Full shadow evaluation, local-light loops and original pass selection
remain unresolved. This closes material-family support for all 1,228 main draws
under a declared inspection contribution policy; it does not establish original
lighting parity or guarantee coherent native presentation. Cached program source
and instruction hashes remain in ignored evidence. Original cache/compile
functions 42fd60/42fcf0/42f4a0 provide no bit-mask fallback.

Named `WorldColorParameters` map to vertex registers 0/1 (position), 4/5 (texture
offsets), 12–15 (clip rows), 16–18 (world rows); pixel 0/1 (primary/secondary indirect
color), 3 (material color), 6 (FRSR Fresnel reflection), 9–15 (packed SH),
16/17 (illumination/ambient),
18 (reflection),19 (environment power),20 (specular),23–25 (directional light),
39–42 (fog). Base/secondary/normal/environment textures resolve through original
runtime slots 0/1/3/7. Cube data preserves six original face slices and packed
RGBA16Float channel bytes; no color conversion or invented mip chain is applied.

Inspection selects ordinary no-shadow families by OR-ing 0x00e00000 into the
authored flags, then includes the original runtime SH feature 0x200. The two
restricted families above keep their actual cached variants and explicitly omit
direct lighting. Original material constants, stage directional light and stage
ambient SH are supplied; the sky owns its separate white SH. This does not
reconstruct original shadow/visibility pass selection. The initial camera and
depth follow recovered source; all-group selection remains inspection policy.
[Renderer state](world-renderer-state.md) separates defaults from authored state.

Coverage and blending are independent of shader-family equality. Low selector
bit 1 shares color programs but chooses original depth pixel 1: it samples base
alpha at **raw UV**, multiplies material alpha, and discards below 0.5. Inspection
merges that recovered coverage into its direct color pass; this does not claim
the original depth-prepass graph. Original `BTNO` state resolves through 547370,
5471e0, table 0x969108 and545db0 to SRCALPHA/INVSRCALPHA ADD with separate-alpha
disabled. Both RGB and alpha therefore use those factors. Main chunks 0/1 contain
444 / 287 draws with no authored blend override; chunk 3 contains 497 source-alpha
draws. Preserve this phase order rather than allowing accidental state leakage.
`SSSM LENV` resets sampler 7 MAXMIPLEVEL 0; original baseline makes this environment
sampler clamp/linear and base/normal samplers wrap/linear.

Numerical translation must respect instruction semantics, not just algebra.
Microsoft's [rsq specification](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/rsq---ps)
takes the absolute value before reciprocal square root, so tangent-plane samples
outside the unit disk use `sqrt(abs(1-x*x-y*y))` after the following reciprocal.
[POW](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/pow---ps)
also takes the absolute value of its base; Fresnel preserves that behavior.
[NRM](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/nrm---ps)
maps a zero vector to zero through its finite multiplier rule; the native helper
handles zero explicitly. These corrections do not establish bit-identical
transcendentals, fused-operation rounding, all degenerate inputs, or texture
filter precision across D3D9/Metal. Those remain comparison work.

## Device ownership and executed checks

`WorldOutput` has one portable header. The Mac implementation retains immutable
vertex/index buffers, original RGBA8 or RGBA16Float textures, ordered selected
draws and native sampler/depth states. Domain allocators own CPU metadata;
`DeviceBudget` charges logical and actual native allocation sizes for payloads.
Upload and submission replacement are transactional. Texture tables can retain
explicit absent slots: whole-table readiness stays false, and each selected draw
must independently resolve its required family textures. Releasing a
bound resource invalidates submissions before retiring the native owner.

The existing host encodes world color/depth first, then opens its existing
movie/UI overlay pass with color load and no depth attachment. Command completion
precedes resource mutation/release. This is the existing conservative synchronous
retirement model, not a recovered render-worker schedule. Unchanged geometry is
not copied each frame; setup creates material sampler/depth objects. Texture
mip count is currently one, matching the recovered initial mip-policy flag;
full generated mip chains and override policy remain unimplemented.

The existing native scenario inside `memory-tests` executes authored fixtures
without assets or a window. It verifies nonzero start-index and signed-base-vertex
addressing through original depth output 0.375, unknown-material rejection,
partial-upload rollback, explicit omitted-light policy and forbidden-energy rejection,
exact geometric/normal-base output, layered-base alpha
interpolation, packed-HDR sky values, environment-cube contribution, source-alpha
RGB/alpha output and original cutout coverage. Fog endpoints and normal samples
outside the tangent disk produce checked finite output. Separate native slice
readback verifies six-face upload order. Resource cases check partial readiness,
texture-release invalidation, half-float/six-face accounting and zero final
charges. These are specific native contracts, not original-game image parity or
proof of coherent actual-stage presentation.

The table above records the initially traced no-SH variants. Runtime selection
also enables packed spherical-harmonic irradiance. The corresponding original
programs and additive combination with vertex illumination are now recorded in
[the output fidelity audit](world-output-fidelity-audit.md#runtime-irradiance-selection-resolved-black-sky).
Sky uses the original white field; ordinary objects use authored scene ambient
RGB. Both remain independent of material ambient and direct light parameters.
