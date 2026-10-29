# Working on the rebuild

Read `docs/process/MEMORY.md`, `docs/process/QUEUE.md`, and
`docs/process/workflow.md`, then the relevant architecture document.

- This is a new implementation. Do not copy, call, wrap, link, import, or reuse
  any existing Bullet Witch project code. This includes math, allocators, clocks,
  platform adapters, decoders, Lua bindings, controllers, rendering, shaders,
  tests, scripts, configurations, caches, and generated runtime resources.
- Original purchased game data and Lua are input data only. Keep the game
  installation read-only; no original assets, scripts, raw decompiler output,
  downloaded source, or generated evidence in Git.
- Unreal is the design and algorithm reference. Inspect pinned source functions
  before implementing the corresponding system. Record the selected algorithm
  and any adaptation in the architecture docs. Original engine structure and
  timing are not constraints on the replacement architecture.
- Put the inspected reference algorithm in comments beside each implementing
  method and each significant call/block. Include pinned commit, function and
  source lines, with concise own-word pseudocode that supports side-by-side
  review. Explicitly label our extra contracts, platform substitutions and
  extracted helpers; a file-level "source-backed" claim is insufficient.
- A diagram or a compiled class does not establish an implemented subsystem.
  Keep proposed design, implemented behavior, and verified behavior distinct.
- Write header contracts first. For both header design and implementation,
  query the C++ Guidelines and C++ Performance MCP corpora and read applicable
  full rules. Put concise rule applications in opening comments and algorithm
  intent above implementation code. Do not cite unread guidance.
- Establish fresh formatting, fatal clang-tidy, build, and meaningful test tools
  with the first code slice. Never invoke or copy old repository tooling.
- Use subagents for execution so the main conversation remains available.
  Keep file ownership explicit when work proceeds in parallel.
- Do not commit, push, launch a game, or expand an implementation slice beyond
  the user's authorization. Preserve the old worktree unchanged.
