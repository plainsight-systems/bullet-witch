# Current engineering decisions

- README order: project purpose, how to get it locally, how to run it, then
  background, status, development details and other reference material. Keep the
  top-level `./run` command visible in the running section.

- Source reconstruction is the implementation direction: preserve behavior,
  establish a direct port, then improve the engine and graphics.
- Memory direction approved October 23, 2026: follow recovered Bullet Witch
  allocation, ownership and retirement behavior first, with native target
  backing. Unreal and modern allocators inform the implementation; they do not
  replace the recovered contracts by default. Preserve verified coalescing;
  investigate fragmentation and reclamation as callers and teardown paths are
  reconstructed, then improve from evidence. Do not assume movable live
  allocations or invent historical budgets. See
  `research/engine/subsystems/memory-management-research.md`.
- Memory allocation must support target/silicon-aware implementations behind
  common headers. Shared code owns lifetime/admission/accounting and reusable
  algorithms; selected native backends own CPU backing, device placement,
  visibility and completion. Target-specialized allocation strategies may
  replace hot paths while preserving the common contract. See the explicit
  boundaries in `docs/architecture/memory-and-residency.md`.
- The front-end now loads/persists profiles, presents interactive menus and
  original costume preview sprites, and captures an owned campaign gameplay
  request through original Lua. Steam operation 2300 makes all five supplied
  costumes available; achievement 13 gates stage/extra menus, and 9/10 gate
  Chaos/Hell. The app currently supplies fresh achievement state. Extra launch
  status 5 versus root status 6 remains unresolved. The memory subsystem follows
  this front-end slice before world loading.
- Platform design follows the agreed DoLittle split: single portable subsystem
  headers, shared behavior/algorithms where target-specific optimization adds no
  value, and native implementations for device/platform work. Startup
  uses Metal for Mac presentation; Xbox implementations remain future work.
- The GitHub repository is `plainsight-systems/bullet-witch`. It uses the
  DoLittle split: Apache 2.0 for project-authored code/configuration and CC BY 4.0
  for project-authored writing/diagrams/original visuals. NOTICE credits
  Plainsight Systems LLC and records third-party terms. Original game content
  remains excluded; research tooling stays tracked under the existing policy.
- Original-engine mapping lives in `research/engine/ARCHITECTURE.md`.
  Reconstruction design lives in `docs/architecture/` and is the working structural baseline.
  It describes the finished engine: application/engine/session/mission/world
  ownership, world simulation, rendering, execution/memory and source boundaries.
  Runtime scenarios support that structure; delivery/migration order belongs in
  `IMPLEMENTATION.md`. Proposed authored game code belongs in `src/game/`; the
  ignored root `game/` remains reserved for local data.
- SRC-01 implements an owned C++20 RAT/CPR reader, catalog resolution and a
  read-only JSON inspector. The resource service now publishes supported USER
  payloads, retains leases and releases tag interests; DPK v1 access is implemented.
  No rendering or other record-handler execution is claimed.
- Format readers now own pure decoding in `formats/`; `ResourceCatalog` owns its
  decoded `ResourceCatalogData` and lookup index. `content/access` composes reads/location
  through the injected `platform::Files` capability and `HostFiles` adapter.
  The umbrella header gathers descriptive resource types; file opens use
  `open_archive` / `open_catalog`.
- Startup runtime now has task lifecycle/active/late passes, an owned Lua 5.0.2
  context, bounded 32-bit chunk conversion, wait/immediate bridges and a root
  driver. Engine mechanisms live in `bw_runtime`; game composition in `bw_game`.
  Original root routing reaches title/menu with deterministic test services.
  `CompanyLogoFlow` and `StartupCheckFlow` now implement bounded game update
  policies with in-process verification. The concrete `StartupSession` owns root/screen tasks and preserves
  idle/Wait/destruction ordering (screen priority 0x2000, root 0x2001). The reusable
  engine video Player owns playback control, consumed-media timing and completion.
  Its decoder owns independent demux/audio/video workers and bounded queues;
  the Mac MediaAudio service feeds AudioQueue independently of rendering. Both
  company movies completed the native run with exact PCM consumption, zero
  underruns and terminal-frame presentation; the final XSEED logo was also
  observed on screen. Valid signed source timestamp corrections are preserved
  separately from monotonic device time. The game adapter owns recovered fade
  arithmetic. Mac native code
  owns Metal/AudioQueue/window/input. The two company movies are marvelousLogo
  and xeedLogo. Advertise and title reuse that transport; FrontEnd now supplies
  character-load and menu policies through the same task lifecycle. Logo open-failure
  continuation is an explicit unresolved failure; see `docs/startup-runtime.md`.
- The engineering workflow requires both C++ MCP corpora in design and
  implementation, opening file contracts, pinned Google formatting, fatal
  clang-tidy and strict builds. The check entry point is `scripts/check.py`.
- Local game inputs are read-only. Generated research and logs stay organized
  under ignored `research/`; authored tools and reviewed summaries may be tracked.
- Native macOS and UBSan verification are available. ASan startup hangs before
  main on the current host; no passing ASan claim is made. ThreadSanitizer also
  fails before main with signal 11 in a minimal diagnostic; no TSan pass or
  race-freedom claim is made. Other target builds
  remain unverified; world rendering/gameplay are unimplemented.
- Existing integration coverage exceeds the imported five-scenario target;
  the explicit adoption exception now enumerates eleven scenarios, including
  the provisioned original-root integration required for the startup slice.

- Engine API and file names describe responsibilities. Original opaque terms
  remain only where compatibility/provenance needs them. The naming map in
  `docs/architecture/file-mapping.md` connects resource archives, catalogs and
  named-file packages to their original formats. `TaskLifecycleScheduler` is
  explicitly single-threaded and not thread-safe; it is not a worker executor.

- Native shaders are standalone backend source assets, compiled by the build.
  The Mac movie shader lives in `src/platform/macos/shaders/movie.metal`; the host
  loads its compiled library beside the executable. Common headers carry no
  native graphics types. GPU optimization follows measured timing/bandwidth
  while preserving baseline behavior.
- Movie decode uses a checksum-pinned minimal shared FFmpeg 7.1.1 build with GPL,
  nonfree and version-3-only components disabled. Owned BGRA/PCM samples cross
  the portable boundary. No system FFmpeg selection or game transcoding occurs.

- The current movie decoder handles pre-recorded assets, not in-engine cinematic
  camera/scene/animation behavior. Revisit native Mac decoding as the port grows;
  the common owned-sample header permits another backend without changing game
  policy. Shared decoder source is not evidence of Xbox or Windows deployment.

- Memory migration now covers current native startup catalogs/archives/services,
  root Lua, task storage, owned media/image/font buffers, presentation state and
  Metal texture accounting. Resource/runtime/media domains are explicitly
  selected at the target entry; default aggregate limits remain uncapped pending
  measured application policy. The root's recovered mode is reclaiming; its
  bounded first-fit heap splits/coalesces and never silently grows. Context and
  sample owners survive yields/decoder teardown. Texture retirement follows the
  existing GPU completion wait. Legacy tools and external library/native overhead
  remain explicit boundaries. World pools, AI linear heaps, phase arenas and
  asynchronous device retirement await concrete consumers. See
  `research/engine/subsystems/memory-migration-completion.md` for exact coverage
  and verification; `docs/architecture/memory-and-residency.md` remains the
  broader subsystem contract.

- CPU domain locks protect reservation/commit/rollback counters, never native
  allocation/free or zeroing. Pending reservations count against limits and
  appear separately in snapshots; releasing storage keeps its charge until
  native free completes. The root Lua coalescer requires exclusive owner access
  without an internal mutex; synchronized thread handoff is allowed. See
  `research/engine/subsystems/memory-concurrency-refinement.md`.

- Standard engine subsystems follow established implementations first. Inspect
  concrete mature source, pin references, record adopted algorithms and justified
  departures under organized research, then implement and verify the relevant
  native behavior. This is mandatory workflow, alongside both C++ MCP corpora;
  do not reinvent baseline mechanisms or treat mock results as device acceptance.

- The media redesign is approved. Use Unreal's Media Framework as the engine
  architecture guide: shared playback control and independent sample/audio/video
  responsibilities, with native implementations under `src/platform/`. Original Lua
  and reconstructed game screen code retain playlist order, skip/fade policy and
  transitions for the faithful port. Movie playback has no logo-specific rules.

- Source organization follows the inspected Doom 3 game/engine pattern: one
  `src/` implementation root, common contracts under `include/bw/`, target-owned
  entry points under `src/platform/<target>/`, and shared application composition
  under `src/framework/`. Framework composes game and engine; engine mechanisms
  cannot depend on game code. The Mac entry selects native services and the
  explicit FFmpeg factory. Shared video, FFmpeg, native audio and native
  presentation have separate build targets. Future target entries reuse the
  shared framework; no additional target implementation is claimed.

- The opening cinematic and title flow preserve recovered timing/input/status
  contracts. Title uses original PNG images, FONT glyphs and packed atlas
  channels through shared bounded decoders and renderer-owned Metal textures.
  The game adapter owns title/font/selected-loading resource interests; encoded
  publication is distinct from decoded/device readiness. Native startup defaults
  to fullscreen after AppKit transition completion; `--windowed` and size options
  remain available. Version glyph flags cross an original BGRA convention and
  require explicit red/blue mapping into canonical RGBA sampling.

- Corrected native fullscreen acceptance now covers all four startup movies and
  title outcome 2, with full PCM consumption, no underruns and terminal frames.
  The original START/copyright overlays and corrected Version1.0.5 glyphs were
  visually inspected. Late video-frame drops remain observable; original-game
  audiovisual parity is not claimed. Strict checks, UBSan and real-root/asset
  integration pass. Profile/menu integration is now implemented; confirmation
  sound remains a gap.

- Profile persistence now owns original System/Record bytes and a validated opaque
  checkpoint. Missing files use recovered defaults; failed loads retain prior
  state, and explicit continue-without-saving disables writes. The portable
  SaveStorage contract has a POSIX atomic-file implementation and Mac application
  support directory selection. This is per-file replacement, not a multi-file
  transaction or checkpoint writer. Steam content-refresh flags are separate
  from achievement-based menu gates; see profile-persistence.md.

- Menu policy and rendering remain separate: MenuFlow publishes bounded rows;
  StartupMenuSurface uses existing SpriteOutput and original PNG/FONT data.
  Costume previews are authored 2D images, not a world-rendering dependency.
  FrontEnd gates readiness, fades and persistence; StartupSession captures
  StartMission as owned launch data and leaves WaitMission pending. The host
  exits at that boundary until a world-loading consumer exists. Current options
  cover persisted volumes and costume; audio gain, BGM/cues, other options and
  full submenu animation/localization parity remain outstanding.
