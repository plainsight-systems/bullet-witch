# Native shading direction and pixel-footprint filtering

## Demonstrated boundary

The PW5 outdoor shoulder and PW0 cave weapon are captured through the actual
resident model, original material/light publication, native scene renderer and
production display pipeline. Debug contributions separate normal-map input,
interpolated basis, local/directional specular, shadow visibility and reflection.
The first large high-frequency signal is in the surface/specular path; output
mapping exposes it rather than generating it.

The recovered program separately normalizes normal and tangent but leaves their
mapped combination unnormalized. A unit N and T need not be orthogonal after
skinning and interpolation. For decoded tangent-space coordinates x,y,z and
d=N dot T, the squared combined length is
`z*z + x*x + y*y*(1-d*d) + 2*z*x*d`. Captured PW5 shading length 1.04754 makes
N dot H 1.04502 before saturation. Its unit-direction cosine is .997595.
The corresponding cave weapon local-light cosine changes from 1.02356 to .976817.
This is an angular invariant failure, not evidence that the authored light
strengths should be reduced.

## Adopted unit-direction boundary

The native shader normalizes the final mapped direction before all lighting,
Fresnel and reflection. Its direction, original basis construction, authored
normal sample, material exponent and light constants remain unchanged. Zero
remains zero. This explicitly departs from the recovered unnormalized equation.

Reference inspected: Unreal commit
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Shaders/Private/MaterialTemplate.ush`, non-decal
`TransformTangentNormalToWorld` and its `CalcMaterialParameters` consumers.
That implementation normalizes the transformed tangent normal before lighting.
The reflection formula also requires unit normals to preserve reflected-vector
length and angle.

Native verification uses original renderer capabilities with authored geometry:
an outside-disk decoded normal has cosine `1/sqrt(3)`; a sheared nonorthogonal
basis has independently calculated cosine .9462734107; an orthogonal basis
preserves its authored angular response. The memory executable passes. This
fix introduces no CPU state, resource, allocation or transfer. Both design and
implementation consulted full Core I.7 (`#ri-post`) and performance GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`): the unit-direction postcondition
is checked through completed native draws; diagnostic readback remains outside
the production path.

The original program witnesses remain unchanged and continue to document the
source equation on their supplied inputs. They are not relabeled as equality
proofs for this explicit native adaptation.

## Measured sampling problem and adopted filter

Unit direction alone does not remove outdoor shoulder stippling. In 2,586
adjacent pixel pairs within the same original triangle and draw, shoulder
specular red p99 differences change from 46.47 to 44.25. Enabling the already
implemented source mip option reduces normal-map p99 differences from .556 to
.241; it does not change geometric normals. The observed normal texture has
512 by 1024 pixels, one level under the original default, and approximately
three by three texels per screen pixel at a representative highlight. The
problem is present inside triangles, not merely at silhouette or hard-edge
boundaries.

The native specular consumer now estimates four full unit-normal samples at the
tensor Gauss nodes `(+/-1/(2*sqrt(3)), +/-1/(2*sqrt(3)))` within the pixel. These
are box-quadrature nodes, not scene-tuned offsets. Each sample uses interpolated
normal/tangent and UV derivatives; its normal lookup uses half-size texture
gradients for its equal-area footprint. Derivatives are evaluated in draw-uniform
control flow before alpha discard.

For mean `m`, centered variance `v = mean(dot(n_i-m,n_i-m))` supplies
`L = sqrt(max(1-v,0))`. The variance identity assumes unit samples; zero geometry
retains the center equation instead of manufacturing a distribution. Identical
samples give exactly zero variance without an epsilon. Mean direction is
normalized separately from the variance-derived length.

The [published Toksvig method](https://developer.download.nvidia.com/assets/gamedev/docs/Mipmapping_Normal_Maps.pdf)
then gives `p' = p*L/(L+p*(1-L))` and amplitude `(p'+1)/(p+1)`. The source
material exponent and intensity remain inputs. Zero power has unit amplitude;
a zero-length mean at positive power gives zero exponent and amplitude
`1/(p+1)`. There is no dependence on `pow(0,0)`. Consumed negative exponents are
rejected transactionally at native draw admission, not silently clamped.

Pinned Unreal's `TextureCompressorModule.cpp`,
`CompositeNormalLengthToRoughness` / `CompositeNormalToRoughness`, demonstrates
the normal-length-to-lobe-variance architecture. Its PBR roughness constants
are not imported into this legacy power model. This implementation estimates
moments at runtime from existing source XY textures and interpolated geometry;
it does not rewrite assets or claim original source parity. It also avoids
incorrectly trying to obtain variance from a single reconstructed unit normal.

The prepared moment is reused by directional and local direct specular lobes.
Diffuse, irradiance, Fresnel and cubemap response retain their center unit normal.
Only draws with a positive-power, nonzero-scale direct specular consumer execute
the quadrature. Cost is four extra normal-map lookups for those mapped materials,
bounded local shader values and no allocations, retained history or transfers.
Geometric-only materials require no additional texture. Both MCP phases also
retrieved Core I.6 (`#ri-expects`) for the public exponent precondition.

## Actual-content verification

The comparison uses aligned sixteen-sample linear-HDR reference frames, before
exposure/gamma/bloom. All sixteen subpixels must match the low-resolution opaque
owner, draw, primitive and shadow visibility; every option uses the same mask.
Temporal error subtracts the reference frame's own response to a 1 mm camera
translation. Fourteen low-resolution blocks affected by tiny diagnostic versus
production differences are excluded explicitly, not hidden by tolerance.

| Region | Static pixels / motion pixels | Original static RGB MAE | Repaired static RGB MAE | Original motion-residual MAE | Repaired motion-residual MAE |
|---|---:|---:|---:|---:|---:|
| PW5 body | 1469 / 1141 | .15658 | .10644 | .19774 | .11829 |
| PW5 shoulder | 140 / 124 | .33711 | .15636 | .44494 | .23358 |
| PW0 cave weapon | 744 / 585 | .00869 | .00320 | .00724 | .00272 |

The independent reviewer reproduced every strict numerical row. Freshly linked
probes then compared the integrated production shader with the frozen candidate:
both complete HDR frames (2,359,296 HALF components each) are bit-identical,
as are every retained presentation frame at 1, 119, 120, 179 and 180. This ties
the numerical candidate and visible output to the actual implementation.

Native public-contract tests additionally verify the constant-field positive
power peak, zero-power amplitude and rejection rollback. Original source
mip/edge defaults and light intensities remain unchanged. A matching original
live frame is not required for these demonstrated native repairs.

The filter estimates a pixel-footprint distribution; it is not exact sixteen-
sample rendering, a temporal antialiaser or a denoiser. Bright coherent shoulder
highlights remain, and source mip enable still improves some outdoor temporal
measurements more than this bounded filter alone. This does not justify
silently overriding the launch setting. The measured improvement and current
images were accepted by the user on October 25 with residual angle-dependent
speckling and coarse shadows retained as baseline limitations. This does not
establish original-image correspondence or declare every bright pixel wrong.

Ignored reproducible captures and numerical results are organized under
`research/evidence/reconstruction/lighting-noise-material/`; independent
source/reference and numerical checks are under `lighting-independent-review/`.

## Final repository verification

`python3 scripts/check.py all` passes: 335 files satisfy the pinned formatter,
185 translation units pass fatal clang-tidy, all six suites pass, and five
repository-policy tests plus the 658-file content guard pass. Independent review
repeated the native suites, strict reference metrics and complete frame-byte
comparisons, and found no remaining actionable implementation or ownership
finding. The final gate log is retained as
`lighting-noise-material/surface-filtering-full-gate.log`. No visible game
launch, commit or push was performed for this repair.

## User acceptance — October 25, 2026

The user accepted the current lighting implementation and authorized its commit.
Residual angle-dependent speckling and coarse shadows are accepted baseline
limitations. They may be consistent with limitations of the original renderer,
but no matched original-image correspondence was established. The explicit
native final-normal normalization and pixel-footprint specular filtering remain
documented departures from the recovered equation, not claims of original
behavior.

The latest reproduction references are the user's October 25 screenshots at
7:18:15 a.m. (cave) and 7:19:52 a.m. (outdoor school outfit). They preserve the
viewing contexts for the remaining angle-dependent appearance; the screenshots
and extracted data remain outside Git. No new visual investigation is required
for this accepted slice. Residual visual quality belongs to the later upgrade
pass unless a concrete new implementation defect is demonstrated.

Acceptance closes this represented lighting slice, not the named upstream
mission/event/weather/particle/settings producers or every original-device
evidence limit. The independent implementation review and passing verification
remain distinct from this user decision. Production code is unchanged since
the final passing full gate; this acceptance update is documentation only.
