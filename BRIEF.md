# Bullet Witch project brief

**Status: Source reconstruction underway; resource/data foundation (SRC-01) implemented and corpus-checked.** Updated October 22, 2026.

## Confirmed direction

Reconstruct the Steam PC engine into readable, buildable native source using the
recovered executable, scripts, assets and architectural map as evidence. Preserve
original behavior first, then use that codebase for a direct port and subsequent
upgrades, including graphics. Source reconstruction is the chosen implementation
route; automatic decompiler output is reference material, not finished source.

The working destination remains Bullet Witch on Xbox Series X in Developer Mode.
The Mac is the current research and development host. Renderer backend, middleware
adaptation, build/deployment tooling, packaging and performance targets remain
engineering decisions within the source-reconstruction route. The
[source reconstruction plan](IMPLEMENTATION.md) defines
incremental implementation milestones and fidelity checks.

The reconstruction design entry point is
[Engine architecture](docs/architecture/README.md), the working baseline covering the
finished runtime structure, session/world ownership, simulation, rendering,
execution/memory contracts, change axes and source dependencies. Reconstruction
order and existing-code migration belong in [IMPLEMENTATION](IMPLEMENTATION.md).
The [original-engine architecture](research/engine/ARCHITECTURE.md) remains the
evidence reference linking subsystem maps, recovered behavior and outstanding
contracts. The proposed design builds one engine incrementally from shared modules;
use this baseline to design subsequent implementation contracts. All research,
research tooling, extracted resources and evidence stay under `research/`.
Production source and tests live separately in `src/`, `include/`, `tools/` and `tests/`.
The downloaded game installation remains read-only.

The [research index](research/README.md) defines the folder layout: living maps
in `engine/`, external references in `sources/`, dated reports in `reports/`,
checkpoints in `logs/`, and raw outputs in `evidence/`. New research does not
accumulate at the root. The [engine provenance record](research/sources/engine-provenance.md)
tracks publisher history, middleware evidence and engine lineage.
[Direct developer sources](research/sources/cavia-engine-history.md) establish that
Cavia built the original engine in-house from scratch.

## Completed foundation

- Recovered and validated an unpacked copy of the 32-bit x86 executable.
- Recovered 232 Lua occurrences / 231 unique chunks, including startup, missions,
  tests, AI and embedded code. Every unique reconstructed source passes a
  normalized-bytecode comparison after recompilation.
- Mapped all 214 native API names called by those scripts and all three native
  registration tables; exported pseudocode for 2,398 script-related functions.
- Inventoried all 6,625 CPR archives, mapped RAT references and linked 27,079 AI
  tree records to their Lua functions. Original executable and archive hashes
  were rechecked without changes.

Evidence and validation details are in the
[script-recovery report](research/reports/2026-10-21-script-mapping-and-decompilation.md).
These are established static results. Runtime/gameplay validation and a working
port are later milestones.

The first reconstructed module is now buildable: [resource/data foundation](docs/resources.md).
It reads the full RAT/CPR corpus, decodes names/dependencies/record ranges and
provides a CLI inspector. Runtime resource handlers and startup scheduling remain
in SRC-02; container inspection does not imply a running game.

## Systematic engine mapping: architectural coverage complete

The current map connects:

1. Engine architecture, subsystem responsibilities, object ownership, startup
   and shutdown.
2. Main loop, task scheduling, Lua integration and callback dispatch.
3. Classes, structures, vtables, resource formats and loading.
4. Rendering, actors, animation, AI, physics, audio, input, UI and saves.
5. Windows, Direct3D, Steam and middleware boundaries.

The [pass 06 report](research/reports/2026-10-22-engine-documentation-pass-06.md)
records coverage across these domains, reproducible audits and corrected findings.
Remaining work is bounded contract reconstruction and runtime fidelity validation,
tracked in the worklist. This milestone does not mean every native function or
gameplay behavior is reconstructed. Source reconstruction is now selected;
the map and contract worklist guide implementation rather than another general
feasibility decision.

For each mapped area, record named functions and addresses, callers and callees,
recovered fields/types, lifecycle and ownership, dependencies, supporting
evidence, confidence and unresolved questions. Names invented during analysis
must remain distinguishable from original symbols and strings.

## Working agreement

- Continue autonomously within the agreed reconstruction scope; no per-step prompts.
- Preserve findings and evidence incrementally so work can resume from disk.
- Record blockers against the affected function or subsystem and continue on
  other available work.
- Use targeted tests or runtime observations to resolve specific unknowns within
  mapping. Treat uncertainty locally rather than reopening the project direction.
- Maintain diagrams alongside findings: subsystem dependencies, execution
  sequences, state transitions, object relationships and data-loading paths.
  Diagrams must distinguish established links from hypotheses or unresolved
  dispatch. Keep editable Mermaid diagrams in the research documents by default.
- Update the architecture entry point, subsystem notes and worklist together
  when findings change. Trace conclusions back to file paths, addresses, hashes
  and reproducible tools.

## Mapping milestones

- **Architecture baseline:** central entry point, evidence conventions, initial
  subsystem inventory and connected runtime/resource maps.
- **Subsystem coverage:** extend the map through each domain, recording its
  interfaces, ownership, lifecycle, data and platform dependencies. Each entry
  has a concrete next action and local unresolved items.
- **Implementation handoff:** use sufficiently mapped subsystems to define
  implementation boundaries and reconstruction tasks, retaining links back to
  the original behavior and evidence.

A subsystem is not marked complete merely because its functions have automatic
pseudocode. Completion requires its relevant interfaces and behavior to be
explained, with remaining uncertainties explicitly bounded.

## Historical proposal retained for reference

The original text below is preserved as an unapproved proposal. Its rejection of
the PC route, toolchain claims, UWP capabilities, resource limits, deployment
instructions, and milestone requirements are not established project decisions.
In particular, the downloaded PC executable directly imports Direct3D 9; the
original phrase “DX9/11” was not a verified description of this build.

# Bullet Witch → Xbox Series X (Dev Mode) — Recompilation Project Plan

Personal project. Goal: take the Xbox 360 release of *Bullet Witch* (cavia, 2006/07), statically
recompile it to native x86-64, and run it as a UWP package on a Series X in Developer Mode.
Not for distribution — no game data or binaries leave the LAN.

Why the 360 route and not the Steam build: the Steam port (Xseed/Marvelous, 2018) is a Win32
DX9/11 executable with no source. Getting it onto Xbox would mean writing a private Win32→UWP
translation layer from scratch. The 360 XEX, by contrast, has a working recomp toolchain
(XenonRecomp / XenosRecomp / ReXGlue), and the Series X is x86, so the recomp output targets the
console without any further architecture translation.

---

## 0. Prerequisites

| Need | Notes |
|---|---|
| Legal copy of the 360 release | Disc, or the old Games on Demand purchase on the 360's HDD. |
| A way to dump it | Softmodded 360 (**BadUpdate**, USB-stick exploit) → FTP the files off; or disc + PC DVD drive with flashed firmware. Output: `default.xex` + the game's data directory. |
| Windows build box | Build Tools for Visual Studio (C++ desktop + UWP workloads), latest Windows SDK, CMake, Git. VS Community for remote debugging on the console later. SSH in from the Mac. |
| Xenia (emulator) | Not for playing — it's the oracle. Trace which imports the game hits and what the GPU stream looks like. |
| Series X in Dev Mode | Already done. Device Portal at `https://192.168.1.168:11443`. |
| Steam copy (optional) | Second reference: same assets/logic, independently reimplemented renderer. Useful when something looks wrong and you want to know if it's you or the game. |

Toolchain repos (hedge-dev):

- `XenonRecomp` — PowerPC XEX → C++ (x86 intrinsics for VMX128).
- `XenosRecomp` — Xenos shader microcode → HLSL.
- `ReXGlue` — static recompilation runtime SDK (Xenia-based kernel/HLE surface). Feb 2026. Removes most of the per-game runtime work that Unleashed Recompiled had to do by hand.
- `UnleashedRecomp` — the reference project. Read its CMake, its renderer (Plume: D3D12/Vulkan/Metal), and its "what we had to patch" notes before writing anything.

---

## 1. Dump and inventory

1. Dump the XEX and data. Keep an untouched copy; work on duplicates.
2. Decrypt/extract the XEX (xextool or Xenia's own loader). Note: title ID, base address, imports list, whether it's a single module or loads additional XEX modules.
3. Catalogue the data directory: archive formats, texture formats (likely DXT-family in 360 tiled layout), audio (XMA), video (Bink is common for the era), level files.
4. Run the game in Xenia with tracing enabled. Capture:
   - kernel imports actually called (file, thread, audio, input, Live)
   - a GPU trace from the title screen and one combat scene
   - the load order of data files

**Gotchas**
- 360 textures are *tiled* and big-endian; expect to untile and byte-swap.
- XMA audio needs decoding — ReXGlue should cover it via Xenia's path; verify.
- If the game uses Live for anything but DLC enumeration, stub it early.

---

## 2. Recompile to C++ and run on PC first

Non-negotiable: get it running as a plain Windows x64 executable *before* touching UWP. Same
architecture as the Xbox, but with a real debugger.

1. Write the XenonRecomp config: function boundaries, jump tables, any hand-identified
   indirect-call targets. The first pass will miss some; Xenia's traces tell you what was
   actually executed.
2. Generate the C++. Expect a very large translation unit set; split the build.
3. Link against ReXGlue's runtime. Build. Fix link errors (missing kernel HLE functions are the
   usual culprit — implement or stub).
4. Milestone: **boot to title screen.**
5. Milestone: **load a level and render something.**
6. Milestone: **playable at 30 fps on PC.**

**Gotchas**
- VMX128 → SSE translation and float rounding can surface as physics or animation bugs, not
  crashes. Compare against Xenia frame-by-frame when something feels off.
- Indirect jump tables the recompiler couldn't resolve manifest as "jumps into nowhere."
  Add the targets to the config and re-run.
- Save files: decide early where they live (`%LOCALAPPDATA%` on PC, `ApplicationData` on UWP) so
  the runtime abstraction is clean from the start.

---

## 3. Shaders and renderer

1. Run XenosRecomp over every shader in the data set → HLSL.
2. Start from Plume (UnleashedRecomp's RHI) with the D3D12 backend only — Vulkan/Metal don't
   matter for the Xbox target. Keep the Mac in mind only if you want a Mac build later.
3. Map Xenos state (render targets in EDRAM, resolve passes, MSAA tricks, hi-Z) onto DX12
   equivalents. This is the part that rewards knowing GPU architecture — most "it renders but
   wrong" bugs live here.
4. Bullet Witch specifics: heavy particle/alpha work for magic, destructible geometry, a lot of
   post-processing for the era. Profile these scenes early.

**Gotchas**
- EDRAM resolve semantics. Everything that looks like a missing render target is usually a
  resolve you haven't modelled.
- Shader constant layout on Xenos vs DX12 root signatures — do this mapping once, carefully.

---

## 4. UWP-ify the runtime

Keep a single codebase with a platform layer; don't fork.

| Subsystem | PC (Win32) | UWP / Xbox |
|---|---|---|
| Graphics | D3D12 | D3D12 — same API, same code |
| Audio | XAudio2 | XAudio2 — available in UWP |
| Input | XInput / DirectInput | `Windows.Gaming.Input.Gamepad` |
| Files | CreateFile on arbitrary paths | `ApplicationData::Current->LocalFolder` for saves; game data shipped in the package or read from a USB/`LocalState` path |
| Threads | Win32 threads | Win32 thread APIs work in UWP; avoid anything that needs the desktop subsystem |
| Window | Win32 HWND | `CoreWindow` + swap chain for composition |
| Steam/Live | none | none — stub |

Package with `makeappx`, sign with a self-signed cert (`signtool`). Manifest must declare the
package as a **game** (`uap:Capability` set + the Xbox "Game" category) so the console gives it
the full-GPU resource tier, not the 1 GB app tier. Double-check in Dev Home → app Details →
App type after install.

Deploy from the build box without the browser:

```
curl -k -u USER:PASS -F package=@BulletWitch.appx \
  https://192.168.1.168:11443/api/app/packagemanager/package
```

Game data: don't put multiple GB inside the package (portal upload cap ~2 GB per file). Either
ship assets separately into `LocalState` via the portal's file explorer / FTP homebrew, or read
from a USB drive formatted NTFS.

**Gotchas**
- UWP x64 has no Win32 console; route logs to a file in `LocalState` and tail it through the
  portal, or use ETW.
- Resource tier: Series X "game" type UWP ≈ 10 GB RAM, full RDNA2 GPU, several Zen 2 cores.
  Plenty for a 360 title, but the recomp has overhead — watch memory.
- No PIX on Dev Mode. Build your own GPU timestamp queries into the renderer from day one.

---

## 5. Milestones (in order)

1. Dumped + inventoried; Xenia trace captured.
2. XenonRecomp config stable; C++ builds and links.
3. PC: title screen.
4. PC: first level playable.
5. PC: full playthrough without crashes.
6. UWP build on PC (same box, `-DUWP=ON`).
7. Series X: title screen in Dev Mode.
8. Series X: playable at 60 (the Steam port unlocked framerate; the recomp can too).
9. Nice-to-haves: 4K render targets, ultrawide-style FOV options, texture pack hooks.

---

## 6. Boundaries

- The dump, the recompiled binary, and the package stay on the LAN. Nothing is published.
- If any of the toolchain changes get upstreamed (ReXGlue fixes, XenonRecomp config
  improvements), they go up as code only — never with game data.
- Dev Mode activation lives in Partner Center under the professional account; log in once a
  quarter so Microsoft's inactive-account sweep doesn't deactivate it.

---

## References

- hedge-dev/XenonRecomp, hedge-dev/XenosRecomp, hedge-dev/UnleashedRecomp (GitHub)
- ReXGlue SDK (Feb 2026) — Xenia-based runtime for 360 recompilation
- Xenia project — emulator and tracing
- Xbox Device Portal REST API — `/api/app/packagemanager/package`
- Mr-Wiseguy/N64Recomp — the project that proved the approach
