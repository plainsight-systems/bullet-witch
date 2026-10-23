# Original display luminance adaptation

The original engine is the production reference for this controller. Native
code and instruction exports are retained in ignored
`research/evidence/engine/pass-21-tone-shader/`, `pass-22-tone-programs/` and
`pass-23-tone-adaptation/`. The shared contract deliberately retains its sampled
ring and linear slew algorithm rather than replacing it with an exponential
adaptation model from another engine.

`0x5712f0` reads the completed log-luminance result, computes its exponential in
double precision then converts to float and floors at 0.001. Frame delta is
`0x00a0b7b8`. The shared API takes that elapsed interval explicitly; native
readback and render-target lifetime remain backend responsibilities. The
accumulator reaches the original float `0x3d088889` (1/30 second), resets to zero,
and inserts exactly one sample. Excess time is discarded, never replayed as
multiple history samples.

Before the ring fills, the new luminance becomes adaptation state immediately.
Once full, `0x571060` averages the retained ring in index order and the update
limits change to plus or minus `adaptation_slew * frame_delta`. The internal
state at offset 0x40c is written **before** output minimum/maximum clamps. Those
clamps must not feed back into temporal history. If both bounds are enabled,
maximum is raised to at least minimum + 0.001 for that update.

Native setters supply the normalization contract:

| Function | Setting | Range / reset |
| --- | --- | --- |
| `0x571250` | History length | Clamp to 1–256; changing it clears the ring, write index, full flag and adaptation state, but leaves the external sample clock alone |
| `0x5712a0` | Slew per second | Clamp to 0.005–100 |
| `0x571200` | Minimum | Clamp to 0.001–63.999 |
| `0x5711b0` | Maximum | Clamp to 0.002–64 |

`0x461f00` supplies the stage settings every frame and enables both bounds. A
nonpositive authored history length disables user-luminance selection in the
output pass while its setter still constructs a one-element ring. Therefore
that mode decision belongs to the output backend, not the adaptation function.
The selected stage authors 64 samples, slew 0.0005 (normalized by the original
setter to 0.005), minimum 0.001, maximum 64, exposure key 0.18 and gamma 2.2.
The parser preserves authored values; the controller applies native ranges.

The current inspection backend uses the actual current GPU reduction for its
first frame when no previous completed sample exists. This is an explicit
inspection scheduling policy, not proof of the original first-frame readback
or exposure schedule. No synthetic exposure value is substituted.
Shared tests cover startup, sample-clock boundaries, ring wrap, slow-frame
behavior, slew, output-only clamps and invalid-input state preservation inside
an existing in-process test executable. They do not certify native HDR/output
composition; that has separate real-device readback and ignored stage capture.

## Native stage capture

The ignored manual diagnostic `research/evidence/reconstruction/stage/offscreen_stage_probe.mm`
uses production `MissionLoading`, `StageInspection`, `WorldRenderer` and compiled
shaders against a fixed 1024 × 576 offscreen Metal target. It performs the same
HDR preparation and final display composition as the host, then synchronously
reads the completed BGRA8 target into a PNG. Its exact current-library build
command is retained beside it as `offscreen_stage_probe.build.txt`. This is
repeatable manual research, not a new integration scenario or replacement for
normal-menu/original-Lua acceptance.

The raw-surface baseline `offscreen-stage.png` had mean RGB 0.972239 / 255.
The authored output path `offscreen-stage-output.png` produced mean RGB
108.074 / 255, with 581,946 nonblack pixels. Both select 1,672 main/cave draws
plus one sky. Visual inspection of the latter shows textured cave walls,
ceiling, floor and the building beyond the opening. Exterior black horizontal
regions and a bright opening remain visible; the capture does not claim
complete scene/pass parity. No diagnostic brightness or light overrides were
introduced to obtain it.

The next capture, `offscreen-stage-output-ccw.png`, applied the traced default
counter-clockwise cull state and still showed the exterior gaps. Independent
CPU rays into pixels (512,350), (512,390) and (512,430) found no triangle in the
then-selected main/cave models. Their nearest actual scene surfaces belonged to
`st04_c_background` ground and `st04_c_detail2` metal/wall geometry, with nonzero
authored illumination. The ignored `exterior_pixel_rays.cpp` and its log retain
exact draw/material/triangle identities. This identified a model-selection error,
not a reason to change shader brightness or suppress legitimate black materials.

After applying the recovered ordinary-model classification, the production
capture `offscreen-stage-all-world.png` selects 4,097 draws across ten models,
with three custom-pass omissions and one sky draw. It shows the metal entrance
structures and fills the earlier exterior holes; mean RGB is 104.547 / 255 with
589,529 nonblack pixels. The cave exit ground remains very bright. This confirms
useful static presentation and the missing-model diagnosis, while preserving the
remaining authored-pass and gameplay limitations.


The source-camera capture `offscreen-stage-original-camera.png` uses the
recovered44-degree camera, registered static clearance queries and reversed
depth. Its raw HDR sibling contains finite RGB with a maximum near0.433; the
bright exterior sample at(512,350) is approximately(0.2441,0.2498,0.2399).
Independent CPU replay of the recovered first-frame reduction, bloom and output
for these exact HDR pixels matches the native display within one byte per
channel (mean absolute error0.0617). The source reduction gives log luminance
-6.217097, geometric luminance0.001995 and exposure60.0996. The bright output is
therefore reproduced by the current pass equations and first-frame policy; this
does not prove the original startup exposure schedule, complete material/pass
selection or full visual fidelity. See the renderer's
[display-output findings](world-display-output.md) for that distinction.
