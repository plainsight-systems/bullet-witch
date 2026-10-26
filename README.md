# Bullet Witch

![Bullet Witch's animated title screen running in the native alpha macOS build, with the START prompt and Alpha · macOS label.](https://media.plainsight-systems.com/bullet-witch/title-screen-2026-10-24-0dbf54587d10.png)

Native alpha build on Apple M3 Max · Metal · captured October 24, 2026.
[Capture details](research/reports/2026-10-24-public-demo.md#published-still).
Game imagery remains the property of its rights holders and is not covered by
this repository's licenses.

This is an unofficial C++20 engine reconstruction of Cavia's Bullet Witch,
running natively on Apple silicon with Metal. Cavia is the studio behind
Drakengard and the original NieR. Bullet Witch first released on Xbox 360 in
2006; the Steam PC release followed on April 25, 2018.
[Release sources](research/reports/2026-10-24-public-demo.md#release-facts).

I want to get Bullet Witch running on Xbox Series X, then be able to work on
it: preserve how the game plays, improve the graphics, and change the engine
where there is a reason to. Having readable source makes that possible.

This project reconstructs the engine from the Steam PC release in C++20.
The original executable, Lua scripts and game data provide the evidence for
how it works. The destination is a faithful port to Series X in Developer
Mode, with improvements built on top of that baseline. Development currently
runs on an Apple silicon Mac.

**Requires purchased game data.** No game assets, extracted scripts or raw
decompiler output are included in Git. Research findings are written in my
own words. This project is unaffiliated with the game's rights holders.

## Get it locally

The current native build is verified on macOS with Apple silicon. You need
Git, Python 3.9+, CMake 3.24+, C/C++ compilers with C99 and C++20 support,
Xcode’s SDK, the Metal toolchain and `make`.
[Tooling setup](scripts/README.md) covers those dependencies. The build downloads
its dependencies from checksum-pinned source archives.

```sh
git clone https://github.com/plainsight-systems/bullet-witch.git
cd bullet-witch
```

You supply your own purchased Steam installation. Game assets are not included
in this repository.

## Run locally

From the repository root:

```sh
./run
```

This builds the current Mac host and launches fullscreen. It uses your purchased
installation at `~/Games/BulletWitch-Steam`. To use another location, set
`BULLET_WITCH_GAME_DIR` or run `./run --game /path/to/BulletWitch-Steam`.
Use `./run --windowed` for a window, or `./run --build-only` to build without
launching. Native host options pass through; see the
[host notes](docs/native-startup-host.md) for controls and verification options.
Use `./run --texture-mips on` to enable the original texture mip option. It is
off by default; changing it requires restarting. See the
[texture notes](research/engine/subsystems/lighting-texture-mip-generation.md).
For recording, use the same launcher with diagnostics redirected:

```sh
./run --capture
```

Capture mode keeps fullscreen presentation, game audio and the ordinary 60 Hz
host cadence. It does not skip loading, accelerate playback or change gameplay.
Diagnostics go to the ignored `research/evidence/capture/latest.log` rather
than the terminal. The host's pacing target is not a claim of sustained 60 FPS
in every scene. See the
[capture notes](docs/native-startup-host.md#recording-a-demonstration) for preparation
and current audio limits.

For static-stage inspection, `./run --inspect-camera X Y Z YAW PITCH` sets an
explicit camera position and angles in degrees. Omitting it uses the recovered
starting camera. The explicit camera option keeps a fixed development view.

In Concept Mission → One Shot Kill, the bounded traversal build uses these
controls. Full mission behavior is still being reconstructed.

| Action | Xbox controller | Keyboard / mouse |
| --- | --- | --- |
| Move | Left stick | W/A/S/D |
| Look | Right stick | Mouse movement |
| Evasive leap | LT | Space |
| Crouch/stand | Left stick click | Left Shift |
| Fire | RT held | Left mouse held |
| Reload | X | R |
| Melee presentation | A | Right mouse |
| Switch weapon | B | Mouse wheel down / up (forward / backward) |
| Aim | Right stick click | E |
| Open/page magic ring | Tap LB/RB | Middle mouse opens; wheel changes page |
| Ring choice: right/bottom/left | B/A/X | C/X/Z |
| Cancel ring | Y or right stick click | Middle mouse or E |
| Pause/resume | Menu | Escape |

These are the original default bindings; Controller Configuration can rebind
them and change camera inversion or sensitivity.

Release the shoulder button after opening the ring, then tap again to page.
Holding one shoulder while pressing the other closes it. Face buttons select
spells directly; the sticks and D-pad do not navigate the ring. Wait for its
opening/page transition to settle before selecting.

The normal pause menu uses the original Help, Options, Return, Stage Start,
Retire and Exit artwork. Options includes live music/effects volume, original
controller configuration, brightness calibration and in-session costume changes.
Back offers to discard edited settings; Exit follows the autosave/save choice.
Stage Start rebuilds the scene. Return currently restarts the checkpoint-zero
scene; restoring captured world checkpoints awaits actual world startup.

Developer diagnostics remain separate from normal pause. Their interface and
activation controls are pending design; no developer shortcut or overlay is
admitted in the normal build. Build-time separation is retained independently
of capture mode.

B or the mouse wheel switches weapons with the original change
animation, retaining body movement and aiming state; the paused Weapon Class
selector resets presentation to neutral. Both preserve position and require
Alicia to be grounded. Ammunition and magic are live resources: firing consumes rounds and reloads
spend magic, which recovers to its current ceiling while unpaused. Hits,
projectiles, spell effects and combat outcomes are not simulated. Spell requests
report missing target or world prerequisites rather than pretending to cast.
See the [animation review checklist](docs/animation-review.md) for exact controls,
input sequences, expected results and the boundary between live controls and
explicit presentation previews.

## Where it stands

The resource readers handle the installed game's archives and catalogs, and
the original startup Lua runs through the reconstructed task and script
runtime. Its startup tasks connect to shared movie playback and a native Mac
host using Metal and AudioQueue. Shaders live in separate source files and compile with the build, so the graphics implementation
can grow and be tuned independently of the host code.

On an M3 Max, the native Metal build runs the splash logos, opening cinematic
and animated title screen using the original assets. It loads profile data
and displays interactive menus and costume selection. The original Lua
orchestrates screen flow; reconstructed C++ implements the menus and carries
their selections back to Lua. Front-end costume previews use the game's authored
2D images. One Shot Kill now opens its original mission script and completes its
resource-loading sequence. The scene uses ordinary stage and prop models,
original textures, lighting,
shadows and reconstructed shaders. Selected-costume skeletal animation, a live
camera and custom character movement now support bounded traversal.
Mission behavior, combat, outcomes and some menu options remain open. The [startup notes](docs/startup-runtime.md) and
[native host notes](docs/native-startup-host.md) describe what runs and what
remains; the [implementation plan](IMPLEMENTATION.md) follows that work through
a playable mission, the complete game and the Xbox port.

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

## Development checks

To build the libraries, resource inspector, Mac host and tests, then run the
tests:

```sh
python3 scripts/check.py host
```

These checks do not need game data. The inspector is written to
`build/host-debug/bw-resource`; [resource inspection](docs/resources.md)
explains how to use it with your purchased installation.

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

Captured game imagery and audio remain the property of their respective rights
holders and are not covered by this repository's licenses. Demonstration media
stays outside Git.

This is an unofficial project, unaffiliated with the game's rights holders.
Users supply their own purchased game; its code and assets are not included
or licensed by this repository.
