# First macOS CPU backing

The first native provider selects macOS libmalloc through `malloc_zone_memalign`
on the default zone and `malloc_zone_free` on the pointer's originating zone.
Target composition explicitly constructs this provider and passes its
common `CpuBacking` capability to memory domains. Portable code has no implicit
Mac default. This is reclaiming CPU storage; Metal resource allocation and
device completion remain separate work.

## Source basis

Inspected Epic revision `f45729c0b6f097038e72b52d366a62baeca3275b`:
`Core/Private/Mac/MacPlatformMemory.cpp`, `FMacPlatformMemory::BaseAllocator`,
and `Core/Private/HAL/MallocAnsi.cpp`, `AnsiMallocInternal`, `AnsiFreeInternal`,
`FMallocAnsi::TryMalloc`. These demonstrate target-selected allocation and a
nullable aligned native allocation branch paired with native free. The POSIX
branch is one compile-time path; this does not claim it is Unreal's default Mac
allocator. See [the broader Unreal study](memory-unreal-reference.md).

Inspected Apple's libmalloc revision
`c49dafa25f1efe8607701ae6014a663ad2ee437f`,
[`src/malloc.c`](https://github.com/apple-oss-distributions/libmalloc/blob/c49dafa25f1efe8607701ae6014a663ad2ee437f/src/malloc.c):
`_posix_memalign` requests storage through the default zone, publishes its output
only on success, and distinguishes alignment rejection from allocation failure.
`_malloc_zone_memalign` permits byte counts that are not multiples of alignment
for this API. `free` routes storage back to its registered zone. Also inspected
`malloc_zone_memalign`, `malloc_zone_from_ptr`, and `malloc_zone_free`: these
expose the native acquisition, owner lookup and release operations used here.
Selecting those Apple APIs makes the backing boundary explicit while preserving
the repository's strict ban on incidental `malloc`/`free` in engine code.
This is a pinned
source reference, not identification of the exact binary installed on this Mac.
Source snapshots and hashes are under ignored
`research/evidence/reconstruction/memory/native-backing/`.

## Chosen contract

Positive byte count, power-of-two alignment at least `alignof(std::max_align_t)`;
invalid requests and native failure return null without throwing. Success returns
raw uninitialized storage. Deallocation receives the original pointer once, with
the same size/alignment tuple. The provider delegates allocation synchronization
to libmalloc and adds no mutable accounting state or global engine singleton.
Different allocations may be allocated/released concurrently; this does not
synchronize object access or make double-free safe. There is no real-time latency
or lock-free guarantee.

The factory uses `make_shared` for its small bootstrap provider/control block and
may throw before a domain exists. Shared domain ownership retains the provider
until all owned allocations are released. Bootstrap allocation is an explicit
external allocation, not recursively charged to the domain being constructed.

Domains own zeroing, owner metadata, alignment normalization, overflow rejection,
budget admission and accounting. Backing charge means the bytes requested from
this provider, including domain overhead; it does not claim malloc usable bytes,
committed pages or physical residency. Returning an allocation to its zone
permits allocator reuse and does not guarantee immediate OS page reclamation.

This follows the recovered ordinary independently reclaimed heap path, while
substituting Mac native backing for Windows heap calls. There is no evidence
requiring a separate native malloc zone per logical domain, and no measured basis
for introducing a new size-class allocator in this slice. Bounded heaps, render
rings and Lua-specific arenas retain their separate future contracts.

## Implementation checks

Design MCP searches queried both corpora for native allocation ownership and
alignment; implementation searches queried both for paired C API ownership and
injected native allocation. Full rules read: Core R.1 and E.6, performance MEM.6.
The provider is injected at composition; shared lifetime and test guards retain
paired release on exceptional paths. Native in-process tests exercise writable
non-multiple-sized aligned allocations, independent reclamation, cross-thread
release and invalid-request rejection. They do not prove pressure behavior,
fragmentation or GPU residency. The parent slice runs repository-wide checks.

The selected zone provider and native tests passed a standalone Apple Clang C++20
build with `-Wall -Wextra -Wpedantic -Werror` and a successful in-process run.
Both translation units passed the pinned LLVM 21.1.8 fatal clang-tidy policy;
Google formatting was applied to the new files. Logs are beside the ignored
source snapshots. These focused checks precede the parent slice's full build
and test run; no graphical executable was launched.
