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
- How a subsystem is built comes from modern engine practice: engine source
  we can read at a pinned revision, and current talks, papers and vendor
  documents. Each is a source we learn from and depart from when the game
  needs it, never a specification. The original's engine is never a source:
  it tells us what the game contains and does, not how to structure ours.
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
- Size every subsystem by what the game runtime needs. Inside that subset, a
  slice follows its sources' behavior, ordering and failure handling and
  records each step it leaves out; see Sources and citations. Do not
  reproduce a source's build matrix, macro seams, hidden base members or
  unused overloads.
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
- No source citations or other implementation detail. Sources live in the
  slice records.
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
  modern engine practice, never from the shipped engine.

## Sources and citations

A source is anything a design follows: engine source at a pinned revision, a
paper, a talk, a vendor document. Every source is treated the same way.
Citations exist for review and do not ship in the tree.

- A slice plan names each source it follows: a source tree with its revision,
  or a paper, talk or document with its year and location. No other file
  lists sources.
- A pinned source tree is read from a blob-less fetch of that revision alone,
  with no working tree, at the path under the tree's key in
  `research/local.md` in the main checkout. That file is untracked and
  machine-local; no absolute path of this machine goes in a tracked file.
  Read one file at a time with
  `GIT_TERMINAL_PROMPT=0 git -C <checkout> show <revision>:<path>`, and
  `ls-tree` to list paths. Never check it out, fetch another ref or widen
  it: `git grep`, `git log -p` and `git diff` against it fetch blobs and
  are not run.
- Citation form, the same for every source:
  `// Source: <source>, <revision or year>, <path or section>:<lines>`.
- In a slice worktree, every type and function that follows a source carries
  a citation in that form, and every step of a source left out has a note
  beside the declaration saying why the game does not reach it. The reviewer
  reads the cited source, not the citation.
- When the user accepts the slice, and before it is committed, every citation
  and omission note leaves headers, sources, tests and build files, whatever
  the source. A comment that explains what the code does or why stays, in
  our words, naming no source: a platform fact is stated as the fact, not as
  where it was read.
- The citations and the omission list survive in the slice's plan under
  `research/slices/<slice>/`, untracked, with the research.
- Committed code, build files and `docs/` name no source. Before every
  commit, `git grep -n -P '\bSource:' -- src tests tools cmake docs third_party`
  is empty, and so is a case-insensitive grep over the same paths for the
  name of each source the slice plan lists, like the machine-path grep.

Why: an agent reading the tree designs from this project's code and its
architecture documents. A source named beside the code pulls the next change
toward that source where we would depart.

## Slices

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

- Before writing or keeping a test, name what it asserts: a header
  declaration, written `(<header>, <identifier or invariant number>)`, or an
  architecture statement. Never a line number; lines move.
- No test target gets a private include directory.
- Do not assert what a one-line wrapper forwards to: an intrinsic, a libc or
  std function, an OS call. That tests the platform, not our code.
- Do not assert internal structure, locking strategy or algorithm steps unless
  the header promises them.
- Logic that is only reachable through an OS-bound path belongs in its own
  pure function with a unit test; the integration test then keeps one probe
  that the path is wired.
- Prefer one assertion per invariant over scenario sprawl.
- A scenario is written only after its author names the change that this
  scenario alone would catch. If a compile error, another scenario or
  reading a one-line body catches it, the scenario is not written. One
  table-driven scenario per invariant; never one executable per function.
- A pure function pulled out of an OS-bound path is a row in the owning
  module's existing test executable. A new executable is for a new public
  header, not a new function.
- Every test executable is a scenario table on
  `tests/support/bw/testing/harness.hpp`. No test defines its own `require`
  or `main`; a worker-thread check uses a local helper that exits 1.
- A slice reports net lines of the test tree against net lines of the code
  tree, whole trees against HEAD, measured by the architect, not taken from
  an implementer's report. The ratio is expected to fall as plumbing ends;
  a slice where it rises says why per scenario.
