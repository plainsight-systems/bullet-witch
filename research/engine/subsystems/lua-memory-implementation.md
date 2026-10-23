# Lua context memory reconstruction

## Evidence and selected contract

The Steam image and address convention are pinned in
[memory-bullet-witch-evidence.md](memory-bullet-witch-evidence.md).
Existing Ghidra 12.1.4 exports close the current root's mode selection:

- `0x43bc50` constructs the root context at object `+0x118` through `0x560310`;
  instruction `0x560342` initializes its mode byte at context `+0xc` to zero.
- `0x43fc90` passes backing at root `+0x2ba8` and size `0x10000` to `0x560d00`.
  This is the verified **64 KiB root heap**, not a portable VM size guarantee.
- `0x560ea0` selects the reclaiming first-fit heap when that mode is zero:
  allocate with 16-byte alignment, copy the smaller old/new byte count, free old.
  `0x560ba0` frees in that mode. The reconstructed failure path must retain the
  old allocation if replacement fails; the original unchecked copy is unsafe.
- `0x560f20` anchors a new coroutine and calls `0x574d50(state,0)` in reclaiming
  mode. The latter updates the GC threshold and collects when current allocation
  exceeds it, matching pinned Lua's `lua_setgcthreshold`. This is collection,
  not resetting all VM storage between root calls.
- `0x560e30` closes reclaiming Lua before disabling its heap. `0x560d00` closes
  an existing state before reinitialization. Pending continuations retain storage.
- The inspected `0x560fc0` setter call at `0x50268e` selects linear mode for an
  AI construction path. That distinct unused context is not silently implemented
  as reclaiming or generalized into the current root's behavior.

Witnesses remain ignored under `research/evidence/engine/pass-06a/`,
`pass-06b/`, `research/evidence/reconstruction/profile/export/`,
`research/evidence/reconstruction/memory/export-followup/` and
`research/evidence/native-decompilation/functions/00574d50.c`.

## Source references and design

The dependency remains Lua **5.0.2**, tarball SHA-256
`a6c85d85f912e1c321723084389d63dee7660b81b8292452b190ea7190dd73bc`.
Inspected `src/lmem.c` (`luaM_realloc`), `src/lstate.c` (`mallocstate`,
`f_luaopen`, `close_state`, `luaE_newthread`, `lua_open`), `src/ldo.c`
(`luaD_pcall`, `luaD_rawrunprotected`) and `src/lapi.c`
(`lua_setgcthreshold`). Lua 5.0.2 has no later-version allocator constructor.
The project patch explicitly carries callback/userdata through initial state,
its separately allocated global state, coroutines and teardown; no mutable
process-global current-context selector is used.

The established allocator comparison is Matt Conte TLSF commit
`deff9ab509341f264addbd3c8ada533678591905`, inspected `block_split`,
`block_merge_prev`, `block_merge_next`, `tlsf_realloc` in the retained source
under `research/evidence/memory/modern-references/tlsf/tlsf.c`.
Its replacement-allocation failure leaves old bytes intact; adjacent-free
coalescing preserves reusable storage. We retain the recovered **first-fit**
search rather than adopt TLSF's bins or claim constant-time allocation.
[Source](https://github.com/mattconte/tlsf/blob/deff9ab509341f264addbd3c8ada533678591905/tlsf.c).

Synchronization belongs to the actual sharing boundary. Reinspection of TLSF's
split/merge/realloc functions finds no internal locking in those algorithms.
Pinned Unreal `FMemory_GCreateMalloc_ThreadUnsafe` adds an
`FMallocThreadSafeProxy` only when its process-wide allocator needs one; this
separates algorithm and sharing policy. See the commit and inspected paths in
[the Unreal reference](memory-unreal-reference.md). Our Lua heap has one
exclusive Context owner, so it carries no mutex or generic synchronized wrapper.

The bounded coalescer owns one allocation from an injected native-backed Domain.
It supplies the recovered 16-byte-aligned Lua requests, splits trailing space,
coalesces free physical neighbors, and exposes live/high-water/free/largest-free
measurements. Native metadata occupies the configured capacity. A 64-bit layout
may use more bytes than the original 16-byte block header and Lua objects;
capacity adaptation requires measured current-root evidence, not an implicit
fallback or an unexplained larger constant. No caller may retain pointers past
heap destruction; the Context closes Lua before destroying its heap.

A protected C entry wraps allocating host API operations. Lua's internal longjmp
must not skip live C++ destructors; callback allocation returns null without
throwing, and C++ errors are caught before Lua error/yield operations. Native
wrapper storage migrates through the same domain where its interface permits;
legacy constructor routes and external callback captures remain explicitly
identified migration boundaries.

Design consultations retrieved Core Guidelines R.1 and SL.C.1, performance
EMB.4; implementation consultations retrieved F.6 and MEM.6. The deterministic
allocator recommendation in EMB.4 does not override recovered first-fit behavior
or imply a real-time bound. Exhaustion remains a tested explicit failure.
The concurrency revision separately consulted both corpora for design and
implementation: CP.3 (`#rconc-data`) confines writable state, CP.2
(`#rconc-races`) requires externally ordered access, and MEM.6
(`guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md`) retains
Domain injection without conflating backing safety with heap sharing.

## Implemented boundary and verification

`memory::CoalescingHeap` now owns one Domain-backed region with fixed 16-byte
alignment, first-fit allocation, trailing splitting and adjacent coalescing.
This matches the current Lua caller's alignment; arbitrary over-aligned clients
and the original allocator's corresponding leading-fragment rules are not
claimed. Deallocation locates the live block through the bounded physical list
before merging, rejecting unmatched pointers without interpreting arbitrary
caller memory as a header. This makes allocation/free/snapshot O(block count),
not an unmeasured constant-time claim. Reallocation always uses a replacement
before freeing old storage, including shrink requests, as recovered. There is
no in-place growth, compaction or reset API.

Every heap operation requires exclusive access, including `snapshot()` and
destruction. There is no creator-thread affinity: an externally synchronized
handoff may transfer exclusive use to another thread. Calls on the same heap
must never overlap; destruction additionally requires all pointer users to be
quiescent. Separate contexts and their heaps may run concurrently against a
shared Domain. Removing the redundant mutex preserves first-fit, splitting,
coalescing and replacement-allocation behavior; it makes no measured speedup
claim and does not add a shared-heap consumer that the engine does not use.

`lua_open_with_allocator` and `lua_bw_protected` are project extensions to the
exact pinned 5.0.2 source, not upstream APIs. Initial state allocation receives
the callback directly; global-state creation/free bypasses ordinary GC accounting
while still using the same callback. Coroutines copy allocator association.
Ordinary Lua allocation/free uses that state's callback. Lua OOM remains Lua's
protected memory error; no C++ exception crosses the allocator callback. The
patch is idempotent under repeated CMake configuration and checked against exact
upstream fragments. The original `lua_open` remains the explicit legacy path.

Native Context owns its Impl/control block, bindings map nodes/keys, temporary
argument vector and converted bytecode stream through the injected Domain.
Its VM heap is separately bounded within that domain. Lua closes before that
region releases; pending callbacks are cleared before close. Public `Value`
strings, returned `Reply` vectors and type-erased callback capture storage remain
caller-owned interface types and are not represented as fully intercepted memory.
The default Context constructor is retained for legacy tests/tools; native root
composition uses the explicit domain constructor. Named-package migration is
recorded with the resource parser changes.

The real original root workload passed at the unchanged **65,536-byte** capacity
on the current 64-bit Mac build. At its title/menu-return observation, peak
occupied heap bytes were **27,456**, free payload bytes **39,120**, largest free
payload range **38,384**, and rejected requests **0**. Occupied includes block
metadata and alignment, unlike requested payload. Current native block metadata
is 48 bytes rather than the original 16-byte header; this run required no
capacity increase. These observations cover the existing front-end fixture,
not arbitrary missions or worst-case game allocation. Do not add nested heap
occupancy to its Domain's backing charge as though it were additional storage.

In-process runtime contracts passed for first-fit reuse and both-neighbor
coalescing; failure preserving realloc's original bytes; partial bootstrap
failure; yield/resume retention; protected bind/start/global exhaustion; and
independently allocated concurrent contexts. The existing real-root corpus also
passed the full startup/menu route and reconstructed campaign handoff with zero
live domain allocations after teardown. These are allocator/lifetime results,
not a new visual acceptance run. Source-only checks and strict compilation were
performed; parent integration owns the final whole-tree and sanitizer gates.
