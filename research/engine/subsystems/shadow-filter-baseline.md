# Shadow filter border baseline

The source eight-tap jittered comparison remains intact. Native filtering now
protects a zero-cleared border, with the same derived inset in caster and receiver
projection. This replaces accidental repetition of an edge caster when a point
sample leaves the texture. The border mechanism is an explicitly chosen Unreal
reference baseline; the original game's undocumented sampler address value has
not been assigned invented semantics.

## Original sampler call revalidated

The machine instructions in
`research/evidence/engine/pass-08-world-state/instructions/00451770.txt`
show the call at `451cb9`: load the global device pointer, push value `6`,
state type `1`, sampler `13`, and the device `this`, then call vtable offset
`114`. The following call applies value `6` to state type `2`. State type `3`
is `1`; filter types `5`, `6` and `7` are `1`; border color is zero.

The retained DirectX SDK header defines device vtable slot 69 (offset `114`)
as `IDirect3DDevice9::SetSamplerState(this, Sampler, Type, Value)`. The right-to-left
COM calling convention and explicit `this` agree with those pushes. This is not
an argument shifted by a wrapper or a misidentified vtable method.

Device creation is also checked: `4122c0` calls the Direct3D9Ex creation method
at vtable offset `50`, writing its output directly to `a0b6e0`; `969104` points
to that same storage. The corresponding instructions push `a0b6e0` at `41260c`
and `41263a`. The native reference index confirms these relationships.

The SDK declares address values wrap `1`, mirror `2`, clamp `3`, border `4`,
and mirror-once `5`. It does not define `6`. The recovered call genuinely passes
`6`, but these static facts do not establish the driver's accepted behavior or
an external interception layer. Therefore this implementation does not claim
that original value `6` means clamp. Existing exports and SDK declarations close
the calling-convention question; no further speculative tracing is required to
choose a documented native boundary policy.

## Pinned production reference

Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`:

- `Engine/Source/Runtime/Renderer/Private/ShadowDepthRendering.cpp`,
  `FProjectedShadowInfo::ClearDepth` and `SetStateForView`: clear the full
  allocation, then restrict caster writes to the interior with a scissor when
  border clamping is required. The source identifies prevention of shadow
  stretching as the reason for this restriction.
- `Engine/Source/Runtime/Renderer/Private/ShadowRendering.h`,
  `FShadowProjectionShaderParameters::Set`: the shadow projection accounts for
  border offset/scale and uses a point clamp sampler.
- `Engine/Source/Runtime/Renderer/Private/ShadowSetup.cpp`: projection X/Y
  scaling reserves the border. Its fixed four-texel policy is not copied here.
- `Engine/Shaders/Private/ShadowFilteringCommon.ush`: sampled depth comparisons
  and clear-depth treatment are separate from the kernel. This reconstruction
  retains Bullet Witch's eight original jitter taps, not Unreal's PCF kernel.

The source files were inspected locally; ignored reference snapshots live under
`research/evidence/reconstruction/stage/`. No Unreal implementation text is
copied into production code.

## Shared footprint contract

For the 1024-square map, the guard per edge is
`ceil(radiusUV * 1024 + 0.5)` texels. Encoded jitter components lie in `[-1,1]`;
therefore the maximum displacement is the supplied radius. The extra half texel
covers nearest-sample footprint/rounding. Finite nonnegative input is required,
and guards occupying half the map or more are rejected before publication.
The actual authored radius, approximately `.058 / 1024`, requires one texel.

The native submission retains the maximum guard required by enabled receivers.
The full color/depth allocation clears to zero for reverse depth. Caster writes
use the interior scissor, without shrinking the viewport. The projection reserves
that same guard in homogeneous X/Y rows, preserving its depth and W rows. This
avoids cropping the fitted source volume while keeping samples beyond the outer
edge clamped to unoccluded zero depth. A changed receiver radius must be reflected
in both projection construction and submission.

Receiver centers outside the original normalized X/Y rectangle remain lit.
Inside it, the original comparison remains `receiverDepth + bias >= mapDepth`.
No new depth-range rejection, W clipping, comparison bias or far-plane rule is
introduced. Caster raster clipping remains Metal's homogeneous clip behavior.
The recovered perspective projection is described separately in
[shadow-projection-baseline.md](shadow-projection-baseline.md).

## Native verification

The existing Metal integration scenario uses authored geometry, a caster covering
the complete shadow allocation and a constant-coordinate receiver. It verifies:

- all four outer edges and four corners at zero radius, the authored small radius,
  and a two-texel radius whose taps cross the allocation boundary;
- retained interior caster coverage;
- exact eight-tap averages at each interior border and corner, using independently
  recorded seed-1 jitter signs at screen cell `(16,16)`;
- reverse-depth equality, nearer/farther comparisons, clear depth zero and each
  outside-rectangle direction;
- homogeneous W on both caster and receiver; and
- atomic rejection of a radius whose footprint leaves no interior.

The boundary averages are actual GPU pixel checks, not only tests of the guard
formula. For the two-texel radius the three-texel protected ring produces visible
fractions `3/8`, `5/8`, `4/8`, `4/8` on left/right/top/bottom and `6/8`, `6/8`,
`5/8`, `7/8` at the corresponding corners. These distinguish a protected clear
border from repeatedly sampling an occluding edge texel.

`cmake --build build/host-debug --target memory-tests --parallel 4` and the
resulting `memory-tests` executable pass with the native changes above. The
homogeneous-W case also passes. These checks establish the sampling and ownership
contracts; the stage capture remains the separate integration check for the
recovered camera warp and real corpus.

Both design and implementation queried the C++ Guidelines and Performance MCP
corpora. I.1 makes radius, footprint and failure behavior explicit at the shared
boundary; GPU.1 keeps the map and jitter resident, with no production readback or
per-frame CPU filtering. Readback appears only in the native verification fixture.
