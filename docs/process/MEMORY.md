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
  read-only JSON inspector. It does not execute handlers or render anything.
- Format readers now own pure decoding in `formats/`; `RatCatalog` owns its
  decoded `RatData` and lookup index. `content/access` composes reads/location
  through the injected `platform::Files` capability and `HostFiles` adapter.
  The umbrella header retains type names; file opens use content functions.
- The engineering workflow requires both C++ MCP corpora in design and
  implementation, opening file contracts, pinned Google formatting, fatal
  clang-tidy and strict builds. The check entry point is `scripts/check.py`.
- Local game inputs are read-only. Generated research and logs stay organized
  under ignored `research/`; authored tools and reviewed summaries may be tracked.
- Native macOS and UBSan verification are available. ASan startup hangs before
  main on the current host; no passing ASan claim is made. Other target builds
  and rendering/gameplay execution remain unverified.
- Existing integration coverage exceeds the imported five-scenario target;
  retain it under the explicit workflow adoption exception until reconciled.
