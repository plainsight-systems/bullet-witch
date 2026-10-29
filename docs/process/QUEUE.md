# Next work

The first headless game-loop implementation is committed as `d20097a`. Its 17
scenarios remain unchanged and pass; native measurements and source/ownership
boundaries are in [Game loop](../architecture/game-loop.md#verification).

The user authorized implementation of the first memory dependency after
architecture, headers, comments-only units and tests. The Mac virtual-memory
blocks and native fatal failure path are implemented and accepted for commit.
See [Memory](../architecture/memory.md) for source correspondence, ownership,
headless reporting extraction and verification. All 18 CTest entries pass:
17 existing game-loop tests and one memory executable containing six native
success scenarios and seven child-process failure cases. Build, fatal lint,
independent header checks, formatting and whitespace checks pass.

The user authorized moving next to preparation of the selected Binned3 allocator,
bootstrap and thread-cache contracts: source mapping, architecture, headers,
comments-only implementation plans, then tests. Review before implementation.
The current VM leaf is not a complete allocator. No push or game launch is
authorized, and existing game-loop source is unchanged.
