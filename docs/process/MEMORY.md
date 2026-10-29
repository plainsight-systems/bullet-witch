# Current decisions

- Architecture baseline is committed on `codex/unreal-rebuild`; the first code
  slice is accepted and authorized for commit. No push or game launch is authorized.
- No prior Bullet Witch code, tests, tooling, configs or caches are reused,
  called, wrapped or imported. Original worktree and purchased data are untouched.
- [Game loop](../architecture/game-loop.md) is authoritative. Unreal reference
  `f45729c0b6f097038e72b52d366a62baeca3275b` supplies the selected flow and algorithms.
- User authorized implementation after architecture → headers → comments-only
  units → tests. Expected missing-definition link red was recorded before bodies.
  All 17 prepared behavioral scenarios now pass without assertion changes.
- Mac time uses Mach ticks/timebase+source offset; waits use source BSD usleep /
  sched_yield. Maximum FPS defaults to source zero, without project override.
- Native explicit60 FPS test measured59.439217 FPS, max interval25.713ms,
  5.696976% of one CPU core. Scheduler read/set/verify all0; observed priority45.
  Jitter remains an OS scheduling boundary, not a reason for invented policy.
- Full build, four independent header units, fatal clang-tidy and pinned format
  pass. Root independently reviewed source correspondence/ownership and checks.
- The host is headless with an explicitly empty world-work seam. No world,
  rendering, input, audio, jobs, simulation or game data is implemented yet.
- Rejected custom-loop code and historical results remain ignored evidence only.
- VS Code uses the current `build-game-loop` compilation database explicitly.
- Next: memory preparation in order: Unreal mapping and architecture, headers,
  comments-only implementation plans, then tests. Review before implementation.
