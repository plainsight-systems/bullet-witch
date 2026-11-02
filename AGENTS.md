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
- Where the original's intent is unclear, Unreal Engine at the pinned commit
  is the guide for how a subsystem should behave.
- Scope is set by what the game needs, improved. Argue a cut by "the game
  never reaches this", never by "the 2006 game did not have this".

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
