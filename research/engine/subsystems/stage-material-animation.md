# Stage material animation

The One Shot Kill stage retains an authored material animation clip and label
map. The clip contains three float2 tracks; the stage model has two animation
slots named `Material516.t0` and `Material834.t0`. Matching names binds tracks
1 and 2; the unmatched label at track 0 has no model slot and is ignored. Each bound
track has 4,001 keys, frame times 0 through 4,000 and a 30-frame-per-second
clip rate. Production code reads these values; it does not hardcode a water
scroll speed or material name.

Original attachment is traced through `0x5e4df0`. The model update at
`0x42aec0` zeroes material slots, evaluates attached clips and accumulates
mapped values. Snapshot construction `0x4268d0` maps global slots into each
model's table. The `TANM` executor `0x430920` copies a selected float4 into
vertex constant `4 + destination`; actual stage commands select destination
zero and slots zero or one. `TRST` resets that constant. The recovered static
vertex program adds constant 4 XY to the base UV.

The bounded reconstruction supports the actual uncompressed float2 key
encoding and linear interpolation; other encodings remain explicit failures
when requested. Immutable key bytes remain with the mission owner. Validation
checks finite rate/duration, sorted finite times, finite values and complete
ranges once, then sampling uses binary search and fixed scalar values.

The production reference is Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Curves/RichCurve.cpp`, `InterpEval` and
`FRichCurve::Eval`. The relevant contract is endpoint handling followed by a
bounded search for adjacent keys and interpolation within their time interval.
Our reconstruction retains original source encoding/time units; it does not
copy Unreal tangent, compression or extrapolation machinery. Generated source
snapshots and extracted clip/model bytes remain ignored stage evidence.

Animation updates replace only the two texture-offset constant vectors for
matching retained model/draw identities. They do not reconstruct the full draw
list, recreate samplers, upload geometry or allocate per frame. The host supplies
its existing game-time delta to the stage view; no independent wall clock is
introduced.

## Clock and sampling closure

Pass 63's `0x41edb0` zero-initializes the result, clamps a shorter track to its
own end, calls `0x41e760` with linear interpolation enabled, then clears Z/W.
The `0x46000000` branch selects an exact key or adjacent interval and evaluates
`left + (right - left) * alpha`. Only the meaningful float2 is read by the
reconstruction; the original temporary float4 read is not reproduced beyond
its useful components.

Pass 65's `0x41f140` multiplies elapsed seconds by the authored clip rate, then
calls the clock at `0x41f0a0` (pass 64). The repeat flag comes from MTAB offset
`0x10` via `0x504480`; it is one in the actual clip. The clock wraps whenever
phase is greater than or equal to duration, including the exact endpoint.
Nonrepeating clips clamp at duration. The sampler preserves these controls,
using bounded modulo rather than repeated subtraction for long time steps.
Double accumulated elapsed seconds avoids unbounded per-frame work; this is
not a claim of bit-identical long-run x86 float accumulation.

Model decoding now captures effective `TANM`/`TRST` slot state at every draw.
This matters because reset commands follow each of the two animated surface groups: treating
all commands inside a material as one final state would erase their animation
or leak it into later surfaces. Model slot names and clip labels remain format
boundaries; rendering receives descriptive offset updates.

Each active `TANM` encloses three indexed draws before its `TRST`, so the two
curves produce six retained constant updates. The source command offsets and
existing corpus check establish this count; it is not six independent curves.

## Verification

The independent in-process sampler cases pass for nonuniform times, fractional
interpolation, repeat boundaries, nonrepeating endpoint clamps and malformed
key/clock data. The existing original-root corpus scenario passes with six
updated draw constants and the two independently expected authored values at
one elapsed second. Engine-domain live bytes do not change during that update,
and all migrated CPU allocations return to zero at teardown. The corpus reports
4,100 selected ordinary draws with zero omissions. These checks verify data,
binding and lifetime; native visual observation is recorded separately. All
changed animation translation units passed targeted pinned fatal clang-tidy.

## Steady-state data-flow refinement

The first implementation sampled the same named channel once per draw and scanned
all retained draws twice per update. For this stage that meant six curve searches
for two curves and repeated searches through 4,775 submitted draws. This is
avoidable work rather than an interpolation requirement.

The pinned Unreal revision above, `Engine/Source/Runtime/Engine/Private/Curves/SimpleCurve.cpp`,
`FSimpleCurve::Eval` and `EvalForTwoKeys`, uses binary search over prepared keys
followed by scalar linear interpolation. The original interpolation routine
likewise uses scalar SSE instructions. A two-component result alone does not
establish a useful packed-SIMD workload. Preserve the original interpolation and
clock contract while preparing the data flow: bind each named curve once, sample
it once per tick, distribute its value through retained slot indices, and locate
native destinations through a sorted publication-time index. Sample loads use
unaligned-safe copies from ranges already validated at bind time; they do not
repeat file-range validation. Setup remains the allocation boundary. Native
updates still validate the complete batch before changing any constants.

The resulting expected work is O(unique curves * log keys + animated slots +
updates * log submitted draws + matching destinations), without per-frame
allocation. Measurement and optimized code inspection accompany the change;
CPU SIMD specialization remains a separate measured comparison, including
Apple ARM and Intel vector instructions. This is unrelated to moving curve
evaluation onto a GPU.

### Measured sampler cost

An optimized ARM64 probe used the actual two 4,001-key tracks, 10,000 warm-up
samples, then five trials of 300,000 updates at `(i % 8000) / 60` seconds. The
pre-change sampler was compiled from the retained optimized assembly; the new
sampler used `-O3 -DNDEBUG`. Parse/bind support ran before the timed region. Median
sampler-only cost was:

| Work per update | Median nanoseconds |
| --- | ---: |
| Previous implementation, six samples | 246.605 |
| Validated-load implementation, six samples | 134.706 |
| Validated-load implementation, two unique samples | 62.399 |

Checksums match at the same sample count. Optimized assembly now uses direct
loads in the binary search without calls to checked byte-region helpers.
Interpolation remains scalar on this target. These numbers measure the sampler,
not native publication, rendering or whole-frame performance; they do not claim
an ISA-specific speedup and do not measure or disprove a benefit from CPU SIMD.
No packed ARM/Intel implementation was compared in this experiment. The current
two-curve update and a future larger animation batch are distinct workloads:
prepared channels provide the place to batch independent values, while a CPU
SIMD backend would need its own representative layout and timing comparison.
The independently removed full-scene update scans are reflected in the
algorithmic cost above, not included in this sampler timing. The reproducible probe, assembly and trial logs remain ignored
under the stage evidence directory.

The user accepted scalar interpolation for this initial port pass after this
comparison. That acceptance does not establish scalar code as the long-term
CPU animation backend. Where reconstruction leaves implementation ambiguous,
use established data layout and CPU vectorization practice, with representative
measurements, rather than assuming a naive scalar loop is sufficient.
