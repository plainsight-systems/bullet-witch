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
- At allocator-leaf acceptance, the host did not globally replace C++ new/delete
  or initialize allocator thread lifecycle; adoption is the next slice below. Full jobs/GPU/GC/transient systems remain out
  of this bounded allocation leaf.

- Allocator adoption preparation follows accepted `76c8b03`: architecture,
  executable operator declarations, four facade lifecycle declarations,
  implementation comments and real native test consumers are written. Source
  PreInit sets up game cache before dispatch; Exit stops dispatch after engine
  PreExit while retaining process allocator/game-cache lifetime. No arbitrary
  Tick pump, frame-wait cache hook or game-cache clear was added.
- Preparation changes no production bodies. The default Debug build, 24 headers,
  fatal clang-tidy and existing 23 tests pass. Explicit new targets compile then
  link-red for four source-extra delete forms and four lifecycle forwarders.
  Runtime assertions are not yet executed. No test doubles or sentinel symbols
  conceal missing behavior. Implementation awaits review/authorization.

- User authorized allocator lifecycle implementation. The prepared four facade
  forwards, complete global operator family and source PreInit/Exit integration
  now execute. Game cache/heap retain process lifetime; Tick/timing are unchanged.
  The global operator test completes real native engine startup/Exit before its
  late static destructor validates ownership and frees through global delete.
- Both Debug/Release builds and 25/25 CTest entries pass, with fatal clang-tidy,
  24 headers, format and whitespace checks. All 24 global operators are strong
  symbols in the actual host. Bounded headless startup/shutdown exits0. Mermaid
  reserved participant ID was renamed and parser-verified outside repo tooling.
  User accepted this slice and authorized its commit and moving to the next
  item. No push or graphical launch is authorized.


- Accepted allocator lifecycle is committed as `117ca48`, not pushed. User
  authorized next-item preparation through the established staged workflow.
- Math begins at the inspected TVector leaf, which uses scalar expressions.
  ARM Mac inherits generic scalar sqrt/reciprocal-sqrt/finite classification;
  future vectorized transforms require separate native NEON register algorithms.
  Shared contracts/core algorithms and platform/silicon implementations remain
  separate with compile-time selection, no per-operation runtime dispatch.
- Math architecture, declarations, comments-only implementation plans and native
  tests are prepared. Float/double precision, explicit conversion, default
  initialization and exact normalization branch/threshold behavior come from
  the pinned source. Runtime math behavior is not yet implemented or validated.
- Source default GEnsureOnNANDiagnostic is false. Debug checks nonfinite data,
  reports once per templated diagnostic callsite, preserves values and continues.
  Prepared concrete headless stdout reporting is an explicitly documented sink
  adaptation, not fake callbacks/noops or a claim of complete logging/ensures.
  Source optional console/ensure mode and concurrent first-error once-state
  policy remain explicitly identified boundaries. No production bodies written.

- Math preparation validation: default Debug build and 28 independent headers
  pass with fatal clang-tidy; prepared Debug/Release test consumers compile
  without warnings and fail linking on real missing production definitions.
  Existing Debug tests pass 25/25; format and whitespace checks pass. Root
  independently reviewed the source/header/plan/test mapping and repeated the
  accepted tests/format/whitespace checks. Evidence is ignored under
  `research/evidence/math-preparation/`. New runtime assertions have not run.

- User authorized math implementation after preparation review. Vector arithmetic,
  exact source normalization branches, mixed-scalar promotion, source precision
  defaults, explicit zero constants and ARM Mac-selected scalar primitives now
  execute. Inline definitions preserve the target boundary without runtime dispatch.
- Source-default Debug once-only reporting and the documented real stdout sink
  adaptation execute; non-Debug diagnostics compile out. Public `bw_math` owns
  consistent build configuration and link dependencies. Optional ensure/logging
  infrastructure and source exceptional concurrent-first-error limitations remain
  explicit. No old implementation was used, and no future SIMD tier is claimed.
- Math Debug/Release full builds, fatal clang-tidy, 28 headers and 26/26 tests each
  pass. Existing assertions were retained; a mixed-precision division regression
  was added to detect premature denominator narrowing. Optimized ARM64 numeric
  consumers have no function calls/dispatch. Mermaid parses outside repo tooling;
  format/whitespace checks pass. Root independently reviewed correspondence and
  repeated Debug 26/26, format and whitespace checks. Await user acceptance; no
  commit, push or game launch occurred.

- User accepted the vector math implementation and requested the next slice.
  Acceptance authorizes committing this bounded math/diagnostics/build/docs/test
  unit; next native SIMD work starts with contracts/plans/tests only. No push.

- Accepted shared vector math committed as `1d3e5d9`, no push. Began native SIMD
  preparation only. Pinned ARM64 source has one float4 and paired XY/ZW double4;
  shared API names and platform-only compile-time selection avoid runtime wrappers.
- Register leaf scope is load/store, broadcast/conversion, arithmetic/fused ops,
  EQ/GT masks/bit-select and reciprocal/sqrt estimate/refinement. Float reciprocal
  uses two polynomial refinements; double uses direct divide. Both rsqrt paths
  use two hardware correction steps, not an invented full-precision scalar path.
  Remaining permutations/reductions/composites are explicit later dependencies.
- Architecture/header/comment-plan/test artifacts are prepared, without numeric
  bodies. The source map was reviewed first, but the concrete architecture file
  landed after declarations/tests; this missed the required artifact order and
  must not recur. Silicon selection was moved wholly into platform selectors
  during root review. Both corpora queried/full applicable rules read.
- Default Debug build/fatal lint/31 independent headers pass; prepared Debug and
  Release consumers compile then link-red on actual absent register definitions.
  Existing Debug CTest26/26, format, whitespace and Mermaid parsing pass. Runtime
  assertions await implementation. No commit/push/game launch for preparation.

- User authorized native register implementation. The prepared ARM64 primitive
  bodies now execute with source comments beside operations and significant
  refinement steps. Source float reciprocal polynomial steps, direct double
  reciprocal, two-correction rsqrt paths, fused rounding and bit masks retained.
  Explicit native pair construction/extraction replaces source aliasing casts;
  no arithmetic departure, scalar fallback or per-operation dispatch added.
- Both corpora re-queried and applicable full rules read during implementation.
  Prepared native tests promoted into default build/CTest with all assertions
  unchanged. Debug/Release full fatal-lint builds, 31 headers and 27/27 tests each
  pass. Optimized native consumers show expected vector instructions without
  function calls. Formatting/whitespace/Mermaid pass. Remaining composition
  primitives and quaternion/matrix/transform tiers remain unimplemented. Await
  user acceptance; no commit/push/game launch. MotionBricks backlog preserved.

- User accepted the native SIMD register leaf and authorized committing this
  bounded implementation, tests and documentation, including the MotionBricks
  backlog note. Next dependency work is preparation only, not new implementation.
  No push or game launch authorized.

- Accepted native register leaf committed as `11fc535`. Next composition leaf
  prepared in the required artifact order: architecture, shared declarations,
  platform comments-only plans, meaningful tests. Both corpora queried and full
  I.9/F.20/GEN.6 rules read. Source algorithms and actual callers bound the API;
  raw bitwise operations and higher-level composites remain deferred.
- Prepared tests distinguish source float/double reduction grouping and fused
  double cancellation, lane routing, NaN payload/signed-zero preservation, GE
  behavior, W setters and exact XYZ memory extent using a real guard page. The
  cancellation fixture uses 2^(digits+1), avoiding asymmetric spacing at 2^digits.
  Debug/Release consumers compile with fatal lint then fail at real absent-symbol
  linking. No numeric bodies or runtime pass claimed. Default Debug build and
  31 header checks pass, accepted CTest remains 27/27, formatting/whitespace and
  DOM-backed Mermaid parse pass. Root independently reviewed source mapping and
  repeated accepted tests. Evidence is ignored under
  `research/evidence/math-register-composition/`; no commit/push/game launch.
- User requested folder-structure review/proposal after this slice, followed by a
  separate organization slice before further math expansion. Recorded in QUEUE;
  no files reorganized now. NVIDIA MotionBricks backlog remains unchanged.

- User authorized register composition implementation after preparation review.
  Native lane routing, dot3/dot4, fused cross, GE/sign operations and exact XYZ/W
  operations now implement the pinned source algorithms. Shared API/ownership
  unchanged; silicon code remains under platform/arm64. Explicit low-pair
  extraction replaces the source float store aliasing cast without changing
  extent/layout. No old-project implementation, new policies or reorganization.
- Both corpora re-queried for implementation; full Per.11/F.20/GEN.6/SIMD.1 read.
  Prepared tests promoted to default build/CTest with every assertion unchanged.
  Debug/Release full fatal-lint builds, 31 standalone headers, 28/28 tests each,
  formatting/whitespace and DOM-backed Mermaid parsing pass. Twelve optimized
  native callers show source fused/grouped arithmetic, exact XYZ accesses and no
  per-operation calls; no broader performance claim. Root reviewed source/body
  correspondence and all twelve optimized callers, then independently repeated
  Debug CTest28/28. Evidence under research/evidence/math-register-composition/.
  Await user acceptance; no commit/push/game launch. Folder review/proposal and
  separate organization slice remain next; MotionBricks backlog preserved.

- User accepted register composition and authorized committing its implementation,
  tests and documentation. Next is a project-wide folder discussion covering the
  full engine/game roadmap, not only current math files. No proposal documents or
  reorganization until that discussion; no push or game launch.

- Accepted register composition committed as `37a1aa8`; clean tree afterward.
- User agreed the full-project layout and tooling direction. Documentation now
  records engine/core nesting, engine/game ownership, OS/CPU platform separation,
  subsystem-local backends, mirrored tests/docs/build modules and explicit
  public/internal dependencies. Existing source has not moved.
- Hosts/tools must compose the same production systems: full game, headless
  simulation, character viewer, AI scenarios and offscreen production capture.
  New tools/simulation_inspector is a 2D client of debug runtime snapshots and
  commands, with layered elevation display, unusual-placement scenario authoring,
  stepping and image/state trace outputs. Collision/navigation remain 3D; tools
  cannot mutate controller/physics memory. Protocol/types/transport remain future
  design. No tool implementation or production behavior claimed.
- Scenario source is distinct from external purchased/derived game content;
  original project-owned fixtures may be tracked, generated recordings/traces
  remain ignored. Documentation-only change; no migration/scaffolding/commit/push.
