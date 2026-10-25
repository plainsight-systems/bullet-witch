# Optional texture mip generation

L25 is a **launch-setting-controlled texture construction path**, not an authored
asset flag, per-frame lighting pass or hidden material feature. The reconstructed
single-level resource matches the original default and the currently inspected
Steam configuration. The graphics-side enabled setting is now implemented through launch configuration,
source staging conversion, portable filtering and native full-chain residency.
Verification and remaining source/device limits are distinguished below.

## Activation and ownership

The 36-byte `SystemSetting` layout begins at original `a0b94c`. Its word at byte
8 (`a0b954`) uses mask `0x08` for this option. Dialog `410750` reads checkbox
`0x3f7`, writes that bit, saves the block, and maps it to runtime flags
`a0b6d8 & 0x80`. `411050` initializes the same checkbox from saved bit 3.
Both image constructors compute the D3D creation level count as
`~(runtime_flags >> 7) & 1`:

| Saved mip setting | Requested levels | Construction |
| --- | --- | --- |
| Clear | 1 | Decode and upload only level zero |
| Set | 0 | Allocate complete chain, upload zero, fill all returned lower levels |

`4102d0` installs default word `0x2f1e52`, whose mip bit is clear. A fresh
read-only check of the local installed 36-byte settings file found `0x6f1e52`;
its mip bit is also clear. This confirms the current installation, not every
Steam user's configuration. The launch settings are distinct from scene
customization and from model sampler `mip_filter` / `MAXMIPLEVEL` state.

`41b3c0` registers `41a6f0` for the resource tag `DDS ` and `41a420` for `DDSC`.
These resources contain the recovered PNG-based images, not an authored DDS mip
chain. Their callbacks inspect global runtime flags when constructing resident
textures. There is no per-record mip flag in this branch. Changing the setting
therefore belongs before resource preparation (or requires replacement of
resident resources), not in per-frame sampler updates.

## Source creation and filtering requests

The two constructors use default-pool D3D textures with usage zero, plus a
system-memory source surface. This is explicit source-surface conversion, not
D3D automatic mipmap generation. They first load/copy the decoded image into
level zero. For every lower level returned by `GetLevelCount`, they call
`D3DXLoadSurfaceFromSurface` with full source/destination rectangles, filter
`0x80005`, and color key zero. **Each lower level reads the same full-resolution
source surface**, rather than the preceding level. Cubes repeat the operation
independently for each of six source faces. No cross-face filtering is requested.

Microsoft's [texture creation contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createtexture)
and [cube creation contract](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-createcubetexture)
define zero levels as a complete chain down to 1×1; dimensions halve with integer
truncation and clamp to one. The source uses the returned count, so device
creation support remains a separate prerequisite.

The [D3DX filter flags](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dx-filter)
identify `0x80005` as BOX plus ordered 4×4 dithering. No sRGB-in/out, mirror,
diffusion or color-key operation is requested. The documented BOX case covers
half-sized destinations, while this game also requests quarter-sized and smaller
destinations directly from the full-size surface. The exact D3DX reduction,
rounding and dithering results for that case are not established by the call
trace. [Surface conversion](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxloadsurfacefromsurface)
is the dependency to witness, including its success/failure behavior; neither
recursive Metal mip generation nor a guessed averaging loop establishes parity.

| Resource path | Source/native format rule | Consequence for enabled generation |
| --- | --- | --- |
| 2D, metadata `+20 & 0x3f == 0x1d` | Packed PNG bytes reconstructed as A16B16G16R16F | Filter the reconstructed half-float texels, never the byte-container PNG colors |
| 2D, low format `0x0f` | A4R4G4B4 staging and resident texture | Initial quantization and lower-level dither/quantization both matter |
| Other 2D formats | A8R8G8B8 staging and resident texture | Preserve the source unorm format and alpha through filtering |
| Cube, low format `0x1d` | Six A16B16G16R16F faces | Same half-float interpretation per face |
| Other cube formats | Six A8R8G8B8 faces | Unlike 2D, cube constructor does not select A4R4G4B4 for `0x0f` |

Ordinary 2D extents come from the PNG header and round odd dimensions up to even.
Half-float 2D extents come from packed metadata; the PNG is twice its logical
width. Cube edge length comes from metadata and also rounds up to even. Those
initial-surface conversion rules precede mip creation. Mip generation alone
does not resolve base conversion: the audit-time gaps in odd dimensions and
2D A4R4G4B4 decoding are now implemented by the shared decoder described below. This path contains no volume/array/compressed
texture creation, even though D3DX as a library supports additional formats.

## Audit-time reconstruction and reachability

`formats::decode_world_texture` retains one 2D image or six base faces in
RGBA8/RGBA16Float and has no mip policy input or mip offsets. Its HALF4 handling
is already separated from color conversion. `WorldResources::upload_world_texture`
accepts that single-base image. Native `TextureAllocation::create` hardcodes
`mipmapped:NO`; accounting includes base dimensions and face count only, and
`upload` writes level zero. The launch host currently exposes no corresponding
original saved mip setting. Sampler linear-mip configuration cannot create the
missing resource levels.

The represented stage already reaches both constructors' content classes:
previous actual decoder census admitted 176 two-dimensional stage textures and
one cube, including the 2048×512 half-float sky and 64×64 half-float cube. Thus
**current content would reach the optional branch if the original setting were
enabled**. This is not an asset-upgrade feature or merely hypothetical future
content. It is inactive for the inspected default configuration. This task did
not rerun that census or change the running corpus.

A complete implementation needs a single launch/settings authority to publish
texture construction policy before preparation; decoded/generation results with
explicit per-level/face layout; backend allocation/upload and device accounting
for those levels; and transactional failure before replacing residents. Sampler
state remains independent. The reusable renderer should consume the resolved
policy/data rather than reading a game's settings file. The numerical filter
still needs its original D3DX witness or an explicitly documented target
inference. Acceptance must distinguish faithful disabled-mode behavior from
unimplemented enabled-mode support; no placeholder interface makes the latter
complete.

## Pinned established implementation reference

Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b` was inspected directly:

- `Engine/Source/Runtime/RenderCore/Private/GenerateMips.cpp`,
  `FGenerateMips::ExecuteRaster/ExecuteCompute`: explicit level count, per-face
  views for cubes, per-level destination extents and render/compute ownership.
- `Engine/Shaders/Private/ComputeGenerateMips.usf`, `MainCS/MainPS`: sampler-based
  source read and explicit output; compute has a separate sRGB permutation.
- The already inspected Metal texture reference in
  [texture sampling](texture-sampling.md) supplies explicit `NumMips` allocation
  and complete resident-level memory accounting.

These are useful engine/native boundaries for implementing the missing policy.
Unreal's loops read level `n-1` when producing `n`, and its shown shader does not
apply the source D3DX ordered-dither request. Consequently, adopting that
algorithm unchanged would be an inference with a known source-graph difference,
not a faithful reconstruction of these calls. It does not resolve D3DX's
quarter-size box behavior or format-specific quantization. No Unreal source was
copied and no production code changed during this investigation.

Evidence: existing ignored passes 02b, 06l, 06n, 06o and 06q; executable
SHA-256 `bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
That audit established the activation/ownership boundary. The implementation
and direct original numerical verification below supersede its earlier open
filtering questions; native allocation evidence remains distinct from scene
visual parity.

## Implemented ownership and construction

The user authorized completion of this graphics path before independent review.
The launch configuration publishes an explicit `--texture-mips on|off` choice,
with `off` preserving the original default. Mission resource preparation captures
that value once in its owned immutable options. The native host captures the
same launch value for DDS sprite resources; neither scene controllers nor
native render callbacks borrow a mutable settings object. All stage, sky, actor,
weapon and costume preparation consumes the same captured policy. Title, menu,
loading and font DDS images use the shared source decoder, retaining format
metadata. Their typed resource upload consumes that policy too; raw controller
icon PNG uploads remain base-only, and movie frames keep their separate path.
The source graphics option is represented by the launch switch; this does not
add the original Windows settings dialog or modify purchased game files.

The portable pixel owner describes an explicit mip count and a contiguous
level-major, then face-major byte layout. Generation constructs a candidate from
an immutable decoded base; every lower level reads that base independently.
Generation failure leaves the input unchanged and publishes no new native handle.
Costume/weapon candidates preserve their prior owners; failed initial mission
admission releases partial residents through its existing failure path.
Decoded/generation storage uses the injected resource memory domain, and no
filtering or configuration lookup occurs in frame submission.

The native move-only texture owner consumes the prepared level count and bytes.
It validates dimensions, maximum level count and complete payload before upload,
accounts the sum of every level and face, reserves estimated device storage,
then reconciles actual native allocation before publishing a handle. Backend
allocation performs no filtering. Handle publication occurs only after every
level has uploaded. Candidate failure releases both allocation and charge;
existing costume/weapon candidate retirement rules preserve current residents. Native
resource borrowers remain scoped to their owner and GPU completion obligation.

This implementation follows the already inspected pinned Unreal allocation/level ownership
boundaries without adopting its recursive filtering equations. Numerical filter
reconstruction and source uncertainty are recorded separately in
`texture-mip-filter.md`. Core E.6 and I.11 and performance COPY.5 were retrieved
for design: RAII candidates, no borrowed ownership transfer, move-only native
resources. Both corpora were consulted again during implementation. Native headless checks
cover rectangular full chains, all half-float cube faces/levels, exact logical
bytes and reported native storage, invalid payload atomicity, invalid level counts,
budget failure, move-only ownership and world upload publication rollback.

## Source base conversion and numerical filter

Read-only UI census found an active four-bit font atlas and odd menu dimensions;
these are handled rather than silently treated as eight-bit even-sized assets.
`ResourceArchive::metadata` provides one checked immutable metadata view for both
world and sprite decoders. The original initial PNG load requests NONE, without
dither. Original DLL witnesses establish nearest eight-to-four-bit conversion
and explicit zero fill after odd dimensions round up to even. Cubes preserve the
source's eight-bit choice for the four-bit metadata code.

[The numerical report](texture-mip-filter.md) records direct original DLL
execution and exact source filter distinctions. Every lower level reads the full
base independently; exact halving uses BOX, other reductions use the recovered
integrated triangle footprint. Ordered serpentine Bayer dithering applies to
UNORM outputs. Native Metal allocation/upload does no filtering and never calls
automatic recursive mip generation. Four-bit storage is expanded to RGBA8 for
this native backend while retaining original channel values and quantization;
its logical accounting describes those actual target bytes.

No visible launch is part of this completion pass. Automated source/native
verification does not establish matched original whole-frame appearance.

## Integrated verification

The complete original-root startup corpus passes with the option off and on,
using one final binary and the same scenario: 123.00 seconds off (300-second
bound), 185.91 seconds on (600-second bound). These are controlled renderer
observations over actual assets, including front-end DDS conversion and mission,
weapon and costume resource preparation; they are not native loading performance
or visual parity measurements. The wrapper rejects an executable changed during
a run. Final hashes, repository checks and independent review are recorded in
[the integrated verification](lighting-redesign-verification.md).
