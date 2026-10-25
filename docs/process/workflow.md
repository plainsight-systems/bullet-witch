# Engineering workflow

This is a self-contained adaptation of DoLittle's workflow for source
reconstruction. Engine architecture is described in `docs/architecture/`; the
implemented resource foundation is described in `docs/resources.md`. Preserve
explicit distinctions between recovered evidence, inference, proposed design
and implemented behavior.

## Design and implementation

1. Establish the logical architecture, change axes, execution contracts and
   file mapping before extending the engine. A review draft is not approval.
2. Before designing standard subsystem behavior, apply the established-implementation
   requirement below. During C++ header design, consult both corpora below. Write opening file
   contracts and the algorithm each file owns; review those before its body.
3. During implementation, consult both corpora again for the concrete work.
   Apply the rules, implement meaningful contract verification, and keep file
   comments accurate. Reuse consultation within one coherent phase rather than
   querying separately for every function.
4. Check correctness and cost, then update memory and the queue.

Delegate task execution to subagents by default.
Keep the main conversation available for planning and parallel exploration,
assign clear file ownership, and integrate the results before verification and
delivery. This does not authorize an independent review agent unless requested.

Use responsibility-based names for engine types, functions and files. Do not
carry opaque original-engine terms into the source architecture. Keep exact disk
signatures, extensions, record tags and script binding strings at compatibility
boundaries, and document the original-to-engine mapping in
`docs/architecture/file-mapping.md`. Do not guess acronym expansions.

Decompose the original game's engine ownership and layout as well as its
algorithms. Trace the actual persistent, session, mission, actor and platform
owners before choosing reconstructed boundaries; unknown Cavia class names do
not justify inventing an unrelated framework. When a bounded trace cannot
establish an ownership/interface contract, inspect the concrete pinned Unreal
implementation, adopt the applicable boundary, and record that inference. This
reference requirement applies to architecture decisions as well as local code.

File headers carry implementation design; there are no separate implementation
packets. Formatting-only passes preserve existing contracts and citations.

## Established implementations first

For standard engine problems, study mature implementations before designing a new
algorithm or extending an unproven one. This applies to clocks and frame pacing,
audio/video playback, streaming, memory, jobs, input, rendering and resource
lifetime. Appropriate references include Unreal source when accessible, Godot,
other established engines, specialist libraries such as FFmpeg, and platform
sample implementations. Choose references that actually solve the relevant
problem; an engine name or API summary is not evidence.

Read the concrete source paths and functions, pin the revision, and save a concise
comparison under the relevant `research/engine/` subsystem. Record the existing
algorithm, its relevant ownership/threading/timing contracts, what this engine
will adopt, and why any departure is necessary. Local reference checkouts and
source snapshots belong under ignored, organized `research/` directories; keep
reviewed findings and source links tracked. Never place references in the game
installation or silently vendor them into production code. Respect the source's
terms when incorporating code.

The first pass preserves recovered port behavior. Where implementation is
ambiguous, choose established algorithms, data layouts and cost models rather
than the simplest working loop. Fidelity does not justify avoidable repeated
work. Consider batching, data layout and CPU SIMD (ARM NEON / Intel SSE or AVX)
for CPU-bound work; this guidance does not mean GPU offload. Measure representative
native hot paths to select target-specific implementations. Acceptance of a
scalar routine for one port slice does not prescribe the architecture of larger
future animation workloads.

Preserve recovered CPU/GPU work placement for the direct-port baseline without
presenting it as recommended modern architecture. Broad repartition is a later
improvement pass after the working port. A measured performance blocker or a
demonstrated defect can justify a focused earlier exception: record the source
boundary, measurements, chosen change and native acceptance. Untraced paths stay
explicitly unresolved; do not extrapolate whole-engine parity from one consumer.
See `docs/architecture/world-renderer-reconstruction.md`.

Prefer a proven implementation or algorithm behind the project's portable/native
boundaries over inventing a new framework. Review known limitations as well as
successful behavior. Reference study supplements the required C++ MCP guidance;
neither replaces the other. If a reference is unavailable, use another suitable
primary implementation and record the limitation rather than proceeding from
assumptions.

When original source behavior remains ambiguous after a bounded trace, use a
concrete Unreal implementation to guide the unresolved part and continue. The
user explicitly authorized this reconstruction approach. Preserve verified
original inputs and contracts, document the inferred algorithm and its evidence
boundary, and validate the resulting consumer. Do not quietly promote the
inference to original parity or keep tracing indefinitely without implementing
a useful, reviewable boundary. A decision that changes the broader fidelity or
platform plan still needs user direction.

Resolve the algorithm before tuning the result. Reproduce the complete
input-to-state-to-visible-output path and locate its first violated boundary.
Use recovered source first; after a bounded trace, use a concrete pinned
established implementation for the ambiguous boundary and record the inference.
Do not accumulate sign, offset, clamp, tolerance, timing or special-case patches
without evidence. A failed correction reopens the diagnosis; isolated helper
tests do not establish integrated correctness.

Verification must exercise the behavior that matters in the running subsystem.
For media, that includes visible final frames, complete audio, clock progression,
buffering and drain. A decoded-frame count, EOF flag or passing mock must not be
presented as evidence of correct native playback. When a device run fails, compare
its measured behavior with the reference algorithm before adding another patch.

## Required C++ MCP consultation

Both header design and implementation must use both local servers:

- `cpp-guidelines`, exposed as `cpp_guidelines`: C++ Core Guidelines.
- `cpp-perf-guidelines`, exposed as `cpp_performance`: performance guidelines.

Use each server's `search_guidelines` for the actual concerns, then
`get_guideline` to read the full rules used. Design queries cover ownership,
lifetimes, interfaces and expected cost. Implementation queries cover actual
algorithms, bounds, layout, allocations, copies and synchronization. Performance
consultation is required even for work not initially labeled performance-sensitive.

Opening comments cite the corpus and exact retrieved ID, its application, and
the returned source reference when available. If a search yields no applicable
rule, record the topic and that result instead of choosing an unrelated rule.
Earlier design citations do not replace implementation consultation. Never
invent IDs, cite unread rules, or substitute memory or web advice for MCP queries.

If either server is unavailable, report which server and stop affected C++
work; obtain explicit user agreement before proceeding without it. Continue
independent work where possible. These instructions do not require a prompt
when both servers are available or when earlier authorization covers the work.

## Opening file contracts

Every new or changed C++ header and implementation starts with:

1. Responsibility and change axis, identifying draft mappings as draft.
2. Inputs, outputs, preconditions, postconditions, failure behavior, ownership,
   borrowed-view lifetime, invariants and relevant synchronization.
3. Ordered algorithm steps: what each step consumes and produces, and who uses
   that result. Reference the corresponding architecture or implemented contract.
   Explain enough locally that a reader need not reconstruct the algorithm.
4. Design and cost: allocations/copies, work scaling, ordering and consequential
   choices. Mark optimizations with `Optimization:` and distinguish derived
   costs from measured performance. Describe existing behavior, not future code.
5. Applicable retrieved rules from both MCP corpora, with their application.

Loading, resource decoding, data layout, dispatch, rendering and simulation are
performance-sensitive. Derive costs in bytes, records, dependencies, entities
or frames as appropriate. Avoid repeated setup work in hot paths. Measure
before claiming a performance improvement. Python tools are reviewed for their
own correctness; C++ corpus requirements do not apply to Python-only changes.

## Comments beside implementation

Keep the header-first contracts and algorithm design. As bodies are implemented,
carry the relevant intent into short comments immediately above meaningful
functions or blocks: for example, reserve pending budget, acquire native storage,
then commit or roll back. Explain the piece's purpose or a consequential ordering,
ownership or failure invariant. Apply the same approach when filling out headers,
including declaration groups and inline or template implementations.

Keep the full design in the opening contract. Local comments should be brief
signposts, not copied paragraphs, repeated rule citations or a narration of every
statement. There is no requirement to comment every function or branch. Lead with the
operation and its purpose. Native addresses, opaque shader IDs and recovered
handler names belong in the research/provenance mapping; they do not substitute
for explaining the implementation. Keep exact disk tags where they clarify a
format boundary, and retain the required guideline citations in file contracts.
Update or remove comments when the corresponding behavior changes.

## Verification

Use [the tooling entry point](../../scripts/README.md). Google formatting is
pinned to clang-format 21.1.8. clang-tidy 21.1.8 enables Google, Core Guidelines,
bug-prone, performance and analyzer families; every finding fails. Compiler
warnings are errors on all authored C++ targets. Pinned third-party Lua C is
built separately and sanitizer-instrumented; it is not claimed as project lint coverage. A necessary low-level
exception must name only the specific check and explain the bounded operation
beside it. Do not suppress whole families to make checks pass. Test fixture
literals are the sole directory-level exception.

Run `python3 scripts/check.py all` before committing C++ work. It checks the
working tree, including authored untracked files, without changing the user's
index. It does not claim to test a partially staged snapshot. Existing commit
and push hooks separately enforce staged/history content policy. No hosted CI
has been configured; a local passing check is not a claim of CI coverage.

Unit tests exercise isolated public contracts directly in process with small
in-memory inputs. Do not launch builds, subprocesses or application flows in
unit tests. Test outcomes, rejection, bounds, ownership and state transitions;
avoid assertions about private helpers or incidental operation counts. Exact
values are appropriate when part of a format or numerical contract.

Integration tests exercise real filesystem/process/component boundaries. Target
at most five distinct essential scenarios across the repository. **Adoption
exception, revised for the authorized startup implementation:** eleven scenarios
are retained (four CLI, five Git content policy, one real archive corpus, one
original-root startup integration). The added scenario covers the new
resource/task/Lua composition with the actual local chunk and fixture services;
it is explicitly integration coverage, not another unit test. This exceeds DoLittle's cap and is tracked
as debt, not represented as compliance. Add none until the inventory is reduced
or the budget is explicitly revised. Do not bundle unrelated workflows, rename
them, or delete valuable rejection coverage merely to change the count. See
[the inventory](../../tests/README.md).

For decoder changes, retain independent Python/reference evidence and repeat
corpus acceptance when output or interpretation could change. Do not alter an
oracle to fit a failure or relax limits without establishing the reason and
preserving the old evidence. Report test cases separately from assertion counts.
A host build or metadata comparison establishes neither rendering nor gameplay
correctness. Numerical and device checks will follow the implemented contracts
when those systems exist; untested platforms remain unverified.

## Durable records and publication

Keep current engineering decisions in MEMORY and remaining work in QUEUE.
Keep research organized under `research/` and generated diagnostics under ignored
`research/evidence/`. This deliberately adapts DoLittle's `.local/` convention
to this project's research layout. Recovered game data never belongs in Git.
Keep reproducible commands in tracked docs, not private paths or copied tool
output. Keep independent review on request. Tooling work does not authorize a
public article, commit, push, rendering milestone or draft architecture approval.
