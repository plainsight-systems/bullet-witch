# Proposed architecture boundaries

**Accepted target boundaries · October 26, 2026. Execution underway.**
The [reorganization plan](reorganization-plan.md) fixes destination groups and four
implementation slices. Slice 1 is accepted (`fd773fb`), and slice 2 is accepted after native review.
These target diagrams do not claim every proposed repair already exists.
This is the complete proposed system view, complementing the
[current implementation](current-ownership.md). The proposal retains recovered
Bullet Witch behavior and existing portable contracts/native implementations.
Boxes name responsibilities, not required new classes, libraries or threads.

Legend throughout: **E** = existing mechanism retained; **R** = proposed
reorganization/extraction of existing responsibilities; **P** = planned runtime
capability. An E box does not claim complete game coverage.

## Proposed system boundaries

Arrows mean dependency or publication as labelled. Shared engine mechanisms know
contracts/observations; game policy supplies concrete rules. Lighting is named
explicitly, even if its files remain under the engine scene family.

```mermaid
flowchart TB
  App["Application composition and main loop [R]"]
  subgraph Content["Content inputs — original data remains required"]
    Original["Purchased original installation [E]"]
    Packs["Optional enhanced asset/script packs [P]"]
    Validate["Original install admission [E]; pack compatibility and precedence [P]"]
    Original --> Validate
    Packs --> Validate
  end
  subgraph Game["Bullet Witch — decisions and authored meanings"]
    Session["Session, root orchestration and progression [R]"]
    Mission["Mission lifecycle and game phase ordering [R]"]
    Policy["Alicia/enemy/boss actions, camera, authored stage policy [E/P]"]
    Screens["Frontend, pause, HUD, settings and game UI behavior [E]"]
    Bindings["Game script bindings and save/checkpoint policy [E/P]"]
    Session --> Mission
    Session --> Screens
    Mission --> Policy
    Mission --> Bindings
    Mission --> Activation["Mission content readiness and activation [E/R]"]
    Mission --> GameWorld["Game world: actors, actions and mission state [E/P]"]
  end
  subgraph Engine["Engine — mechanisms and portable contracts"]
    Runtime["Frame timing, pacing and lifecycle tasks [E]"]
    WorldClock["Authoritative world clock and admitted frame [E]"]
    World["World identity and membership mechanisms, if required [P]"]
    Resources["Resource resolution, residency and definitions [E]"]
    Script["Lua runtime and execution bridge [E]"]
    Input["Physical input observations and repeat processing [E]"]
    Animation["Pose, playback, events and attachments [E]"]
    Physics["Spatial/static contact [E]; dynamic physics provider [P]"]
    AI["AI evaluation/action mechanisms and navigation [P]"]
    Effects["Effects/particle instance mechanisms [P]"]
    Extract["Retained RenderScene membership and update publication [R/P]"]
    Lighting["Lighting fields, lights and receiver evaluation [E]"]
    Render["Views, materials and ordered render pipeline [R]"]
    UI["Sprite/text rendering [E/R]"]
    Media["Audio runtime and independent video transport [E]"]
    Foundation["Math, memory domains and budgets [E]"]
    GameWorld --> Animation
    GameWorld --> Physics
    GameWorld --> AI
    GameWorld --> Effects
    GameWorld -->|"register, update, remove through publishers"| Extract
    Extract -->|"view and receiver observations"| Lighting
    Lighting -->|"resolved light packets"| Render
    Extract -->|"pose, visibility and material packets"| Render
    Compositor["Final world, movie and UI output composition [E/R]"]
    Render -->|"world image"| Compositor
    UI -->|"ordered UI output"| Compositor
    Media -->|"movie output"| Compositor
    Resources -->|"retained definitions and readiness"| Activation
    Activation -->|"activate in recovered order"| GameWorld
    Resources --> Script
    Foundation -.->|"shared contracts"| World
    Foundation -.->|"shared contracts"| Render
  end
  subgraph Native["Selected platform and middleware backends"]
    Devices["Window, events, clock waiting and files [E macOS]"]
    Graphics["Native pass encoding, GPU resources and shaders [E Metal]"]
    Sound["Audio output and codec adapters [E FACT/FAudio/FFmpeg]"]
    FuturePhysics["Dynamic physics implementation [P]"]
  end
  App --> Runtime
  App --> Render
  App --> Compositor
  App --> Session
  App --> Devices
  App -->|"original admission and pack selection policy"| Validate
  Validate -->|"admitted sources and ordered precedence"| Resources
  Devices --> Input
  Input -->|"copied observations"| Screens
  Input -->|"copied observations"| Policy
  Mission -->|"admission and source phase order"| WorldClock
  WorldClock -->|"immutable frame"| GameWorld
  Policy -->|"actions and authored configuration"| GameWorld
  GameWorld -->|"identity mechanisms only if required"| World
  Bindings --> Script
  Screens --> UI
  Screens -->|"game cue requests"| Media
  GameWorld -->|"audio observations"| Media
  Render --> Graphics
  Compositor -->|"final native present"| Graphics
  Media --> Sound
  Physics --> FuturePhysics
```

The diagram groups implementation scope without requiring all planned systems
before reorganizing existing code. In particular, frame timing and current
collision/animation/lighting work survive independently of the future entity
registry. AI and navigation remain planned mechanisms; enemy/boss decisions are
game code. A lifecycle task scheduler does not imply a job pool. No ECS, new
fixed-step simulation or render/worker thread is proposed here. The application
composes the renderer and game session; rendering consumes engine contracts and
packets and must not depend on concrete game classes.

## Proposed lifetime and authority

Solid arrows mean **owns lifetime**; dashed arrows mean **borrows/uses**. The
engine owns shared services, while a mission owns their world-local instances.
These are different objects, not double ownership of one service. Engine VM
services provide execution mechanisms; mutable script contexts and game bindings
belong to their session or mission, not to a shared global script world.

```mermaid
flowchart TD
  App["Application lifetime [R]"] --> Engine["Engine service composition [R]"]
  App --> Session["Game session [R]"]
  Engine --> Platform["Selected backend/device owners [E]"]
  Engine --> Shared["Resources, VM services, input, audio/video and frame timing [E]"]
  Engine --> Memory["Shared allocation domains and device budgets [E]"]
  Session --> Profile["Permanent Profile and device settings [E]"]
  Session --> Screens["Current native screen tasks [E]"]
  Session --> Scripts["Session script context and game bindings [E/R]"]
  Session --> Mission["Current mission [R]"]
  Mission --> Assets["Mission asset residency [E]"]
  Mission --> Control["Mission control and world clock [E]"]
  Mission --> World["Game-world instances and concrete rules [E/P]"]
  Mission --> MissionScripts["Mission-local script state [P]"]
  Mission --> Presentation["Retained render-scene owner [R]"]
  World --> Actors["Actors, action state, camera and animation instances [E/P]"]
  World --> Simulation["World-local physics, AI and effects state [P]"]
  Presentation --> Lights["Scene-local lighting and environment [E]"]
  Presentation --> Draws["Renderable membership and retained publication [R/P]"]
  Session -.->|"narrow service capabilities"| Shared
  Mission -.->|"narrow service capabilities"| Shared
  Actors -.->|"stable resource borrows"| Assets
  Presentation -.->|"stable resource borrows"| Assets
  Actors -.->|"narrow register/update/remove capability"| Presentation
  Draws -.->|"submitted work"| Platform
```

Session owns permanent progression; mission state does not overwrite it through
render or developer interfaces. Game settings keep narrow System/Record save
capabilities. Presentation/actor borrowers retire before mission assets; backend
completion gates native resource reuse. Current synchronous GPU completion is
retained until a separate asynchronous retirement design is approved.

## Proposed frame and data flow

This diagram shows explicit retained-scene publication while preserving current
source ordering constraints. The clarified render-scene publication phase is **R**, not an existing aggregate.
It is a phase boundary, not a promise of a full-world snapshot copy. The grouped
simulation box follows the recovered schedule; AI, physics and animation phase
dependencies must be recovered as those systems land, not inferred from this
diagram. Render-scene preparation consumes completed current-frame observations. Actors or
components may publish changes through narrow capabilities; no central copy is
required.

```mermaid
flowchart TD
  Pace["Application waits and samples FrameClock [E]"] --> Input["Pump events and map current input [E]"]
  Input --> Admission["Game UI/pause policy chooses world admission [E]"]
  Admission --> Frame["WorldClock publishes immutable admitted frame [E]"]
  Frame --> Video["Advance independent movie transport [E]"]
  Video --> Sim["Source-ordered mission simulation and actor/animation work [E/P]"]
  Sim --> Extract["Publish completed changes to retained RenderScene [R]"]
  Extract --> Light["Resolve view/receiver lighting [E]"]
  Light --> Publish["Publish pose, visibility, view, material and light packets [R]"]
  Publish --> Audio["Finish resources/HUD observations; service session audio [E]"]
  Audio --> Transitions["Mission transition branch; session/root tasks [E]"]
  Transitions --> UI["Publish current gameplay or menu UI [E]"]
  UI --> Passes["World render pipeline and native encoding [E/R]"]
  UI --> Compose["Final output compositor [E/R]"]
  Passes -->|"world image"| Compose
  Video -->|"movie output"| Compose
  Compose --> Present["Native present and retire [E]"]
  Present --> Pace
  PresentationTime["Presentation delta [E]"] -.->|"UI advancement"| Transitions
  PresentationTime -.->|"UI presentation"| UI
  PresentationTime -.->|"exposure adaptation"| Passes
  MediaTime["Media timestamps and audio consumption [E]"] -.-> Video
  Frame -.->|"only admitted world time"| Sim
```

Paused worlds continue UI/device/media work without simulation advancement.
Loading/reentry branches preserve their existing presentation-only pump and
rebasing. Movie/audio consumption is not the simulation clock. The current
100 ms presentation cap, target 60 Hz pacing and actor-local subdivisions remain
unchanged; target rate is not a fixed physics timestep.

## Current owners to proposed boundaries

| Current owner | Proposed responsibility | Review recommendation |
| --- | --- | --- |
| `StartupApplication` | Application composition/main loop, selecting owners and calling phases | Keep one composition boundary; name it for the complete application, not only startup. Engine timing owns its algorithm; game owners choose phase behavior. |
| `StartupSession` / root driver | Game session and native-task orchestration | Retain original Lua ordering and shared frontend state. Do not turn it into an engine service locator. |
| `StageInspection` | Mission actor/camera simulation, scene presentation, diagnostics split apart | Separate game simulation from retained render-scene lifetime now; keep entity-registry growth a separate planned capability. |
| `PlayerActor` / controller publication | Game motion/action state plus completed pose observations | Retain actor/component publishers through narrow scene capabilities; make registration, phase admission and retirement explicit. Preserve source transforms and borrowing. |
| `SceneLighting` / stage environment adapter | Engine lighting versus game-authored channel interpretation | Keep one named lighting subsystem with scene-local state; do not move sampling into actors or replace its recovered equations. |
| `WorldRenderer` / `WorldPostprocess` | Render-pipeline contracts and native GPU implementation | Expose pass/data boundaries; retain native encoding and shaders. No generic frame graph required. |
| `StartupHost` | Native window/input/output composition and presentation | Keep device mechanics here; do not migrate game scene/action/menu policy into the host. |
| `GameAudio`, Profile, shared Options | Game admission/cue/persistence policy using engine capabilities | Preserve accepted lifetime and save authority; directory cleanup must not create duplicate runtimes or settings owners. |

## Settled design and remaining implementation contracts

The [reorganization plan](reorganization-plan.md) settles peer engine source
families, grouped game folders, retained RenderScene publication capabilities,
identifiable scene lighting, sibling world/movie/UI output composition, and
resource residency versus game-owned activation. The required original install
and optional future selected packs share an ordered resolution path; packs cannot
bypass installation admission. This pass does not implement that future pack path.

Exact C++ publication handles, borrow durations and removal/retirement checks are
header-first implementation contracts within these boundaries, not another layout
choice. Preserve synchronous completion; no ECS, fixed-step loop or asynchronous
renderer is implied. Concrete gameplay and AI rules remain game-side unless a
shared mechanism is supported by actual source contracts or reuse.

The [comparison and pinned primary references](reorganization-plan.md#reference-hierarchy-and-comparison-decisions)
use Bullet Witch for behavior, Doom 3 for the integrated game/engine structure,
Unreal for concrete scene/lifetime examples, and Lyra for readiness. They do not
prescribe copying those engines' frameworks.
