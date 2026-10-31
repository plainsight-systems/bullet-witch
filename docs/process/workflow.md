# Implementation workflow

This worktree is independent of the old implementation. The only transferred
files are LICENSE, LICENSE-CODE, LICENSE-CONTENT, and NOTICE, kept verbatim.

For a slice, read the relevant pinned Unreal functions, state the algorithm and
owner contracts, then implement new code. Preserve Lua consumption as a data
contract, not as a reason to retain an old interpreter or controller. Third-party
libraries require an explicit selection and version/license record; a prior
project dependency does not select one for this build.

Plan slices around a coherent, usable capability. Review its full dependency
scope before proposing the slice, and include predictable supporting prerequisites
up front instead of splitting individual functions or constructors into separate
review cycles. Preserve architecture → headers → comments-only plans → tests →
implementation approval gates; a larger coherent scope does not waive them. If an
unexpected dependency changes the approved scope, explain the concrete change
before expanding it.

Reference correspondence is visible in implementation comments. Each method and
significant call/block identifies the pinned source function and lines and its
algorithm step, in our own words. Write concise reference pseudocode beside the
implementation. Label platform substitutions, extra safety/lifecycle contracts
and extracted helpers as ours; do not attribute them to Unreal or rely on a
blanket file-level claim. Do not invent a different policy under a reference
label. Report a necessary algorithm departure before implementing it.

Design headers before implementation. During both activities query the
`cpp_guidelines` and `cpp_performance` MCP servers, retrieve applicable full
rules, and explain their application concisely in opening file comments. If a
server is unavailable, stop the affected C++ design/implementation pending user
direction and report the unavailable check; do not fabricate citations or claim
it passed. Implementation comments identify algorithm stages rather
than duplicating every line or embedding a work diary.

The first code slice establishes fresh build/test/format/lint commands. Fatal
clang-tidy and a pinned formatter are required. Until those tools exist, do not
claim an old check script applies here. Tests should exercise independent
behavioral contracts and meaningful failure cases. Native verification must
cover the behavior claimed; compilation, counts, and selected screenshots do
not establish gameplay correctness or performance.

Report what changed and what was actually verified. Keep architecture current,
current decisions in MEMORY, and next work in QUEUE. Store generated evidence
outside Git under organized `research/evidence/` paths. Do not import old test
helpers or generated assets as fixtures. Keep purchased content read-only.

Delegate execution with explicit file ownership. Main-chat review resolves
changes in direction. Commit and push only when authorized; do not modify the
preserved old worktree. No visible game launch without advance notice.
