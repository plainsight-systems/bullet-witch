# Modern memory subsystem references

Research date: 2026-10-22. Scope: reference contracts for the reconstructed engine and later improvements; no implementation or allocator selection in this pass.

These sources are **modern design references, not evidence of cavia's implementation**. They cannot establish Bullet Witch's original heap sizes, ownership rules, threading, or failure behavior. The reconstruction should preserve recovered behavior while giving native backends explicit allocation and lifetime contracts. This comparison supplements [Memory and residency](../../../docs/architecture/memory-and-residency.md); it does not supersede original-engine evidence.

## Unity: allocation lifetime and measurement

Unity 6.0 documents several allocators rather than one universal solution. Its dynamic heap uses TLSF; small allocations can use a bucket allocator; temporary allocation has thread-local stack and thread-safe linear paths. The dual-thread wrapper separates main-thread and other-thread allocation. Lifespan, allocation size, and concurrency determine routing. This is a useful counterexample to choosing a single algorithm before identifying consumers. [Unity 6.0 native allocators](https://docs.unity3d.com/6000.0/Documentation/Manual/performance-native-allocators.html).

The thread-safe linear allocator illustrates an important retention tradeoff: blocks become reusable when their allocations are released, but created blocks remain allocated until application exit. Its block pool can overflow into the main heap, and asynchronous background loading has separate handling. A transient loading peak can therefore produce persistent reserved capacity without being a live-object leak. Unity's documented fallback is its policy, not a policy we should silently inherit. [Unity 6.0 thread-safe linear allocator](https://docs.unity3d.com/6000.0/Documentation/Manual/performance-threadsafe-linear-allocator.html).

Unity's Memory Profiler distinguishes used and reserved memory and exposes a gap between tracked allocations and system-reported usage. Native plugins, drivers, and runtime metadata are among its untracked examples. Our counters should similarly admit incomplete coverage rather than describe engine-requested bytes as total process usage. [Unity 6.0 Memory Profiler](https://docs.unity3d.com/6000.0/Documentation/Manual/ProfilerMemory.html).

**Application here:** describe persistent, mission, operation, and scratch lifetimes before selecting algorithms. A streaming request that survives a scheduler yield needs an operation-owned allocation, even if it began during a frame. Record live bytes, retained backing, and overflow events separately. Evaluate peak retention after loading and cancellation, not just allocation speed. These are documentation contracts; Unity's native allocator source was not audited.

## O3DE: routing does not require private heaps

O3DE separates allocation interfaces from allocator schemas and supports subsystem allocators that forward to the system allocator. Its documentation cautions that privately reserved subsystem heaps can strand capacity. Its ordinary pool allocator is not thread-safe; a separate thread-safe pool exists. Crucially, it explicitly describes ordinary `new`/`delete` using global operators when no class allocator is declared. The defensible requirement is controlled ownership, routing, and observability—not the claim that all engines prohibit C++ allocation syntax. [O3DE allocator documentation](https://docs.o3de.org/docs/user-guide/programming/memory/allocators/).

The inspected source is O3DE tag `2605.0`, commit `3db6943249d8bd7960b9ed7e9aee310b7668586e`. `SystemAllocator::Create` constructs `HphaSchema`; allocation/deallocation/reallocation paths apply instrumentation and select either the schema or a configured OS-allocation path. This demonstrates a stable engine-facing boundary with replaceable backing and diagnostics. It does not imply that Bullet Witch needs O3DE's object model or recovery policy. [Pinned SystemAllocator.cpp](https://github.com/o3de/o3de/blob/3db6943249d8bd7960b9ed7e9aee310b7668586e/Code/Framework/AzCore/AzCore/Memory/SystemAllocator.cpp).

**Application here:** a tagged domain can initially share reclaiming backing while reporting its own demand. Reserve private capacity only where lifetime, bounded work, isolation, or measurements justify it. Container elements and nested strings must propagate the selected allocation contract; placing only the outer object in a domain does not route its indirect allocations. Keep domain names independent of backend algorithms.

## mimalloc and TLSF: candidate mechanisms, different guarantees

The current mimalloc heap documentation makes a version-sensitive distinction: v1/v2 explicit heaps allocate from their creating thread, although freeing can occur elsewhere; v3 heaps support allocation from multiple threads through internal thread-local heaps. It also distinguishes deleting a heap, which preserves outstanding allocations, from destroying one, which frees them. These operations are not interchangeable ownership policies. [mimalloc heap API](https://microsoft.github.io/mimalloc/group__heap.html).

Source checks used **v2.2.4**, commit `fbd8b99c2b828428947d70fdc046bb55609be93e`, and **v3.3.0**, commit `0ddf397796fbefa35b3278bd4431c2913a9892eb`. In v2, `mi_heap_delete` transfers or abandons used pages; in v3, it moves them to the main heap. The v3 destruction path instead destroys pages. A system that expects mission teardown to reclaim everything cannot substitute heap deletion and assume the contract is satisfied. [Pinned v2 heap.c](https://github.com/microsoft/mimalloc/blob/fbd8b99c2b828428947d70fdc046bb55609be93e/src/heap.c), [pinned v3 heap.c](https://github.com/microsoft/mimalloc/blob/0ddf397796fbefa35b3278bd4431c2913a9892eb/src/heap.c).

Matt Conte's TLSF implementation documents constant-time allocation bookkeeping and explicitly leaves synchronization to its caller. The inspected revision is `deff9ab509341f264addbd3c8ada533678591905`. `mapping_search` and `search_suitable_block` use size classes and bitmaps; `tlsf_realloc` can allocate a replacement and copy bytes. Consequently, constant-time bookkeeping does not make arbitrary resizing, backing acquisition, lock contention, or initialization constant-time. [Pinned TLSF project](https://github.com/mattconte/tlsf/tree/deff9ab509341f264addbd3c8ada533678591905), [pinned tlsf.c](https://github.com/mattconte/tlsf/blob/deff9ab509341f264addbd3c8ada533678591905/tlsf.c).

**Application here:** mimalloc is a candidate reclaiming backend; TLSF is a candidate for explicitly backed, bounded pools. Neither supplies our domain budgets, asynchronous ownership, resource admission, or GPU retirement. Select and pin a version only after testing representative allocation traces. Bulk release also needs a separate object-destruction policy: reclaiming raw storage cannot close files, unregister callbacks, or release native resources on behalf of live C++ objects.

## Metal: resource reuse follows GPU dependencies

Metal's `makeAliasable` applies to resources from automatic heaps. Once made aliasable, their backing can be reused; accessing the old resource is undefined. Apple's contract requires synchronization between uses of aliases. This is a resource lifetime rule, not merely an allocation optimization. [Metal makeAliasable](https://developer.apple.com/documentation/metal/mtlresource/makealiasable()).

Apple's multistage image-filter sample provides concrete source fragments: after encoding a blur stage, it makes an intermediate texture aliasable, updates a fence, and arranges a fence wait before the next dependent stage. GPU ordering permits reuse without requiring a CPU wait after every pass. The relevant distinction is proving that GPU accesses cannot overlap, rather than equating the CPU's end of encoding with GPU completion. [Apple heaps and fences sample](https://developer.apple.com/documentation/metal/implementing-a-multistage-image-filter-using-heaps-and-fences).

`currentAllocatedSize` reports resources allocated by a Metal device; `recommendedMaxWorkingSetSize` is an approximate performance-oriented resource/heap working-set guideline. Neither should be relabeled as all process memory or treated as an immutable admission guarantee. [Metal allocated size](https://developer.apple.com/documentation/metal/mtldevice/currentallocatedsize), [Metal recommended working set](https://developer.apple.com/documentation/metal/mtldevice/recommendedmaxworkingsetsize).

**Application here:** native resource owners need explicit retirement or ordered-reuse conditions. A CPU frame arena must not own upload bytes still consumed asynchronously. Track logical resource demand and backing allocation separately: overlapping heap resources do not each represent distinct physical storage. On unified-memory hardware, CPU and GPU views must not be naively added as independent physical totals.

## D3D12: residency and budget are separate contracts

Microsoft's public D3D12 residency guidance describes budgets that change with system conditions and application focus. Applications can query budget information and subscribe to changes; a reservation does not increase the budget. Ordinary heaps are residency units, so partially used heaps can still carry whole-heap residency cost. UMA and discrete devices also expose different memory-pool arrangements. [D3D12 residency](https://learn.microsoft.com/en-us/windows/win32/direct3d12/residency).

**Application here:** portable accounting should separate allocation, residency, and admission decisions. The platform backend supplies pressure and budget observations; engine policy decides whether to evict, defer, reduce demand, or fail. A successful CPU allocation is not proof that the renderer can sustain a resource set. This Windows reference is useful for architecture, but is not evidence of Xbox Series X SDK requirements or the original Xbox 360 engine.

## Recommendation for reconstruction and later improvement

The common design lesson is to make lifetime and accounting explicit before optimizing backing algorithms. The following are project recommendations derived from the references, not claims about the original implementation.

| Concern | Faithful reconstruction contract | Later improvement to measure |
| --- | --- | --- |
| Persistent objects | Explicit owner, matching release, domain attribution | General allocator choice, size-class tuning |
| Mission data | Recovered teardown ordering and outstanding-reference rules | Pools or bulk lifetime regions where valid |
| Temporary work | Scope reset only after every consumer has finished | Thread-local arenas and smaller retained capacity |
| Asynchronous work | Operation leases survive yields, cancellation, and completion callbacks | Batch allocation and transfer mechanisms |
| Graphics resources | Native lifetime and synchronization remain explicit | Heap placement, aliasing, residency scheduling |
| Diagnostics | Requested/live/backing/peak and external estimates remain distinct | Target-specific pressure telemetry and budgets |

An arena reset must wait for all borrowers, not a convenient number of frames. Cancellation starts teardown; it does not prove that a worker, decoder, callback, or GPU command has stopped. The owner must join, acknowledge, or retire each outstanding use before releasing backing. Cross-thread deallocation and safe bulk teardown are also separate guarantees.

Measure the actual startup, media, menu, and subsequent streaming workloads. Include alignment, fragmentation, reserved peaks, cancellation cleanup, failure rollback, and the cost of instrumentation. Track codec and driver memory through the interfaces they expose and document the remainder as external or unknown. Do not silently count estimates twice or promise full allocation interception.

The modern algorithms can improve a native implementation while preserving behavior. Changes to resource capacity, eviction, fallback, or scheduling may alter behavior and deserve explicit comparison against recovered evidence. None of these references justifies recreating every historical allocator detail or replacing every standard container before that evidence exists.

## Source provenance

Web documentation was consulted on 2026-10-22. Unity links target the 6.0 documentation branch; O3DE documentation and Apple/Microsoft API documentation are rolling references. Availability must be checked against each target SDK during implementation. Source versions above are inspection pins, not dependency selections or claims to be latest releases. Annotated mimalloc tags were resolved to their underlying commits before downloading.

Local source excerpts, Apple's documentation Markdown, and SHA-256 retrieval records are retained under ignored `research/evidence/memory/modern-references/manifest.json`. They are research inputs, not vendored production dependencies. No production files or game installation files were changed for this comparison.
