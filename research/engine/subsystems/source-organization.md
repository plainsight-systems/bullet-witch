# Source organization: comparison and assessment

October 22, 2026. Reference inspection and implemented migration. The user selected the integrated
game/engine organization and target-owned entry points. The implementation
migration below records the concrete application of that decision.

## How the previous layout arose

The initial resource foundation (`3c85b91`) used a conventional C++ library split:
`include/bw/` for published headers and `src/` for implementations. The boundary
migration (`0948bbf`) introduced `src/platform/files/host.cpp` alongside format,
catalog and content implementations. That adapter uses standard C++ streams and
filesystem operations; it is not a Mac-specific implementation.

The native startup slice initially introduced root-level
`platform/macos/` and `app/` while retaining the original source organization.
This created two implementation roots and two meanings of `platform`. The
mechanism/backend separation was deliberate; the physical layout combines
conventions rather than following one inspected engine's organization.

Unreal was inspected for media subsystem responsibilities and algorithms. The
repository layout was not derived from Unreal. Describing this folder layout as
following Unreal would overstate the evidence. Calling root-level platform code
inherently unusual would also be wrong: Godot is a concrete counterexample.

## Primary comparison: a game and its engine together

The user clarified that Quake/Doom-style game-plus-engine projects are the
primary comparison, rather than standalone general-purpose engines. That is the
closer scope for reconstructing Bullet Witch. The broader comparisons below
remain context, not the recommended product model.

| Reference | Observed game/engine/platform arrangement |
|---|---|
| [Quake](https://github.com/id-Software/Quake/tree/bf4ac424ce754894ac8f1dae6a3981954bc9852d), `bf4ac424ce754894ac8f1dae6a3981954bc9852d` | The release has WinQuake and QuakeWorld trees. WinQuake commonly groups responsibilities by file prefixes rather than deep folders: host, renderer, sound, VM and platform implementations share the tree. The release also includes `qw-qc`. This is a game runtime with script/native boundaries, not an externally packaged engine SDK. Its largely flat physical layout need not be copied. |
| [Quake II](https://github.com/id-Software/Quake-2/tree/372afde46e7defc9dd2d719a1732b8ace1fa096e), `372afde46e7defc9dd2d719a1732b8ace1fa096e` | Root client/server/common/game/renderer/platform source families. `game/game.h` defines engine imports and game exports. `win32/sys_win.c` loads GetGameAPI, supplies WinMain, initializes Qcommon and calls Qcommon_Frame. The engine/game boundary is explicit within one product. |
| [Quake III Arena](https://github.com/id-Software/Quake-III-Arena/tree/dbe4ddb10315479fc00086f08e25d968b4b43c49/code), `dbe4ddb10315479fc00086f08e25d968b4b43c49` | One `code/` root holds qcommon, client, server, renderer, game, cgame, UI and OS directories. Common VM code includes interpreter and x86/PPC implementations, illustrating that CPU ISA selection and OS selection are separate axes. Client/server/game divisions reflect that game's requirements and should not be imported into Bullet Witch without need. |
| [Doom 3](https://github.com/id-Software/DOOM-3/tree/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo), `a9c49da5afb18201d31e3f0a429a037e56ce2b9a` | One `neo/` root holds framework, game, renderer, sound, UI, idlib and sys. `game/Game.h` defines versioned engine imports (system, filesystem, rendering, sound and other services) and exports an idGame interface. `sys/win32/win_main.cpp` owns the native entry and calls common->Frame. OS services live under one sys family. |

Doom 3 is the strongest structural reference among these for this project's
C++ game-plus-engine organization. This is a recommendation based on its concrete
source boundaries, not a claim that Bullet Witch uses id Tech. Preserve separate
engine mechanisms and Bullet Witch behavior in one source tree; do not make a
general-purpose editor/SDK product the organizing goal. There is no requirement
to copy the references' dynamic-library ABI or multiplayer architecture.

The thin entry-point role can remain, but `app/` is an optional folder choice.
Quake II and Doom 3 place their inspected Windows entries in platform code and
hand frame execution to common/framework code. Our claim that app must be a
standalone root directory was a local convention, not a pattern those references
require. Common interface headers remain useful; a global published include tree
is also optional for an integrated game project.

## Pinned comparisons

Directory listings were inspected through GitHub's public tree API; none of the
returned trees was truncated. Selected source/build files were also inspected.
Raw listings and reference snapshots are ignored under
`research/evidence/architecture/source-layout-comparison/`.

| Project / inspected revision | Observed organization | Consequence for this project |
|---|---|---|
| [Doom 3](https://github.com/id-Software/DOOM-3/tree/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo), `a9c49da5afb18201d31e3f0a429a037e56ce2b9a` | One `neo/` source root contains `framework`, `renderer`, `sound`, `game`, `idlib` and `sys`. `sys/sys_public.h` supplies common declarations; `sys/osx`, `win32`, `linux` and `posix` contain implementations. Headers and bodies commonly live together. | A game and its engine can share one source root while keeping game, mechanisms and platform implementations recognizable. A global include/src split is optional. |
| [Lumix](https://github.com/nem0/LumixEngine/tree/d314f846f865fd900230395f6b19a34854100d1d/src), `d314f846f865fd900230395f6b19a34854100d1d` | `src/` contains `core`, `engine`, `renderer`, `audio`, `animation`, `app`, `editor` and other modules. Core OS declarations sit beside `core/win` and `core/linux`; renderer GPU implementations sit under `renderer/gpu`, including DX12 and Vulkan. `src/app/main.cpp` is the runtime entry. | Keep platform implementations near their owning subsystem where that makes the boundary clearer. OS and graphics API are different axes; they need not share one platform bucket. |
| [OpenMW](https://github.com/OpenMW/openmw/tree/08bd6067c3b643953c457d865afc69c5f539556f), `08bd6067c3b643953c457d865afc69c5f539556f` | Root `components/` contains shared libraries; `apps/` contains the game, editor, launcher and tools. `apps/openmw` includes world, mechanics, rendering, sound and game-specific integration, not just main. Its CMake builds `openmw-lib` and links the executable to it; components have their own library target. | Reconstruction projects benefit from explicit reusable-component versus game/application ownership. The word app does not universally mean a thin entry point; its contents and build boundary must be defined locally. |
| [Godot](https://github.com/godotengine/godot/tree/06d1e7a34ccde4029eed53d018cfde77dfe771d5), `06d1e7a34ccde4029eed53d018cfde77dfe771d5` | Root `core`, `scene`, `servers`, `drivers`, `platform`, `main` and `modules` are peer source families. `platform/macos` is ordinary in that scheme. Metal and CoreAudio drivers have separate directories. `platform/SCsub` and `drivers/SCsub` build their respective libraries. | Root-level platform code is coherent when the whole tree uses peer subsystem roots. That does not justify adding only one peer implementation root to an otherwise src-based tree. |

Godot's [official architecture overview](https://docs.godotengine.org/en/stable/engine_details/architecture/godot_architecture_diagram.html)
corroborates the core/server/driver/platform/main responsibilities. For Lumix,
this assessment is based on the observed tree and colocated files; an attempted
historical `scripts/genie.lua` location was absent, so no current Lumix build
selection claim is made from that file.

## Inconsistencies identified before migration

1. Implementation discovery is split between `src/platform/` and root
   `platform/`. Both are legitimate implementation families, but the distinction
   currently depends on explanation rather than an obvious module structure.
2. `bw_video` combines shared player/PCM code and the FFmpeg decoder in one build
   target. The default `Decoder::open` factory is implemented by FFmpeg. The
   injectable interface exists, but the build does not enforce independent
   selection of the decoder backend.
3. `bw_startup_host` contains reusable native media audio as well as startup
   window/presentation integration. Its build identity still reflects the slice
   that introduced it, rather than all of its responsibilities.
4. The current app owns process setup, loop orchestration and startup verification.
   Its name is not inherently wrong, but this is a locally chosen thin-bootstrap
   convention, not a universal engine rule or an exact OpenMW/Unreal convention.

These are source organization and dependency issues. They do not invalidate the
working media algorithms, portable interfaces or measured native playback.

## Recommendation

A single implementation root, retaining `include/bw/` for common contracts, is
the smallest coherent correction to the current repository. Shared mechanisms,
game behavior, native implementations and executable composition can all be
located under `src/`, with one explicit platform family. Doom 3 is the primary structural reference, with Quake II/III showing explicit
game/engine interfaces in one product. Lumix is supporting context for a unified
source root; exact module names need not be copied. Adding an `engine/` level is an optional ownership aid, not a requirement
established by this comparison.

More important than moving directories: split the shared video library, FFmpeg
backend and native media output into meaningful build targets, and select concrete
backends in application composition. Preserve the common interfaces. Treat OS,
graphics API, codec provider and CPU instruction set as distinct reasons for
change; `macos` identifies an OS implementation, not every Apple Silicon-specific
optimization. Introduce target-specific numerical paths only when implemented
and justified, not as empty directory promises.

The user approved the consistent source map, target-owned entries and matching
build dependencies. This migration preserves working playback algorithms.

## Implemented migration

All game/runtime implementation is under `src/`; common declarations remain
under `include/bw/`. Existing tool and test executables retain their own trees.
The native files moved from root `platform/macos/` to `src/platform/macos/`,
including shaders. `src/platform/macos/main.cpp` parses common launch options,
constructs the selected native host/file capabilities, explicitly selects the
FFmpeg factory and invokes shared startup orchestration. Future target entry
points belong in their target directories, without duplicating the shared loop.

`src/framework/startup_application` owns the existing resource/session setup,
frame loop and startup verification. It borrows the platform-neutral StartupHost
and Files capabilities and receives a decoder factory. This framework is the
composition layer of this game/engine product; lower engine modules do not depend
on concrete game code. CLI parsing stays shared so native entry points can
normalize their arguments before constructing a window. Exceptions reach the
target entry, which reports process failure after normal ownership unwinding.
The existing synchronous main-thread loop is retained; target-specific lifecycle
requirements beyond that contract remain future port work.

The FFmpeg implementation now lives in `src/video/ffmpeg/` with an explicit factory
header. `Decoder` remains an abstract session interface; Player and the game
adapter no longer choose a default backend. `bw_video` owns shared player/PCM
logic; `bw_video_ffmpeg` owns the codec dependency; `bw_media_audio_macos` owns
AudioQueue; `bw_startup_host_macos` owns window/input/Metal. The Mac executable
selects these alongside `bw_startup_framework`. Existing media algorithms,
queue limits, clock mapping and completion checks are unchanged.

This follows the inspected Doom 3 native-entry/framework/game-service boundary
without adopting its DLL API, globals or multiplayer design. No empty target
backends or new integration scenarios are introduced.

## Migration verification

The strict host build, all three registered CTest entries, Google formatting,
fatal clang-tidy and repository content checks pass after migration. UBSan and
the provisioned original-root integration also pass. Dynamic dependency
inspection confirms that `runtime-tests` no longer links FFmpeg; `bw-startup`
links the explicitly selected codec backend. Missing launch arguments are
rejected before native service construction.

Logs are ignored under `research/evidence/architecture/source-layout-migration/`.
The original-root verifier retains its required reconstruction evidence location:
`research/evidence/reconstruction/native-startup/layout-original-root.json`.
No new test scenarios were added for the directory moves. Decoder algorithms
were not changed; prior corpus acceptance is retained rather than presented as
a new decoder measurement.

The post-migration audible native replay passed in 6.992 seconds. Both movies
completed with terminal frames presented, all 112,046 and 95,966 PCM frames
consumed respectively, and zero underruns or inserted silence. The first movie
dropped one late nonterminal video frame; the second dropped none. The route
then stopped at the existing unsupported advertise boundary. The replay log is
`research/evidence/architecture/source-layout-migration/native-replay.log`.
These are device/runtime observations; the user's visual/listening review is
separate.
