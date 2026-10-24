# Skeletal animation baseline

October 23, 2026. [Architecture](../../../docs/architecture/actors-and-animation.md).

The source pose evaluator `0x423d20` reads separate scale, quaternion and
translation channels. Absent channels use the present track's constants at bytes 4,
16 and 32 respectively. The whole-transform sampler `0x429780` orders its ten floats as
quaternion, scale, translation. The vector sampler `0x429fe0` uses linear
interpolation; quaternion sampler `0x4299b0` calls shortest-path spherical
interpolation at `0x406ed0`, with the near-parallel linear threshold
approximately 0.9999899864. Exact keys bypass interpolation. The half conversion
retains the original exponent-31 behavior described in [animation data](animation-data.md).

Source instructions contain packed SSE for quaternion arithmetic. That is CPU
SIMD. The reconstruction must not describe all animation as scalar merely
because the unrelated float2 material curve uses scalar SSE. Numerical
equivalence of the equations does not establish bitwise equality to the
original trigonometric approximations or target SIMD throughput.

Before design, inspected Unreal at
`f45729c0b6f097038e72b52d366a62baeca3275b`:

- [`UAnimSequence::GetBonePose`](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Engine/Private/Animation/AnimSequence.cpp)
  initializes the reference pose, then evaluates selected animation data.
- [`FAnimationRuntime::ConvertPoseToMeshSpace` and `FillUpComponentSpaceTransforms`](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Engine/Private/Animation/AnimationRuntime.cpp)
  consume retained local transforms in parent-before-child order; each child
  composes with the already evaluated parent. The cached single-bone helper
  confirms that repeated ancestry evaluation is not necessary.
- [`TTransform` vectorized representation](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Core/Public/Math/TransformVectorized.h)
  keeps translation, quaternion and scale separate from animation/actor policy.

Adopt immutable shared definitions, reference-pose initialization, prepared
bone/channel associations and a retained pose buffer. Preserve Bullet Witch's
row-vector matrices and authored inverse bind data instead of copying Unreal's
coordinate or gameplay conventions. Float/half decode and validation belong at
clip preparation; the runtime evaluator should not reread archive metadata on
every bone. Sorted-channel search gives bounded random-seek cost without
replaying from the first key. Per-instance cursors or batched SIMD may replace
that search if measured animation workloads justify them.

Both MCP design corpora were queried for lifetime, interface, skeletal data
layout and batching. Core I.11 makes immutable asset borrowing distinct from
owned prepared data; performance CDSA.32 puts conversion in preparation, and
CACHE.4 chooses contiguous transforms because this consumer uses quaternion,
translation and scale together. Implementation queried both corpora again and reread those rules plus Core
SL.con.3 for validated scalar/channel extents before conversion.

The implemented evaluator separates clip preparation from instance sampling.
It retains exact keys and authored constants, binary-searches contiguous typed
time arrays, samples translation/scale linearly and quaternion shortest-path
spherically, blends local transforms before hierarchy evaluation, and prepares
one parent-before-child order. Pending buffers prevent invalid input from
publishing a partial pose. Skin matrices compose inverse bind with the completed
world bone; attachment consumers use the world bone directly.

Authored in-process cases exercise a child stored before its parent, missing
tracks, separate and whole-transform channel order, antipodal and half-turn
quaternions, repeat/clamp boundaries, transition blending, owned prepared data,
invalid-input rollback and allocation-free evaluation/complete domain retirement.
Actual PW0 corpus sampling covers all 161 clips at five times; it is numerical
and bounds evidence, not proof of visible animation or original numerical parity.

The first native actor run exposed a mistaken interpretation of separate-track
constants: scale and translation had been exchanged. Finite-pose tests did not
catch that semantic error. Inspection of the authored neutral clip and original
sampler corrected the order to S/Q/T (+4/+16/+32), with channel pointers S/Q/T
at +44/+48/+52; whole-transform keys remain Q/S/T. The corrected native render
shows Alicia in the original stage. The runtime model's authored Y half-turn and
weapon's separate local half-turn were also recovered before correcting facing;
neither correction is a visual guess. See
[actor bindings](alicia-animation-bindings.md) for exact source witnesses.

Private diagnostic source, commands, output logs and native captures remain
under ignored `research/evidence/reconstruction/actor/`. No original model,
animation or shader bytecode is checked into this repository.

## Measured CPU baseline

Apple M3 Max, native ARM64, Apple clang 17.0.0, evaluator compiled with
`-O3 -DNDEBUG -std=c++20`. The diagnostic loads actual PW0 assets before timing,
prepares each clip once, warms up 1,000 evaluations and runs five batches of
20,000 evaluations at explicit advancing 60 Hz sample times. Each evaluation
samples all 125 bones and composes local, world and inverse-bind skin output.
Idle measured 4.66–4.79 microseconds per pose; walk measured 4.84–4.97
microseconds. Retained domain bytes stayed unchanged, and teardown returned live
bytes to zero. The public contract test separately verifies no backing allocation
during evaluation.

These are single-instance CPU measurements with prepared data, excluding file
loading, game logic, draw-palette copying and GPU rendering. They justify keeping
this bounded baseline while reconstructing behavior; they do not establish many-
actor scaling or prove explicit NEON/SSE parity. The implementation remains
portable typed arrays and equations, with compiler optimization enabled in this
diagnostic. Batch layout and target SIMD should be measured when representative
multi-actor workloads arrive. Reproduction source and logs are
`research/evidence/reconstruction/actor/pose_benchmark.cpp` and
`pose-benchmark.log`; compile that source together with `src/animation/pose.cpp`
at the stated options and link the existing resource/memory/native-backing
libraries. No third-party animation implementation was copied.

## Verification result

`python3 scripts/check.py all` passed after integration: pinned formatting, host
build and all six test executables, fatal clang-tidy across the authored C++
translation units, repository policy tests and content guard. The existing
`startup-corpus` scenario also passed with selected-player admission, retained
palette updates, unchanged residency during advancement, scene retirement and
mission cancellation returning CPU domain bytes to zero. No integration scenario
was added. All 161 PW0 clips prepared and sampled at five times after the channel-
order correction. The native costume probe additionally rendered all six
costumes through production mission/scene/Metal owners for 90 frames each;
[actor bindings](alicia-animation-bindings.md) records draw counts and scope.

This closes the actor-presentation portion of the authorized slice. Original
Runner movement, input, live camera, floor/wall contact and gameplay jump-state
execution remain prerequisites for completing slice 5. No interactive host was
launched during this subtask.
