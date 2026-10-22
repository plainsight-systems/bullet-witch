# Current engineering decisions

- Source reconstruction is the implementation direction: preserve behavior,
  establish a direct port, then improve the engine and graphics.
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
  policies with in-process verification. Native task integration, activation
  side effects, media/fade/input adapters and graphics remain outstanding;
  original-root screen fixtures have not been replaced. Logo open-failure
  continuation is an explicit unresolved failure; see `docs/startup-runtime.md`.
- The engineering workflow requires both C++ MCP corpora in design and
  implementation, opening file contracts, pinned Google formatting, fatal
  clang-tidy and strict builds. The check entry point is `scripts/check.py`.
- Local game inputs are read-only. Generated research and logs stay organized
  under ignored `research/`; authored tools and reviewed summaries may be tracked.
- Native macOS and UBSan verification are available. ASan startup hangs before
  main on the current host; no passing ASan claim is made. Other target builds
  and rendering/gameplay execution remain unverified.
- Existing integration coverage exceeds the imported five-scenario target;
  the explicit adoption exception now enumerates eleven scenarios, including
  the provisioned original-root integration required for the startup slice.

- Engine API and file names describe responsibilities. Original opaque terms
  remain only where compatibility/provenance needs them. The naming map in
  `docs/architecture/file-mapping.md` connects resource archives, catalogs and
  named-file packages to their original formats. `TaskLifecycleScheduler` is
  explicitly single-threaded and not thread-safe; it is not a worker executor.
