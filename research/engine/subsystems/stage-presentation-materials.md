# Stage presentation material closure

October 23, 2026. Slice 4 is authorized. This record separates newly consumed
presentation inputs from the retained static inspection lighting limitations.

## Untextured ordinary objects

The three ordinary below-stage quads have raw selector zero and low variant
zero. Their declarations contain FLOAT3 position and SHORT4N normal, stride20;
there is no texture coordinate or authored texture binding. The original cached
`00800000000` maps to vertex447/pixel12; enabling the renderer's original SH
feature selects `00800200000`, vertex447/pixel16. Pixel16 consumes the same
material constant color, indirect color, emissive/ambient terms, packed SH,
reflection, fog, directional shadow and local-light terms as the corresponding
textured families, but never samples a base texture. Vertex447 never reads UV.

The bounded implementation adds an explicit base-texture requirement to shared
material classification and skips UV reads and texture lookup for this exact
untextured family. The stage now supplies directional illumination and its
shadow factor alongside SH and material terms. The restricted lighting policy
remains available for explicit observations. Dynamic local-light lists remain
a separate consumer; no cached no-shadow shader or flat-color replacement is
invented.

Evidence: ignored `reconstruction/world-renderer/shader-12.txt`, `shader-16.txt`,
`shader-447.txt`, the hash-bound shader registry and stage material census.
The previous geometry witness establishes only initial-camera occlusion, not
permission to discard these ordinary objects from stage presentation.

## Established implementation and contracts

Re-read Unreal's `FMetalRHICommandContext::RHIDrawIndexedPrimitive` and
`RHISetShaderParameters` in `MetalCommands.cpp`, pinned revision
`f45729c0b6f097038e72b52d366a62baeca3275b`. Binding follows the actual shader's
resource list; indexed draw addressing remains independent of material resource
presence. The reconstruction keeps explicit material requirements and binds no
base texture for a shader that does not consume one, while retaining original
immutable device geometry. No Unreal implementation is copied.

Both design and implementation queried the C++ corpora and retrieved full
I.1 (`#ri-explicit`) and GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`). Optional resource consumption is
part of the shared interface; unchanged geometry stays device resident. No new
upload, allocation or readback is introduced per frame.

## Background joint palette contract

Actual required background families use vertex475/476 (two authored index and
normalized-byte weight components) or vertex480/482 (one index, weight one).
The programs address 32 four-row joint matrices at original VS20..147, transform
position and normal/tangent before the object/view transform, and do not
renormalize supplied weights. Extra encoded weight components are not consumed
by these exact programs. Shape deformation is a separate unsupported boundary.

The portable draw borrows an explicit already-mapped palette only during
submission. Native submission copies the bounded palette into domain-owned
storage and validates referenced vertex indices before replacement. A failed
palette cannot publish a partial draw set. Original geometry remains resident;
retained small palette constants use the existing per-draw command encoding.

Production reference inspected at the pinned Unreal revision above:
`GpuSkinVertexFactory.ush` (`GetComputeBoneMatrixInputs`, `CalcBoneMatrix`,
`SkinPosition`) and `GpuSkinCommon.ush`
(`ComputeBoneMatrixWithLimitedInfluences`). The established split separates
pose/section mapping from bounded vertex consumption and applies the weighted
matrix before object placement. This reconstruction keeps Bullet Witch's exact
one/two-influence equations and four-row layout rather than copying Unreal's
modern pose, unlimited-influence or compressed-transform system.

C++ design consultation additionally retrieved I.11 (`#ri-raw`) and GPU.1:
the submission span transfers no ownership, its copied palette owner survives
GPU consumption, and unchanged geometry is not repacked each frame.


## Native shadow consumption

The native submission now carries an explicit light clip matrix, receiver
parameters and caster selection. A separate map pass precedes scene color on
the same command buffer. It retains accounted 1024-square R32Float and
Depth32Float attachments with reverse-depth clear zero and greater-equal tests.
Depth32 is the Metal attachment choice; the original allocation used D24S8.
Caster positions use the same palette and scale/offset as color; cutouts retain
raw UV and material-alpha coverage. The receiver performs eight point samples
from four original jitter slices. Jitter is retained as an immutable device
buffer with the original wrapped point indexing and RGBA components. This
changes storage representation, not sample selection. Seed 1 is explicit;
historical process RNG state is unavailable.

Shadow visibility multiplies directional diffuse and directional specular only.
It does not darken baked vertex illumination, spherical harmonics or environment
reflection. Outside-map receivers are lit. Clamp-to-edge sampling and the
bounds-fit projection are documented Unreal-informed choices while the original
unsupported address value and zero warp-angle initialization remain unresolved.
See [shadow presentation](stage-shadow-presentation.md).

Both native-interface design and implementation retrieved I.1 and GPU.1 from
the two MCP corpora. The explicit matrix/selection contract prevents hidden
scene policy. Retained map and jitter owners avoid intermediate CPU readback.
The production reference is pinned Unreal `ShadowDepthRendering.cpp`,
`FProjectedShadowInfo::RenderDepth`: declare the attachment and depth-write pass
before consumers, rather than deriving shadows in the material upload path.

The existing native surface scenario now renders separate receiver and caster
geometry. It verifies directional occlusion preserves baked illumination,
clearing caster selection restores light, and out-of-map receivers remain lit.
These checks passed on Metal. Separate initial/exterior scene observations and
the actual host window are recorded in [stage acceptance](stage-view-completion.md#slice-4-authored-initial-presentation), without claiming original-game image parity.


## Material updates without resubmission

`WorldTextureOffsetUpdate` identifies a retained model/draw and its two UV-offset
vectors. The native consumer validates the complete batch before writing any
constants; every matching instance receives the update. It retains geometry,
palettes, samplers and textures. Runtime animation therefore does not rebuild
thousands of draw submissions or native sampler objects each frame. Update
cost is a bounded scan over updates and retained draws with no allocation.
The existing native surface scenario verifies a two-texel sampling change,
unchanged device resource count and atomic rejection of a bad later update.
Both design and implementation retrieved I.1 and GPU.1 for this explicit mutable
boundary. The host forwards the batch under its existing main-thread contract.
