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
