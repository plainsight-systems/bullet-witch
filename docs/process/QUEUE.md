# Next work

Accepted headless game loop: `d20097a`. Accepted Mac VM/fatal foundation: `99456b2`.
No push or game launch is authorized.

Accepted slice: [General allocator](../architecture/binned-allocator.md).
Selected Mac Binned3 allocation, metadata, thread caches and dynamic registry are
implemented. Its real pooled crash bootstrap and desktop named-thread trim
dependencies are implemented. Debug and Release builds, fatal clang-tidy, 23 standalone headers,
all 23 CTest entries in each configuration, formatting and whitespace checks pass.
Accepted allocator commit: `76c8b03`.

Accepted slice: [Allocator lifecycle](../architecture/allocator-lifecycle.md).
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


Accepted slice: [Shared math](../architecture/math.md), after accepted allocator
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

Accepted slice: [Native register foundation](../architecture/math-registers.md).
User authorized implementation of the prepared ARM64 register primitive leaf.
Shared contracts retain platform-only silicon selection. Native kernels implement
source float/double lane layout, fused arithmetic, masks, conversions and distinct
reciprocal/refinement paths. No runtime dispatch, scalar fallback or old-project
implementation reuse. Prepared assertions were retained unchanged.

Debug/Release full builds, fatal clang-tidy, 31 headers and 27/27 tests each pass.
Optimized native callers show expected vector instructions with no operation
calls or scalar fallback. Formatting, whitespace and Mermaid parsing pass.
User accepted this register leaf and authorized its commit. Next is preparation
only for remaining register permutations, reductions and shared composites: source
architecture, headers, implementation comments and tests before implementation
review. These are prerequisites for quaternion, matrix and transform work. No
push or game launch authorized.

## Backlog

- Review NVIDIA MotionBricks. User requested examining it later; research has not
  started.
