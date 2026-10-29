# Next work

Accepted headless game loop: `d20097a`. Accepted Mac VM/fatal foundation: `99456b2`.
No push or game launch is authorized.

Accepted slice: [General allocator](../architecture/binned-allocator.md).
Selected Mac Binned3 allocation, metadata, thread caches and dynamic registry are
implemented. Its real pooled crash bootstrap and desktop named-thread trim
dependencies are implemented. Debug and Release builds, fatal clang-tidy, 23 standalone headers,
all 23 CTest entries in each configuration, formatting and whitespace checks pass.
User acceptance authorizes this slice's commit.

Next, plan consumer/lifecycle integration: explicit engine
allocator initialization, owner-thread cache setup/idle/resume/teardown and named
thread dispatch pumps as those threads exist. The current engine host/game loop
remain unchanged and do not route global C++ `new/delete` through this facade.
Do not claim engine-wide adoption or a complete memory subsystem from this leaf.
Any next slice follows architecture → headers → implementation comments → tests
→ implementation and needs user direction before expanding its scope.
