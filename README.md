# Bullet Witch

A new implementation of Bullet Witch's engine and game runtime, using Unreal
Engine's architecture and established algorithms as the reference. Purchased
original game data is required. This branch currently contains architecture
and process documents only; it does not contain a runnable game.

## Get it locally

The `codex/unreal-rebuild` branch is being prepared in a separate worktree.
It has not been committed or published yet. Once published, its checkout and
build instructions will be added here; there is no working download command
for this branch today.

## Run

There is no executable or run command yet. The first proposed implementation
slice is a standalone paced engine loop, with a configurable default limit of
60 frames per second. It does not load assets or render gameplay.

## Architecture

Start with [the runtime map](docs/architecture/README.md) and
[implementation order](docs/architecture/implementation-order.md).
Every code module in the rebuild will be written anew. Existing Bullet Witch
project code is not a dependency, including its lower-level services, imports,
shaders, and tests. Original assets and Lua remain external inputs. Third-party
libraries will be selected explicitly; none are inherited from the old build.

## Content and licensing

No game assets or raw decompiler output belong in Git. Findings are written in
our own words. New behavior must connect to inspected reference evidence and
its documented adaptation.

The existing [split license](LICENSE), [code license](LICENSE-CODE),
[content license](LICENSE-CONTENT), and [NOTICE](NOTICE) are preserved verbatim.
The dependency descriptions in that preserved NOTICE describe the prior
project; they do not mean this documentation-only branch contains those
libraries or build files. Dependency notices will be reconciled as new choices
are made. Unreal is a reference, not bundled source or a linked dependency.
