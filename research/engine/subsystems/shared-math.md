# Shared CPU mathematics

The engine math boundary consolidates existing numerical operations; it does
not introduce a new scene coordinate system or change source reconstruction.
`bw_math` owns fixed vectors, row-vector matrices and affine construction under
`include/bw/math`. Scene queries, camera, shadows, asset decoding and animation
retain their own validation, tolerances, fallback directions and policy.
Reusable numerical algorithms stay in this library when new consumers are added;
local normalization, inverse or shape-conversion implementations must not
reappear in subsystem files.

Before consolidation, identical float and double dot/cross functions lived in
scene collision, placed collision, view and shadow files. Camera offsets and
quaternion-to-affine matrices repeated the same operations. Precision is an
explicit template parameter: a double convex-hull calculation must not silently
become float because both use a three-component array. Storage remains ordinary
fixed arrays, preserving current public layout and ownership.

Reference inspected: Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Core/Public/Math/Vector.h`, `TVector::DotProduct`,
`CrossProduct`, arithmetic operators, and the distinction between safe and unsafe
normalization. Its common typed numerical API is the relevant pattern. We keep
current arrays and explicit arithmetic functions instead of importing the engine's
vector class, target register layer or tolerance choices. The source snapshot is
ignored under `research/evidence/reconstruction/traversal/`.

Three-component dot products retain `(x*x + y*y) + z*z` ordering. Component
arithmetic retains input precision, and division remains division; no reciprocal
approximation or explicit fused multiply-add is introduced. Existing matrix
multiplication retains zero-seeded, ascending inner-index accumulation. Placed
point transforms explicitly retain their double accumulator and float output.
Source quaternion magnitude remains meaningful: affine construction does not
normalize quaternion inputs. Binary parity across compilers or silicon is not
promised; current native regression witnesses and arithmetic-order checks bound
the change. Future SIMD implementations belong behind this numerical contract
and require representative measurements and numerical verification.

Secondary motion extends that same boundary with `normalize_or`, whose caller
supplies the fallback vector and length threshold; the fallback is returned
unchanged. Matrix storage conversion, affine inversion of homogeneous storage,
and translation access also live here. Homogeneous affine inversion rejects a
perspective column and delegates to the existing affine inverse arithmetic.
Callers decide whether failure is an error and which fallback their algorithm
requires. This adds no virtual dispatch or subsystem-specific tolerance.

Direction stepping now lives in `direction.hpp`: `turn_toward` snaps within the
angular step, otherwise constructs a normalized turn basis using caller-supplied
collinearity and axis-degeneracy thresholds. The fallback axis explicitly chooses
the near-antipodal turning side. Finite unit inputs and a nonparallel fallback are
preconditions; no subsystem-specific constants enter this shared algorithm.
The [original turn witness](player-direction-reference.md#verified-near-antipodal-turn-correction)
executes the original helper bytes, establishing the branch difference from a
shortest-yaw clamp. Existing resource tests verify those outputs and the inclusive
threshold boundary. Both phases additionally read I.7 for result contracts.

C++ design consultation: Core I.9 (`#ri-concepts`) makes floating-point template
requirements explicit; I.1 (`#ri-explicit`) keeps input precision and accumulation
contracts visible. Performance MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) retains fixed values and
no frame-time allocation. The performance search also returned GDSA.2 on
reduction determinism; its nonassociativity warning is applicable, but this is
CPU scalar arithmetic, not adoption of a GPU reduction algorithm.

## Verification

The existing resource target now checks float/double cancellation, signed-zero
differences between ordinary and zero-seeded dot products, cross-product
handedness, non-unit-quaternion scaling, affine inverse round trips, singular
rejection, explicit point accumulator precision and matrix composition order.
Additional cases verify caller-selected normalization thresholds/fallbacks, exact
affine storage/inverse equivalence, perspective rejection and preservation of
basis/homogeneous lanes when changing translation.
Existing scene/camera/animation/runtime contract tests also pass. Scoped fatal
clang-tidy covers all changed mathematical consumers; final repository checks
are run after integration with the concurrent actor fixes. No new integration
scenario or external mathematical dependency was added.

The independent authored-stage floor probe was rebuilt against the consolidated
library. Its 234 emitted collision-plane, capsule-clearance and render-floor
records exactly match the earlier report at the probe's recorded precision.
This is additional geometry regression evidence, not a cross-compiler bitwise
claim or corrected-foot native acceptance. Evidence is retained as
`floor-alignment-math.jsonl` beside the original traversal report.
