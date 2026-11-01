# What we are building

A native port of Bullet Witch for modern platforms, macOS on ARM64 first, built
as new source. The original game's executable, scripts and assets are the
evidence for what the game does. Unreal Engine's source, at one pinned commit,
is the guide for how an engine subsystem should behave when the original's
behavior is unclear or when we need a design we know works.

## The two rules that matter

1. **Build the subset the game needs.** Every subsystem is sized by what the
   Bullet Witch runtime calls, not by what Unreal offers. Editor, server,
   tooling and multi-platform scaffolding are out unless the game reaches them.
2. **Inside that subset, follow the guide.** Where Unreal is the reference for a
   mechanism, its behavior, ordering and failure handling are followed as
   written, with no standard-library stand-ins for its types. Where the subset
   leaves a pinned step out, the omission is written down next to the code.

Neither rule is a reason to reproduce Unreal's build matrix, macro seams, hidden
base members or unused overloads. If a review finds a difference nothing in the
game executes, it is recorded as an omission, not built.

## What the runtime is made of

See [source layout](source-layout.md) for the destination tree, module
ownership and dependency direction. In one line: platform adapters under
`src/platform`, engine systems under `src/engine`, Bullet Witch's own rules
under `src/game`, and development tools apart from all three.

## Tooling we want

- **Headless simulation.** The production world, AI, navigation, movement,
  animation and physics running with no window and no 3D renderer, driven by
  scenarios, so behavior can be tested and compared without a GPU.
- **Headless render.** The renderer producing frames to files from the same
  production modules, for image comparison against captures of the original.
- **2D simulation inspector.** A separate client that attaches to a headless run
  or the full game through one seam, draws the published runtime state in two
  dimensions, and submits admitted commands for scenario authoring.
- **The game.** The same modules under a window with input and audio.

All four are compositions of one runtime. None gets its own simulation or
rendering algorithms.

## Inputs and boundaries

- Purchased game data is read-only input and never enters the repository.
- Nothing from the earlier Bullet Witch reconstruction code is reused.
- Third-party libraries are chosen explicitly, one at a time, when a subsystem
  needs one.
- Research about the original game lives in the main checkout's `research/`
  folder: the engine map from the decompilation, the recovered scripts, and the
  Steam unpacking work.
