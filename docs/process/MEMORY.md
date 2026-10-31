# Current decisions

- Architecture baseline is committed on `codex/unreal-rebuild`; the first code
  slice and editor configuration are committed as `d20097a`. No push or game launch is authorized.
- No prior Bullet Witch code, tests, tooling, configs or caches are reused,
  called, wrapped or imported. Original worktree and purchased data are untouched.
- [Game loop](../architecture/engine/runtime/game-loop.md) is authoritative. Unreal reference
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
  implementation after user authorization. [Memory](../architecture/engine/core/memory/memory.md)
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

- Accepted source-layout documentation committed as `e6d93b1`. User authorized
  migrating existing foundations to that layout. Scoped real public/ roots and
  bw/... includes were reviewed before broad edits. Both corpora queried; full
  SF.11/SF.13/GEN.6 read. Pinned Core.Build.cs public/private dependencies and
  VectorRegister.h native selection were inspected as organization guidance.
- Existing core moved into engine/core; vector/SIMD, private binned, OS/CPU and
  shared platform contracts grouped by owner. Tests, runtime probe and active
  foundation drill-ins mirror owners; all local links updated. No copied/generated
  public headers, pimpl changes or empty future modules. Accepted allocator/tasks/
  concurrency definition cluster retained. Global new stays executable-local;
  public math build definitions and root executable paths unchanged.
- Module CMake files now own definitions and scoped dependencies. Public vs
  compiler-required detail vs genuinely private internals are explicit. The same 31
  header checks are classified as 21 public and 10 internal/detail. Eleven positive/negative
  include probes use actual consumer target flags and require the requested
  private header itself missing. Default Debug/Release builds and all 28 tests pass
  fatal lint. Formatting/whitespace, links and 12 Mermaid diagrams pass; bounded
  headless host runs. All 75 mapped C++ files unchanged except include lines,
  independently confirmed by root along with Debug 28/28. Evidence/file/doc maps
  under research/evidence/source-layout-migration/. No migration commit/push or
  graphical game launch; await user review. MotionBricks backlog retained.

- User accepted the source-layout migration and authorized committing the complete
  organization slice. Existing Debug/Release verification remains applicable;
  acceptance changes only documentation status. Next is separate
  quaternion/matrix/transform dependency preparation. No new implementation,
  push or graphical game launch is authorized by this commit task.

- Accepted organization committed as `f383fbb`; user authorized the next math
  dependency preparation. Root reviewed the bounded quaternion-register scope.
  Pinned Common normalization/inverse/rotation, Neon product, exact constants and
  Quat/Transform callers inspected; both C++ corpora queried, full F.8/F.20/GEN.6
  read before headers. Architecture first, contracts second, comments-only plans
  third, tests fourth. Shared composites belong in engine math and target product
  in platform CPU/arm64 through platform-only selection. No numeric bodies added.
- Prepared tests cover order, independent Hamilton products, rotations/inverses,
  precision, signed zeros, normalization thresholds and fallback bit transport.
  Float/double source literals remain distinct, including a regression between
  promoted float and double thresholds. Test error budgets are fixture choices,
  not source accuracy guarantees. Debug/Release fatal-lint default builds pass;
  prepared tests compile then fail linking on absent operations as intended.
  Debug accepted suites remain 28/28. Evidence under
  research/evidence/quaternion-registers/. Await review/implementation approval;
  no commit/push/game launch or future tooling implementation.
- Root independently reviewed preparation and confirmed Debug 28/28 plus Release
  genuine linker-red. Formatting, whitespace and DOM-backed Mermaid parsing pass.
  Preparation is ready for user review; implementation remains unapproved.

- User authorized quaternion-register implementation. Both C++ corpora queried
  again and full F.8/F.20/GEN.6 read; pinned Common/Neon functions and constants
  reinspected. Source comments moved beside methods/significant operations.
  Shared composites stay engine math, product stays platform CPU/arm64. Four
  narrow signature lint suppressions retain approved source operand roles;
  no extra numerical policies, validation, implicit normalization or dispatch.
- Prepared test assertions remain unchanged and target now participates in the
  default build/CTest. Debug/Release full fatal-lint builds, 32 headers, 11
  include-boundary probes and 29/29 tests each pass. Eight optimized native
  consumers retain fused arithmetic/refinement/select, with no calls, heap or
  initialization guards. Static constexpr constant storage removed observed
  double paired-load stack copies without arithmetic changes. Evidence under
  research/evidence/quaternion-registers/. Await user acceptance; no commit,
  push, game launch or future math/tooling expansion.
- Root independently reviewed final source correspondence, inspected all eight
  optimized callers (no stack references/calls/guards/heap paths) and repeated
  final Debug CTest with 29/29 passing. Formatting, whitespace and Mermaid pass.
  Implementation is ready for user review, uncommitted.

- User accepted quaternion-register algebra and authorized its commit. Existing
  verification stands; acceptance edits only update documentation status. Next
  dependency preparation is authorized separately, without numerical bodies.
  No push or graphical game launch.


- Accepted quaternion-register slice committed as `95cba2a`. Next quaternion value
  preparation approved by root after source/dependency review. Both C++ corpora
  queried; full C.49/F.20/GEN.6 read. Architecture was written before headers,
  headers before comments-only implementation plans, then independent tests.
- Values preserve source default initialization, explicit float/double conversion,
  native product/normalization, scalar TVector rotation and scalar dot grouping.
  Source value default tolerance is promoted float, distinct from raw double
  register normalization. Ordered NaN is-normalized behavior remains distinct
  from finite checks. Real constructor diagnostics use the selected stdout sink;
  no callback substitutes or numeric bodies. Checked inverse is deferred until
  the source slow-assertion dependency is implemented, not silently omitted.
- Debug/Release full default builds pass fatal lint and 33 standalone headers.
  New excluded quaternion tests compile and fail linking on actual absent bodies
  in both configurations. Existing Debug tests pass 29/29, independently repeated
  by root. Root reviewed architecture, contracts, plans and test oracles. Format,
  whitespace and DOM-backed Mermaid checks pass. Evidence is ignored under
  research/evidence/quaternion-values/. Test compile fixes only qualified POSIX
  close and made fixture narrowing explicit; no assertions weakened. No commit,
  push, game launch or numerical implementation beyond accepted register slice.


- User authorized quaternion value implementation. Pinned Quat/Neon functions
  reinspected; both C++ corpora re-queried and full C.49/F.20/GEN.6 read before
  bodies. Source scalar dot/rotation, native product/tolerance selection and
  per-template Debug diagnostics retained. No inverse assertion shortcut, new
  finite repair, normalization policy or old-project reuse. Source comments now
  accompany methods and significant calls. Static value identity follows source
  construction; normalization uses immutable typed lane constants.
- Prepared tests remained byte-for-byte unchanged and now run in default CTest.
  Debug/Release full builds pass fatal clang-tidy, 33 header checks, 11 consumer
  include boundaries and 30/30 tests. Source no-init/native-store paths have narrow
  documented lint annotations instead of artificial initialization. Ten optimized
  ARM64 callers show inline native math and the selected scalar value paths, no
  operation calls, heap or guards. Double product retains a 32-byte stack return
  temporary; no API/algorithm change was made to chase it. No benchmark claim. Evidence is
  under research/evidence/quaternion-values/. Root independently reviewed source
  correspondence and repeated Debug 30/30. Format, whitespace and Mermaid pass.
  Await acceptance, uncommitted; no push, game launch or further math expansion.

- User accepted quaternion values and authorized committing this complete slice.
  Existing implementation verification stands; acceptance edits only change
  documentation status. Next source slow-assertion dependency preparation is
  separately authorized, with scope review before artifacts and no runtime bodies.
  No push or graphical game launch.


- Accepted quaternion values committed as `71717b4`, tree clean afterward. Root
  caught an accidental prior-register verification wording change during commit;
  restored that historical register paragraph before final acceptance handoff.
  The documented 32-byte stack temporary applies only to the value product caller.
- Root approved next bounded platform/compiler prerequisite preparation after
  full pinned assertion-path tracing. Architecture maps slow-check selection,
  critical history, log flush, debugger continuation, error device and guarded
  versus unguarded native failure. Shared general assertions are not replaced by
  private allocator fatal handling. Source ordinary CA_ASSUME is unevaluated
  sizeof(bool(expr)), not compiler unreachable. Apple raw breakpoint is a
  caller-site builtin, independent of debugger presence.
- Both C++ corpora queried and full I.5/E.26/GEN.6 read before header design.
  Architecture preceded headers, comments-only target/compiler plans and separate
  tests. Both current Debug and Release select the non-shipping default
  ignore-debugger=false path; configuration/Shipping and analyzer support remain
  explicitly later. Native tests require real tracing success, with no injection
  or silent skip. No runtime or macro implementation bodies were added.
- Pinned Apple query ignores sysctl failure and can read uninitialized storage.
  Proposed checked-call -> false is an explicitly NOT YET APPROVED departure
  requiring user decision before implementation, not silently attributed to
  Unreal. Source successful-call behavior is clear. Header, plan and architecture
  all preserve this unresolved boundary.
- Debug/Release default fatal-lint builds and 35 headers pass. Separate native
  query tests reach missing-definition linker-red in both configurations. Macro
  fixtures fail compilation specifically because their required macros remain
  undefined; no fake test definitions were introduced. Accepted Debug tests stay
  30/30. Format/whitespace/Mermaid pass. Evidence under
  research/evidence/assertion-platform/. Preparation remains uncommitted; no push,
  game launch, checked inverse or full assertion implementation.

- Root independently reviewed the assertion prerequisite preparation, confirmed
  real missing-query link failure versus missing-macro compilation failures, and
  repeated Debug 30/30. No findings. Native breakpoint test intentionally stops
  and reaps a child rather than claiming debugger resume; later codegen must
  retain post-trap caller flow. No additional implementation authorized.


- User approved the proposed syscall failure departure and authorized primitive
  implementation. Reinspected Apple query, raw trap and ordinary CA_ASSUME;
  queried both C++ corpora and read full I.5/E.28/GEN.6 before bodies. Native query
  checks syscall result and returns false without reading failed output. Raw trap
  remains caller-site builtin; assumption is unevaluated bool sizeof. Platform
  library owns the query; no shared assertion/inverse/analyzer expansion.
- Actual native query and assumption tests passed immediately. Breakpoint test
  correctly observed SIGTRAP then hung during prepared plain-kill cleanup.
  Ignored observation copy recorded wait status1407 then blocked reap; local
  Darwin ptrace(2) specifies PT_KILL as continuation delivering SIGKILL. Reported
  defect before changing test, root authorized cleanup correction. PT_KILL now
  passes with original SIGTRAP/reaped-SIGKILL assertions. Production macro was
  unchanged. No native debugger injection, denial skip or reduced timeout.
- Final Debug/Release full fatal-lint builds, 35 headers, 11 boundaries and 33/33
  tests pass. Codegen shows brk followed by caller add/ret; ordinary assumption
  emits no runtime side effect, check or call. Native test kills the stopped
  tracee; debugger resume is not claimed. Sysctl failure branch reviewed, no
  injected failure coverage claim. Format/whitespace/Mermaid pass. Evidence under
  research/evidence/assertion-platform/. Uncommitted pending acceptance; no push,
  game launch or future subsystem bodies.

- Root independently reviewed final native bodies and Darwin cleanup, inspected
  both codegen callers and repeated Debug 33/33. Approved departure and verification
  limits are recorded. Ready for user review, uncommitted.

- User accepted assertion platform primitives and authorized committing this
  complete slice. Existing verification stands; acceptance only changes status
  documentation. Next shared assertion/history/error-device preparation is
  authorized, with bounded source-scope review before contracts or bodies.
  No push or graphical game launch.
