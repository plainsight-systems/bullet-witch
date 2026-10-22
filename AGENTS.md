# Working on Bullet Witch

Read `docs/process/MEMORY.md`, `docs/process/QUEUE.md`, and
`docs/process/workflow.md` at session entry, then the relevant architecture and
source. `CLAUDE.md` points to the same instructions.

- Follow the self-contained workflow. Do not import governance repositories,
  submodules or symlinks from DoLittle.
- Name engine types, functions and files by responsibility. Keep opaque original
  identifiers at compatibility/provenance boundaries, with the naming map in
  `docs/architecture/file-mapping.md`; do not invent acronym expansions.
- Design architecture and file contracts before implementation. The architecture
  set is the working structural baseline. Detailed file/behavior contracts
  still need refinement; documented modules are not automatically implemented.
- C++ header design and implementation must each query both `cpp_guidelines`
  and `cpp_performance` MCP corpora. Retrieve full applicable rules and explain
  their application in opening file comments; follow the unavailable-server
  rule in the workflow. Do not cite unread rules.
- Run `python3 scripts/check.py all` for C++ changes. Use the pinned Google
  formatter and fatal clang-tidy policy; fix findings instead of disabling
  check families. Report only verification actually performed.
- Keep the game installation read-only. Research, recovered material and
  generated diagnostics belong in organized `research/` subdirectories.
  Generated evidence and proprietary assets stay ignored. Preserve repository
  content guards and do not force-add assets or unreviewed evidence.
- Keep current decisions in MEMORY and remaining work in QUEUE. Keep build logs
  and implementation diaries under ignored `research/evidence/`, not in those
  current-state documents. No machine-specific paths or credentials in new docs.
- Unit tests exercise public contracts in process. Integration tests launch
  processes or use real files/components. See the explicit existing integration
  inventory and adoption exception; do not hide scenarios by relabeling them.
- Independent agent review is on request. Do not start rendering or new engine
  features as incidental tooling work. Commit, publish or message externally
  only within the user's instructions.
