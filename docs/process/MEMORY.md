# Current decisions

- Architecture baseline is committed on `codex/unreal-rebuild`; the first code
  slice and editor configuration are committed as `d20097a`. No push or game launch is authorized.
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
- The game-loop build, its four independent header units, fatal clang-tidy and pinned format
  pass. Root independently reviewed source correspondence/ownership and checks.
- The host is headless with an explicitly empty world-work seam. No world,
  rendering, input, audio, jobs, simulation or game data is implemented yet.
- Rejected custom-loop code and historical results remain ignored evidence only.
- VS Code uses the current `build-game-loop` compilation database explicitly.
- Memory followed architecture → headers → comments-only units → tests →
  implementation after user authorization. [Memory](../architecture/memory.md)
  maps the Mac game target's Binned3 path; its native virtual-memory dependency
  and fatal failure path are committed as `99456b2`.
- The inherited emergency reserve size is zero. The current unguarded headless
  host reports real native memory/error fields and exits critical failures with
  code 3. Full engine logging, crash reporting and allocator observers are not
  installed. Peak statistics reflect the sole fatal sample, not history.
- Expected missing-definition link red was recorded before production bodies.
  Six native success scenarios and seven bounded child-process cases now pass
  in one memory CTest entry. The unmapped-decommit test expected errno changed
  from ENOMEM to Darwin's documented EINVAL; production code was unchanged.
- Full build, eight independent header units, fatal clang-tidy, pinned format
  and whitespace checks pass. Root independently ran all 18 CTest entries:
  17 unchanged game-loop tests plus one memory executable. General allocation,
  bootstrap and thread caches were not part of that accepted foundation.
  Game-loop code is unchanged.

- User authorized selected Binned3 implementation and bringing its real named-
  thread dispatch dependency forward. Architecture, headers, comment plans and
  red-link tests preceded implementation. New `bw_allocator` owns the selected
  small/large paths, TLS/recycler, source dynamic registry, bootstrap and pooled
  crash reserve. No default-heap stand-ins or old-project reuse.
- Desktop global trim follows source epoch/game-local/async render-RHI behavior;
  named owners execute their queued callbacks. No synchronous barrier, invented
  worker scheduler or local-only substitute. Main-loop/accepted VM unchanged.
- New allocator, structural, synchronization, dispatch and crash native suites
  pass in Debug and Release: 23 CTest entries per configuration, comprising the
  accepted 18 plus five new suites. Both full builds, 23 standalone header units,
  fatal clang-tidy, formatting and whitespace checks pass. Root independently
  repeated Debug 23/23, format and diff checks. User accepted the slice and
  authorized committing it; no push or game launch is authorized.
- Source exactness review corrected FreeExternal entry/realloc and forced-flush
  epoch ordering before final validation. Own-code extraction and native platform
  adaptations are identified next to methods, not presented as unchanged Unreal.
- Current host does not globally replace C++ new/delete or initialize allocator
  thread lifecycle. That consumer integration is next review work, not silently
  claimed by these test consumers. Full jobs/GPU/GC/transient systems remain out
  of this bounded allocation leaf.
