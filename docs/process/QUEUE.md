# Next work

Accepted headless game loop: `d20097a`. Accepted Mac VM/fatal foundation: `99456b2`.
No push or game launch is authorized.

Accepted slice: [General allocator](../architecture/engine/core/memory/binned-allocator.md).
Selected Mac Binned3 allocation, metadata, thread caches and dynamic registry are
implemented. Its real pooled crash bootstrap and desktop named-thread trim
dependencies are implemented. Debug and Release builds, fatal clang-tidy, 23 standalone headers,
all 23 CTest entries in each configuration, formatting and whitespace checks pass.
Accepted allocator commit: `76c8b03`.

Accepted slice: [Allocator lifecycle](../architecture/engine/core/memory/allocator-lifecycle.md).
Executable-local global operators, facade cache hooks and game-owner PreInit/Exit
integration are implemented. Debug/Release full builds, fatal lint, 24 headers,
25/25 tests each, native bounded headless lifecycle, actual host operator symbols,
format and whitespace checks pass. User acceptance authorizes committing this
slice; no push is authorized.

Source game cache remains process-lived; no frame-sleep idle/resume hook, Tick
queue pump or game-thread cache teardown was added. Actual worker wait/exit hooks
remain for future worker owners, and Cocoa main-dispatch memory-pressure delivery
remains for a future graphical host. The user authorized moving to the next
foundation item; its preparation is a separate slice. No worker scheduler, world
or render expansion is included in this accepted work.


Accepted slice: [Shared math](../architecture/engine/core/math/math.md), after accepted allocator
lifecycle `117ca48`. User authorized implementation of the prepared vector,
selected platform primitives and source-default diagnostics. Those bodies now
execute, with source algorithm comments beside each method/significant step.

The public `bw_math` target propagates the consistent Debug/non-Debug diagnostic
configuration and concrete reporting dependencies. The headless stdout sink is
an explicit adaptation; full configurable logging/ensure services remain outside
this leaf. Source exceptional unsynchronized once-state is preserved, without a
new concurrency claim. The accepted host does not consume math yet.

Debug/Release full builds, fatal clang-tidy, 28 standalone headers and 26/26 CTest
entries in each configuration pass. Formatting/whitespace checks pass. Optimized
ARM64 numeric consumers have direct arithmetic and no per-operation calls or
runtime dispatch. Math Mermaid parses successfully. Root independently reviewed
source correspondence and repeated Debug tests/format/whitespace checks.

Accepted shared math commit: `1d3e5d9`. No push or graphical launch.

Accepted slice: [Native register foundation](../architecture/engine/core/math/math-registers.md).
User authorized implementation of the prepared ARM64 register primitive leaf.
Shared contracts retain platform-only silicon selection. Native kernels implement
source float/double lane layout, fused arithmetic, masks, conversions and distinct
reciprocal/refinement paths. No runtime dispatch, scalar fallback or old-project
implementation reuse. Prepared assertions were retained unchanged.

Debug/Release full builds, fatal clang-tidy, 31 headers and 27/27 tests each pass.
Optimized native callers show expected vector instructions with no operation
calls or scalar fallback. Formatting, whitespace and Mermaid parsing pass.
Accepted native register commit: `11fc535`.

Accepted slice: [Register permutations and reductions](../architecture/engine/core/math/math-register-composition.md).
User authorized implementation after preparation. Native permutations, reductions,
comparison/sign operations and exact XYZ/W operations now execute, with source
comments beside the methods. Prepared assertions remain unchanged and now run
in the default build/CTest. Debug/Release full fatal-lint builds, 31 headers and
28/28 tests each pass. Optimized native callers preserve source grouping/fusion
and three-element access extent with no per-operation calls. Formatting,
whitespace and Mermaid checks pass. User accepted this slice and authorized its
commit. No push, game launch or reorganization.

Accepted register composition commit: `37a1aa8`.

Accepted source-layout documentation committed as `e6d93b1`.

Accepted slice: [Source-layout migration](../architecture/source-layout.md).
Existing foundations, tests and foundation drill-ins now follow the agreed
owners. Scoped real public headers and module-owned CMake definitions replace the
universal source include root. Required inline/type detail stays visible;
genuinely private allocator/check/storage headers do not leak to consumers.
All 75 mapped C++ files differ only in include lines. No algorithms, layouts,
constants, test assertions or runtime policies changed.

Debug/Release full fatal-lint builds, the same 31 headers, all 28 CTest entries,
11 actual consumer include-boundary probes, formatting/whitespace, document links
and 12 Mermaid diagrams pass. The documented bounded headless host path works.
VS Code continues using the generated compilation database, now with scoped
per-target flags. User accepted the migration and authorized its commit; no push
or graphical game launch.

Accepted source-layout migration commit: `f383fbb`.

Accepted slice: [Quaternion register algebra](../architecture/engine/core/math/quaternion-registers.md).
User authorized implementation after architecture, headers, comments-only plans
and red tests. Shared safe/identity normalization, unit inverse/rotation and the
native ARM64 product now execute through the accepted register primitives.
Exact source thresholds, candidate+select and fused accumulation order remain;
no class/interpolation/matrix/transform expansion or altered test assertions.

Debug/Release full fatal-lint builds pass with 32 headers, 11 boundary probes and
29/29 tests each. Eight optimized consumers use native instructions inline with
no operation calls, heap, initialization guards or stack copies. Immutable static
constexpr arrays preserve source constants without paired-load stack copies.
Formatting, whitespace and the architecture Mermaid parse pass. User accepted
the slice and authorized its commit; no push or game launch. Later quaternion/matrix/transform
dependencies remain separate preparation.
Future inspection protocol/types/transport and tool implementations remain designed
slices; no empty game/world/rendering/tool modules were created by migration.

Accepted quaternion register commit: `95cba2a`.

Accepted slice: [Quaternion values](../architecture/engine/core/math/quaternion.md).
User authorized the prepared implementation. Aligned float/double values,
constructors/diagnostics, native product and normalization, scalar dot/rotation
and native any-greater now execute. Source algorithms and selected platform
ownership remain as prepared; no new numerical policies. Prepared tests are
unchanged and promoted to the normal build/CTest.

Debug/Release full fatal-lint builds pass with 33 headers, 11 boundary checks and
30/30 tests in each. Ten optimized callers have native inline arithmetic with no
operation calls, heap or guards; scalar value paths remain scalar. The double
product caller retains a 32-byte stack return temporary, recorded without changing
the source return-value API.
Root independently reviewed source correspondence and repeated Debug 30/30.
Formatting, whitespace and Mermaid pass. User accepted this slice and authorized
its commit; no push or graphical launch. Next is preparation of the source
slow-assertion dependency, reviewed before new contracts or bodies.

Value inverse is explicitly deferred until the source `checkSlow` assertion
foundation exists. Do not skip that check or insert a math-local policy. The raw
register inverse is already available. Matrix/rotator/axis-angle construction,
interpolation and transforms are later slices. Tools and inspection remain future
work; the current slice adds no runtime host integration.

Accepted quaternion value commit: `71717b4`.

Accepted slice: [Assertion platform prerequisites](../architecture/engine/core/diagnostics/assertions.md).
User approved implementation and the native-query failure departure. Current
process debugger query, caller-site Apple breakpoint and ordinary Clang
unevaluated analysis assumption now execute. On failed sysctl, return false
without reading output; successful source P_TRACED behavior remains unchanged.
Shared assertion coordinator/history/error device, configurable debugger policy,
analyzer mode and checked quaternion inverse remain unimplemented dependencies.

Debug/Release full fatal-lint builds pass with 35 headers, 11 boundary probes and
33/33 tests each. Actual untraced/traced query and native SIGTRAP tests pass.
The breakpoint fixture's Darwin cleanup was corrected to PT_KILL after evidence
showed plain kill hung while reaping a stopped tracee; its assertions remain.
Optimized callers preserve post-trap flow and omit assumption evaluation. No
claim of debugger resume or injected sysctl failure coverage. Formatting,
whitespace and Mermaid pass. User accepted the slice and authorized its commit;
no push or game launch. Full assertion/history/error-device preparation remains
next, without new bodies until separate authorization.

Accepted assertion platform commit: `bbb4e82`.

Accepted slice: [Assertion error history](../architecture/engine/core/diagnostics/error-history.md).
User authorized the prepared implementation. Diagnostics-private process storage
and RAII plain-text writer now execute the selected 16-bit TCHAR, truncation and
suffix termination algorithms. The production diagnostics module owns definitions
and its WordMutex dependency remains private. Prepared tests are unchanged and
promoted into the default build/CTest. Debug/Release full fatal-lint builds, 36
headers, 11 boundary probes and 34/34 tests each pass. Root source/ownership review
and independent Debug repeat pass; prepared tests remain unchanged. Format,
whitespace and Mermaid pass. User accepted and authorized its commit; no complete
assertion-path claim or startup-mutex change. Next is preparation of the source
static-initialization-safe mutex prerequisite, without new bodies until approval.

Next prerequisites after this leaf: actual ANSI/UTF-8 conversion and formatted
history operations; source publication/stack/log services and installed Mac
error-device lifecycle. The coordinator and checked quaternion inverse remain
pending. Do not silently choose null services or reuse the memory-local fatal
handler to claim the full path complete.

Accepted error-history commit: `abadd90`.

Accepted slice: [WordMutex lifecycle](../architecture/engine/core/concurrency/word-mutex-lifecycle.md).
User authorized implementation. Source constexpr default construction is in the
header, the old out-of-line definition is removed, and the actual history mutex
requires constinit. Lock algorithms and prepared test sources are unchanged.
Both startup object-order compositions now run in the default build/CTest.
Debug/Release full fatal-lint builds, 36 headers, 11 boundaries and 36/36 tests each
pass. Object inspection confirms no mutex dynamic-initialization/guard code in
fixture storage or production history; prepared tests remain unchanged. Source
review/format/whitespace/Mermaid pass. User accepted and authorized its commit;
no formatting/coordinator/inverse expansion in this accepted change.

Next planning must cover a coherent usable assertion capability and its full
predictable dependency set before proposing further implementation. Include
encoding/formatting, publication and required error-device/lifecycle dependencies
up front rather than separate one-function or constructor review cycles. Preserve
architecture/header/plan/test/implementation approval gates; do not silently
expand the current authorized work.

## Backlog

- Review NVIDIA MotionBricks. User requested examining it later; research has not
  started.
