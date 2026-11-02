# Working on the rebuild

## Purpose

We are remastering Bullet Witch, not porting it. The target is the game its
makers were aiming at: its look, feel and systems realized without the
limits of 2006 console hardware and the shortcuts those limits forced.

- The original executable, scripts and assets tell us what the game is.
  Shipped behavior is evidence of intent, not the specification.
- Where the shipped implementation is a workaround for its hardware, such as
  a frame cap, a draw budget, a texture size or a simplified simulation, the
  remaster drops the workaround and keeps the intent.
- How a subsystem is built comes from modern engine practice: Unreal Engine
  at the pinned commit first, and other current engines, talks and papers
  where they do better. Where a design follows a source, cite it. The
  original's engine is never that source: it tells us what the game contains
  and does, not how to structure ours.
- Scope is set by what the game needs, improved. Argue a cut by "the game
  never reaches this", never by "the 2006 game did not have this".

Targets, in build order:

- macOS, native, Apple silicon, Metal.
- Linux, native, NVIDIA GPU, Vulkan.
- Windows, native, NVIDIA GPU, Direct3D 12.
- Xbox Series X, UWP sideloaded through Dev Mode, AMD silicon (Zen 2 CPU,
  RDNA 2 GPU), Direct3D 12 inside the Dev Mode sandbox.

Three rendering backends behind one RHI boundary, built in that order; the
Windows backend is the Xbox backend. This is a community project for a game
whose rights we do not hold: nothing is published to a store, and the Xbox
target lives within what Dev Mode allows.

Read [docs/architecture/overview.md](docs/architecture/overview.md) and
[docs/architecture/source-layout.md](docs/architecture/source-layout.md).

- This is new source. Do not copy, call, wrap, link or reuse code from the
  earlier Bullet Witch reconstruction.
- Purchased game data and the original Lua are read-only inputs. Nothing
  purchased, generated or downloaded goes in Git.
- Size every subsystem by what the game runtime needs. Inside that subset,
  follow the Unreal reference's behavior, ordering and failure handling, and
  write each omitted pinned step down beside the code. Do not reproduce
  Unreal's build matrix, macro seams, hidden base members or unused overloads.
- Keep proposed design, implemented behavior and verified behavior distinct in
  what you write.
- Do not commit, push, or launch the game without the user's word.

## Architecture documents

`docs/architecture/` describes the system as it is. One document per
system, named for the system it covers in full: `shaders.md` is the whole
shader system, and a document called `rendering.md` would have to cover the
whole renderer.

- Content is invariants and flows: what is always true, what moves where,
  who owns what. Diagrams over prose; tables over paragraphs; short.
- No plans, phases, rollback, verification lists, alternatives considered or
  decision records. Git history is the record. Slice plans live untracked
  under `research/slices/`.
- No pin citations or other implementation detail. Those live beside the
  code.
- No status words. A document that says what is done, pending or next is a
  plan, not architecture.

## Recurring mistakes

Entries here are errors an agent or the user has caught more than once. Read
them as rules. Add an entry when the same mistake comes back.

- **Treating the shipped engine as the architecture input.** The original is
  a 2007 in-house engine built for one game. Its render passes, memory layout,
  threading and internals are not design input for the remaster, and tracing,
  decompiling or mirroring them is not research we do. The original is read to
  answer "what does the game contain or do": its assets and their formats, its
  scripts, its behaviors, its look. How a subsystem is built comes from
  modern engine practice, Unreal at the pin first, never from the shipped
  engine.

## Reference

- Unreal pin: `f45729c0b6f097038e72b52d366a62baeca3275b`, labeled "5.8.4 release".
- Pinned checkout: a blob-less fetch of the pin alone with no working tree,
  at the `unreal_checkout` path in `research/local.md` in the main checkout.
  That file is untracked and machine-local; no absolute path of this machine
  goes in a tracked file. Read one file at a time with
  `GIT_TERMINAL_PROMPT=0 git -C <unreal_checkout> show <pin>:Engine/Source/<path>`.
  Never check it out, fetch another ref or widen it.
- Citation in code: `// Unreal <pin> Engine/Source/<path>:<start>-<end>`,
  the path from the Unreal repository root. For any other source:
  `// Source: <title>, <author or venue>, <year>, <url or section>`.
- Original game data: the `game_data` path in `research/local.md`,
  read-only.
- Slice records are not tracked. The plan for a slice is
  `research/slices/<slice>/plan.md` in the main checkout, which is the first
  entry of `git worktree list`; a worktree has no `research/`.
- Roles: the project session writes the plan; a fresh session per slice runs
  `/architect <slice>`, which uses the `researcher`, `implementer` and
  `reviewer` agents under `.claude/agents/`.

## Testing

Two kinds of test. A unit test needs nothing but the harness and the unit under
test; it lives in `tests/unit/<owner>/`. An integration test may need the OS, a
thread, a child process, a file or another module; it lives in
`tests/integration/<owner>/`.

Both kinds assert public contracts only: the invariants the public headers and
the architecture documents state. The test of a test: if the implementation
changes and no invariant changes, the test must not need to change.

- Before writing or keeping a test, name the header line or architecture
  statement it asserts.
- No test target gets a private include directory.
- Do not assert what a one-line wrapper forwards to: an intrinsic, a libc or
  std function, an OS call. That tests the platform, not our code.
- Do not assert internal structure, locking strategy or algorithm steps unless
  the header promises them.
- Logic that is only reachable through an OS-bound path belongs in its own
  pure function with a unit test; the integration test then keeps one probe
  that the path is wired.
- Prefer one assertion per invariant over scenario sprawl.
