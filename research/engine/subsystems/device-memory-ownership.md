# Device memory ownership and admission

Design date: 2026-10-23. This continuation supplies native texture ownership and
budget accounting; it does not implement a render graph, heap aliasing, GPU
residency control, or delayed frames in flight.

## Source evidence

Inspected `FMetalTempAllocator::Allocate` and `Cleanup` in the pinned Unreal
source snapshot at commit `f45729c0b6f097038e72b52d366a62baeca3275b` (recorded engine
version 5.8.4). Allocation retains Metal buffers; cleanup removes used buffers
from the available pool and defers recycling/release until the native retirement
path permits it. We adopt the completion-before-reuse obligation, not Unreal's
pool geometry or deferred callback implementation. The current startup renderer
already waits for each command buffer to complete; preserving that ordering
provides a simpler valid lifetime boundary for its retained textures.
[Unreal source](https://github.com/EpicGames/UnrealEngine/blob/f45729c0b6f097038e72b52d366a62baeca3275b/Engine/Source/Runtime/Apple/MetalRHI/Private/MetalTempAllocator.cpp).

Inspected Apple SDK macOS 26.2 `MTLDevice.h` and `MTLResource.h` declarations and
contracts. `heapTextureSizeAndAlignWithDescriptor` estimates heap requirements;
it is not a promise that an independently allocated texture consumes exactly
that amount. `MTLResource.allocatedSize` reports bytes occupied by the resource.
The backend therefore reserves using the descriptor estimate, creates the
texture, reconciles the resource's reported allocation before publication, and
rolls back if either native creation or reconciliation fails.
[Heap texture requirements](https://developer.apple.com/documentation/metal/mtldevice/heaptexturesizeandalign(descriptor:)),
[resource allocated size](https://developer.apple.com/documentation/metal/mtlresource/allocatedsize).

## Contracts

A portable DeviceBudget records pending logical payload/estimated native bytes
separately from committed logical/reported native bytes. Optional configured
limits admit before native creation and reconcile before publication. Defaults
are uncapped because no recovered universal native GPU capacity is established.
Counters do not represent all Metal driver/process memory or physical residency.
CPU domain accounting includes only budget/host CPU objects, never invented CPU
buffers standing in for GPU allocation. Shared CPU/GPU physical memory is not
summed twice as if these categories were independent physical heaps.

A move-only DeviceCharge retains the budget and releases exactly its own charge.
A native texture owner retains that charge and the Metal object; destruction
releases the native object before returning its accounting. Replacement acquires
and validates the new object before discarding the previous one. Temporary
replacement overlap therefore participates in budget admission. The existing
128 MiB sprite logical cap and texture-count cap remain unchanged. Movie and
sprite textures share the explicitly selected device budget but remain distinct
owners. No allocation or charge rollback silently falls back to a global owner.

The host's current synchronous command completion proves that a released texture
is not still consumed by an earlier submitted draw. Future asynchronous rendering
must transfer these owners/charges to completion-retired submission storage;
a CPU frame boundary alone is insufficient. This slice adds no guessed frame
count or aliasing policy.

## Scope and verification

Host State and executable-path scratch receive the selected CPU domain. Cocoa,
Metal pipeline/command machinery, drawables, filesystem path conversions and
audio framework internals remain explicit externally managed allocations.
The API's native-size observation does not remove that limitation.

Portable tests exercise admission, adjustment rollback, overflow, move ownership
and lifetime. Headless Metal checks create/upload/read a small texture, observe
reported allocation accounting, reject a budgeted acquisition, retain the owner
through command completion, and release to zero. These extend the existing
native memory contract scenario without launching a window or changing the
integration inventory. Device availability failure is reported, not treated as
passing native verification.

Design consulted Core E.6 (RAII) and performance GPU.1/GPU.9 in full. GPU.9's
lifetime proof requirement is adopted; heap suballocation is deferred because
this renderer has no transient render graph and already retains textures.
Implementation consultation separately covers bounded arithmetic, move-only
ownership, and completion synchronization.

### Verified implementation

On 2026-10-23, strict debug builds of `memory-tests` and
`bw_startup_host_macos` passed. The extended `memory-tests` passed on the native
Metal device: a shared RGBA texture was uploaded, copied by a submitted blit,
waited to completion and read back byte-for-byte. With two live textures at the
configured logical limit, a replacement request failed while preserving the
previous texture and both live charges. Explicit release after completion
returned pending/live resource and native-byte charges to zero. Portable checks
also passed reported-size reconciliation failure/rollback, overflow rejection,
move replacement, retained budget lifetime and return of CPU metadata charges.
These are ownership/accounting results, not proof that all driver allocations or
physical residency have returned to zero.

Pinned clang-format was applied to the changed files. Fatal focused clang-tidy
passed the device budget, Metal texture owner, native startup host, portable
budget tests and Metal acceptance tests. No game window was launched; this does
not constitute audiovisual playback acceptance. The coordinating task owns the
final whole-repository gates.
