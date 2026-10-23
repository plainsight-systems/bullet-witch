# Bullet Witch

I want to get Bullet Witch running on Xbox Series X, then be able to work on
it: preserve how the game plays, improve the graphics, and change the engine
where there is a reason to. Having readable source makes that possible.

This project reconstructs the engine from the Steam PC release in C++20.
The original executable, Lua scripts and game data provide the evidence for
how it works. The destination is a faithful port to Series X in Developer
Mode, with improvements built on top of that baseline. Development currently
runs on an Apple silicon Mac.

## Why this approach

The project started with static recompilation in mind. That could carry the
existing engine onto another machine, but I want to understand and work on
what comes across. Source reconstruction gives the systems names, ownership
and interfaces that can be read and changed, while keeping their behavior
traceable to the original.

The Steam release gives the work a concrete starting point. Its scripts
describe startup, missions and parts of the AI, but they call into native
engine code to do much of the work. Following those calls connects a script
to the resources it needs, the tasks that advance it, and eventually the
animation, physics, sound and rendering that make it visible.

That is how the engine is being built, one connected part at a time. The
archive reader used by the inspection tool is also the reader used by the
runtime. The task and Lua systems that drive startup will support later game
behavior. Platform-specific graphics, input and media belong behind their
own interfaces, so a port can change those implementations while preserving
the game logic.

The order matters to me. A working version of the original gives later
changes something to be compared against. Otherwise a graphics improvement
and a reconstruction mistake can arrive in the same frame, and it becomes
harder to tell which one changed the game.

This is R&D under [Plainsight Systems](https://plainsight-systems.com/) LLC.

## Where it stands

The resource readers handle the installed game's archives and catalogs, and
the original startup Lua runs through the reconstructed task and script
runtime. Its startup tasks connect to shared movie
playback and a native Mac host using Metal and AudioQueue. Shaders live in
separate source files and compile with the build, so the graphics implementation
can grow and be tuned independently of the host code.

There is no playable build yet. The Mac build runs the logos, opening cinematic
and animated title screen using the original assets. Character loading and the
interactive menu come next. The [startup notes](docs/startup-runtime.md) and
[native host notes](docs/native-startup-host.md) describe what runs and what
remains; the [implementation plan](IMPLEMENTATION.md) follows that work through
a playable mission, the complete game and the Xbox port.

## Build locally

The current native build is verified on macOS with Apple silicon. You need
Git, Python 3.9+, CMake 3.24+ and C/C++ compilers with C99 and C++20 support.
The build downloads checksum-pinned Lua 5.0.2 and FFmpeg 7.1.1 source archives.
The Mac host also needs Xcode’s SDK, the Metal toolchain and `make`;
[tooling setup](scripts/README.md) covers those dependencies.

```sh
git clone https://github.com/plainsight-systems/bullet-witch.git
cd bullet-witch
python3 scripts/check.py host
```

This builds the libraries, resource inspector, Mac startup host and tests, then
runs the tests.
It does not need game data. The inspector is written to
`build/host-debug/bw-resource`; [resource inspection](docs/resources.md)
explains how to use it with your own purchased Steam installation.

For the full development checks, provide clang-format and clang-tidy
**21.1.8** and run:

```sh
python3 scripts/check.py all
```

That adds formatting, static analysis and repository-content checks. See
[tooling setup](scripts/README.md) for tool discovery, sanitizers and checks
against the installed game.

## Find your way around

| Path | What it holds |
| --- | --- |
| `include/bw/`, `src/` | Shared engine contracts and their C++ implementations |
| `src/game/` | Bullet Witch's script bindings and game-specific behavior |
| `src/framework/`, `src/platform/` | Shared application composition, target entry points, native backends and shaders |
| `tools/` | The resource inspector and repository-content guard |
| `tests/` | Contract tests and integration checks |
| `scripts/`, `cmake/` | Build, formatting, analysis and dependency setup |
| `docs/architecture/` | The engine design, ownership and source boundaries |
| `docs/process/` | Current decisions, work queue and development workflow |
| `research/` | Original-engine maps, dated investigations and research tools |

There are two useful ways into the design. The
[original-engine map](research/engine/ARCHITECTURE.md) explains what the
research has established about the game. The
[reconstruction architecture](docs/architecture/README.md) explains how that
behavior fits into the engine being built here. The
[research index](research/README.md) connects the subsystem notes and their
evidence.

Game assets, extracted scripts and raw decompiler output stay outside Git.
The public repository contains the implementation, research tools and written
findings. Some research links point into local evidence that a clone will not
contain.

## Working on the project

Start with the [current decisions](docs/process/MEMORY.md),
[work queue](docs/process/QUEUE.md) and [workflow](docs/process/workflow.md).
Each C++ file begins with its responsibility, ownership and algorithm. New
behavior needs a connection back to the evidence; unresolved behavior stays
identified until it can be established.

Enable the repository's commit and push checks in a new checkout:

```sh
git config core.hooksPath .githooks
```

## Credits and license

The original game is Cavia's work. Community investigations, including Rich
Whitehouse's Noesis reader, provide useful references alongside the local
analysis. The [source notes](research/sources/community-reverse-engineering.md)
record those contributions, and [NOTICE](NOTICE) credits the runtime and
research tools used here.

Project code and configuration use Apache 2.0; writing, diagrams and original
artwork use CC BY 4.0. See [LICENSE](LICENSE) for the split. Third-party
components retain their own licenses.

This is an unofficial project, unaffiliated with the game's rights holders.
Users supply their own purchased game; its code and assets are not included
or licensed by this repository.
