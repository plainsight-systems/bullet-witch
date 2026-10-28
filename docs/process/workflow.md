# Implementation workflow

This worktree is independent of the old implementation. The only transferred
files are LICENSE, LICENSE-CODE, LICENSE-CONTENT, and NOTICE, kept verbatim.

For a slice, read the relevant pinned Unreal functions, state the algorithm and
owner contracts, then implement new code. Preserve Lua consumption as a data
contract, not as a reason to retain an old interpreter or controller. Third-party
libraries require an explicit selection and version/license record; a prior
project dependency does not select one for this build.

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
