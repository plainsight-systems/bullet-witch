# Final display edge filter

The original has a known optional spatial filter after scene composition. It is
separate from tone mapping, bloom, depth of field and runtime elemental effects.
This document records its source contract, native policy and verification limits.

## Source and activation

Original executable SHA256:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
Retained source disassembly is under ignored
`research/evidence/reconstruction/lighting-source-fidelity/output-lifecycle/`.
New raw shader/source and executable witnesses are under ignored
`research/evidence/reconstruction/lighting-final-edge/`; none are copied into Git.

`410750` maps checkbox `3f6` to SystemSetting flags mask `0x04` (bit index2).
The settings translation maps that mask to renderer flag `0x40`. This is not
bit index4. The SystemSetting record is36 bytes; the flag word begins at byte8.
The missing/invalid configuration branch at `41064a..4106b4` clears the record,
selects a supported mode, and writes flags `0x2f1e52`. Mask `0x04` is **off** in
those defaults. A user's saved setting can turn it on independently of stage data.

`461db0` first queues final-display framebuffer binding. When renderer flag
`0x40` is set and its supplied current surface contains a nonnull texture, it
queues the special final-copy command through `461850/461940`. Otherwise it
uses the ordinary copy. The surrounding `4618f0/4619f0` commands dispatch to
`436ab0`, a no-op; they are not an inferred sampler/state save/restore.
The actual special command vtable `82cf80` dispatches `547f30`.

`547f30` binds source texture0, disables depth and blending, supplies pixel
constant0 XY `(0.5/width,0.5/height)` and constant1 XY `(2/width,2/height)`,
then executes the fullscreen quad and re-enables depth. Dimensions are the
original active frame globals. Original vertex source at `83f1a8` passes clip
XY and texture ZW from each quad vertex without any projection offset.
`546970` supplies corners (-1,+1),(+1,+1),(+1,-1),(-1,-1) and UVs (0,0)..(1,1).
Thus D3D9 samples normalized pixel coordinates `(x/width,y/height)`. The native
edge vertex uses a half-pixel raster shift to preserve that coordinate contract;
it does not reuse the quarter-pixel postprocess quad.

## Shader and input contract

Pixel program `9694e0` is1300 bytes. Its literal shader instructions establish:

1. Read the four half-texel diagonal neighbors. Compute RGB luminance with
   (.299,.587,.114); add .0026041669771 to the northeast neighbor's luminance.
2. Form the edge direction from diagonal differences and normalize it. The
   outer offset is direction divided by8 times the smaller absolute direction
   component, clamped per component to[-2,2], then multiplied by two texels.
3. Read the two outer samples and two inner samples at direction times half a
   texel. Form the four-sample mean and the inner two-sample mean.
4. Use the four-sample result only if its luminance lies within the diagonal
   neighbor range; otherwise use the inner mean.
5. Include center luminance when calculating local contrast. Keep the unfiltered
   center unless contrast is at least max(.125*maximum diagonal luminance,.05).

These are the original constants and branch boundaries, not a modern FXAA
substitution or a visually tuned sharpness control. Alpha follows the same
sample/selection operations. Presentation opacity is the existing host-owned
RGB fade, applied after this output result.

The special executor itself does **not** set sampler state. The represented
preceding tone executor `57a220` leaves sampler0 point filtered, clamped in U/V,
with no mip filter; the current RGB-only filter also leaves point/clamp. Native
implements that concrete predecessor contract. Missing runtime effect producers
that could change inherited sampler state must publish their own complete pass
contract before activation; a global claim of sampler equivalence across all
absent effect chains is not made here.

`463040` invokes `56e990` with format21 (A8R8G8B8) for tone output. Therefore
when tone precedes this filter, native first renders the completed tone/bloom
result into a retained BGRA8Unorm texture, then samples it in the edge pass.
Filtering HDR before exposure or moving quantization after filtering would be a
different algorithm. When tone is disabled, the filter consumes the supplied
current scene/RGB-filter surface directly rather than inventing a tone pass.

## Engine boundary

`WorldOutputParameters::final_edge_filter_enabled` is an explicit copied
policy, defaultfalse. It belongs to saved display settings, independently of
stage-authored tone/bloom values. The current game has not reconstructed the
Windows configuration-dialog/SystemSetting owner; the renderer API supports
activation, but that does not claim a missing settings UI or persistence path.
The engine must publish this policy from its settings owner when that exists.

WorldPostprocess owns the optional display intermediate with normal device-budget
accounting. It allocates on activation/extent change, reuses across frames and
toggles, and releases with scene targets. The edge filter runs before display
overlays. No CPU image transfer or new animation/world clock is introduced.
Both design and implementation retrieved Core I.1 and performance GPU.9:
activation is explicit, and the extra bounded target remains owned until GPU
consumers complete. Original algorithms are readable, so no Unreal algorithm
substitution was necessary.

## Verification and remaining boundaries

Three original `547f30` executions with supplied16x16,1920x1080 and2560x1440
frame dimensions confirm constant XY uploads, texture binding, and depth/blend
writes. Shader initialization/binding and draw helpers are skipped in that
executor witness; it does not claim a live original GPU framebuffer.

The independent pixel oracle interprets the original SM3 instructions on an
authored16x16 grayscale image with explicitly supplied point/clamp sampling and
integer-pixel UVs. Its256 expected pixels include80 changed by the filter.
The tracked native helper uses the real production Metal shader, completed
BGRA8 pixels, the source-derived expected image, and owner lifetime/accounting
checks. Native verification results are recorded below.

The source constructor defaults and explicit renderer API do not establish the
user's original live Steam configuration. Original GPU sampling/rounding and
OS presentation color management remain separate from this bounded witness.
Absent depth-of-field, mist, elemental, afterimage and other effect producers
remain distinct upstream activation boundaries; this filter does not implement
or silently accept those owners.

Native verification completed: all256 grayscale pixels and16 independent
colored/variable-alpha sample pixels agree with the literal original-bytecode
oracle within one8-bit storage step per channel. The colored case exercises the
luminance coefficients and filtered alpha separately from grayscale. Actual
WorldPostprocess checks confirm one additional accounted BGRA8 image only when
post-tone filtering is enabled, reuse across completed frames/toggles, uniform
tone-output preservation, tone-disabled direct input, and complete resource
release. The focused memory-tests and resource-tests executables pass.

Pinned fatal clang-tidy passes world_output.cpp, world_postprocess.mm,
world_output_test.cpp, macos_final_edge_test.mm and the integrating native
scenario file with their actual CMake compile flags. The shader compiles through
the normal target. Source/native witnesses and logs remain in the ignored
lighting-final-edge directory. Whole-repository gates remain the integrating
lead's responsibility; no visible launch, commit or push was performed here.
