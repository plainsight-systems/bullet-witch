# Engineering workflow

This is a self-contained adaptation of DoLittle's workflow for source
reconstruction. Engine architecture is described in `docs/architecture/`; the
implemented resource foundation is described in `docs/resources.md`. Preserve
explicit distinctions between recovered evidence, inference, proposed design
and implemented behavior.

## Design and implementation

1. Establish the logical architecture, change axes, execution contracts and
   file mapping before extending the engine. A review draft is not approval.
2. During C++ header design, consult both corpora below. Write opening file
   contracts and the algorithm each file owns; review those before its body.
3. During implementation, consult both corpora again for the concrete work.
   Apply the rules, implement meaningful contract verification, and keep file
   comments accurate. Reuse consultation within one coherent phase rather than
   querying separately for every function.
4. Check correctness and cost, then update memory and the queue.

Use responsibility-based names for engine types, functions and files. Do not
carry opaque original-engine terms into the source architecture. Keep exact disk
signatures, extensions, record tags and script binding strings at compatibility
boundaries, and document the original-to-engine mapping in
`docs/architecture/file-mapping.md`. Do not guess acronym expansions.

File headers carry implementation design; there are no separate implementation
packets. Formatting-only passes preserve existing contracts and citations.

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
