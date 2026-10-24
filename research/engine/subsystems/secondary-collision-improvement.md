# Secondary collision: bounded improvement assessment

October 23, 2026. Decision: retain the recovered solver for this slice. The small
iterative correction tested here improves node contact but does not reliably
improve the skinned garment. A mesh-aware collision upgrade is separate work;
remaining dress/body visual parity stays unresolved. No production code, capsule
radius, authored mask or garment visibility was changed for this assessment.

## Published methods and scope

[Müller et al., Position Based Dynamics (2006)](https://matthias-research.github.io/pages/publications/posBasedDyn.pdf)
describes iterative constraint projection. Its rigid-body/cloth collision section
also explains why vertex-only tests miss intersections through triangle interiors.
This supports testing alternating contact and structural projections; it does not
make endpoint contact equivalent to garment surface collision.

[Macklin et al., XPBD (2016)](https://matthias-research.github.io/pages/publications/XPBD.pdf)
adds compliance and accumulated constraint multipliers to reduce dependence of
stiffness on timestep and iteration count. Zero compliance reduces to the hard
PBD constraint case. XPBD does not supply missing garment collision geometry or
make low-iteration hard contacts converge automatically. Replacing this source
solver with XPBD would therefore be a redesign, not a small dress-clipping fix.

The source implementation already has closest-point capsule projection:
`0x4fe900` projects the first matching capsule; `0x4fcd80` subsequently applies
cone and segment-length constraints. Those later operations can put an endpoint
back inside a capsule. This ordering is source-backed, not a newly discovered
reconstruction error. The parent node's collider mask applies to its child
endpoint; anchors must not be included in a dynamic endpoint clearance tally.

## Bounded actual-content experiment

An ignored copy of the production solver adds either eight or thirty-two
alternating passes: project against every masked capsule, then reapply the
existing cone and segment-length rules. The baseline uses zero additional passes.
No authored contact radii or masks change. This is a candidate intentional
improvement, not asserted original behavior.

Each variant runs the same six costumes, fifteen clips, two translated/rotated
placements and 120 updates per case. Four sampled frames per case produce 720
records. Skinning uses the production material palette and packed weights without
renormalization. The diagnostic counts draw-referenced vertices influenced by
secondary bones and compares them with the four animated leg capsules.

| Measurement across 720 sampled frames | Source baseline | 8 passes | 32 passes |
|---|---:|---:|---:|
| Mean worst masked endpoint penetration, world units | 0.015456 | 0.003824 | 0.002951 |
| Worst masked endpoint penetration | 0.136852 | 0.034954 | 0.034954 |
| Sum of sampled secondary-influenced vertices inside leg capsules by more than 0.001 | 19,386 | 18,949 | 18,980 |
| Worst sampled vertex penetration | 0.155380 | 0.156193 | 0.156246 |
| Frames with fewer inside vertices than baseline | — | 52 | 52 |
| Frames with more inside vertices than baseline | — | 39 | 41 |

All variants retain finite skin matrices, unchanged body bones, zero steady-state
memory-domain allocation calls and zero live domain bytes at teardown. Length
and cone projection remain in each experimental pass; their retention alone is
not proof of simultaneous contact convergence or acceptable motion. Increasing
the iteration count does not remove the worst endpoint residual, and the mesh
proxy regresses in dozens of samples. No native render of the experimental
variants is claimed or needed to promote this failed candidate.

These are **contact proxies**, not proof of render-visible garment/body triangle
intersection. Capsules approximate the animated body; skin weights interpolate
between frames and can move a garment surface differently from its controlling
endpoints. Vertices are counted per draw reference, not as unique mesh vertices.
Secondary-influenced vertices also include non-skirt geometry. The reviewed live
frame was not captured with a matching state, so these numbers do not establish
that its visible clipping coincides with any particular residual.

A preceding focused forward-evasion frame-30 check found 13–19 negative
endpoint/capsule pairs across costumes/orientations, 9–15 against leg capsules,
with worst penetration approximately 0.0040–0.0059. The broader motion corpus
above exposes larger residuals; the focused result must not be generalized.

## Original PC footage actually inspected

The publisher's [XSEED PC gameplay livestream](https://www.youtube.com/watch?v=XAkp-wx6zgY)
was viewed in a dedicated Chrome tab, muted and paused, using the seek slider and
frame-step controls. No remote video was downloaded.

- [25:24](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=1524s): default long dark dress from the side/rear; useful silhouette reference.
- [25:29](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=1529s): camera cutaway to an enemy; excluded from garment assessment.
- [26:18–26:19](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=1578s): sampled paused movement frames, including a raised leg and trailing/folding dress panels. The backlit, compressed image does not reliably distinguish exposed authored slits from a leg crossing a garment triangle.

These inspected frames are visual reference, not confirmation of exact clipping
severity or parity with PW5. The stronger historical dress/leg statement remains
the firsthand Xbox 360 review recorded in
[secondary-motion native verification](secondary-motion-native-verification.md#original-release-clipping-reports-and-current-review).
Do not convert that review or these inconclusive PC frames into a tuning target.

## Upgrade boundary and reproduction

A meaningful upgrade needs garment-surface coverage and a mapping from its
contacts back to secondary bones or a separate deformable surface, together with
structural constraints, temporal contact handling and costume-specific validation.
That is larger than adding one projection after the existing chain solve. Keep
this bounded experiment as a rejected candidate; do not ship its extra iterations
or inflate capsules to hide intersections.

Ignored evidence is under `research/evidence/reconstruction/secondary-collision/`:
`build_experiment.py`, `secondary_variant.cpp`, `mesh_witness.cpp`, the three
`build-*.json` command records and logs, `witness-{0,8,32}.log`, `comparison.json`,
and the focused `residuals.py` / `residuals.json`. The generator compiles standalone
executables against the coherent host libraries; it does not replace the shipping
archive. Each executable takes the read-only original game directory as its sole
argument. The underlying actual-content witness and source tables remain under
`research/evidence/reconstruction/actor/`.

C++ guidance consulted for this bounded algorithm investigation: Core Guidelines
E.4 (design invariants) and C++ Performance Guidelines MEM.9 (prepare allocation
outside hot updates). No production change means no new production gate or native
visual acceptance is claimed; the existing complete application gate remains the
last implementation validation.
