# Resource memory ownership migration

Design: 2026-10-23. This is the first resource adoption of the memory foundation,
not a claim that all engine allocations are routed. It follows the recovered
retained-resource behavior summarized in
[memory-management-research.md](memory-management-research.md), and the explicit
routing/lifetime separation established by the pinned O3DE and mimalloc source
inspection in [modern references](memory-modern-references.md).

## Contract and scope

The native ResourceService receives an owning memory-domain capability. Its
archive payload, decoded record/argument vectors, nested strings, request and
publication maps, tag/name sets, traversal temporaries and internal diagnostics
use that capability. The shared archive object and its control block also use the
capability through `allocate_shared`. Shared archive leases preserve bytes and
metadata after tag removal and service destruction. A copied container retains
its allocation domain; no allocator reference may dangle after its producer dies.

The allocator is a standard stateful STL allocator retaining its domain. This
is deliberately different from handing an independently copied container a bare
PMR resource pointer: that pointer alone does not extend resource lifetime.
Nested record members receive the same capability explicitly. Allocation failure
propagates; no process-heap fallback is permitted for an injected domain. Partial
construction unwinds via container/shared-owner destruction. The service retains
its existing single-threaded mutation contract.

HostFiles reads the complete native archive directly into the final allocated
byte vector. Existing legacy file capability implementations may use a documented
copying compatibility overload. The legacy parser/CLI entry path remains separate
from native ResourceService composition; its allocations are not claimed as
engine-domain coverage.

## First-slice exclusions (superseded where noted below)

RAT/catalog parsing and its indexes/mounts remain existing owners. Filesystem
paths, standard stream internals, exception-library storage, and externally
returned LoadReport strings remain library/compatibility boundaries. Those
allocations are not hidden inside the coverage claim. Lua, video, renderer/device
storage, profiles, tasks and game presentation containers are future migrations.
This slice does not change archive interpretation, resource tag semantics,
publication ordering, or encoded/device readiness.

## First-slice verification contract

Existing parser and lifecycle contracts must remain unchanged. Focused cases
will verify nested allocator identity, independent archive copies, shared leases
after service destruction, retained native backing after composition releases its
reference, final reclamation, and bounded allocation failure with cleanup. Large
names must exceed typical short-string storage so allocator propagation is
exercised rather than inferred. Real-corpus/native checks remain integration
checks owned by the main task; synthetic parsing alone is not device acceptance.

Both C++ corpora were consulted during design and again during implementation.
Retrieved Core Guidelines R.20/F.27 preserve explicit shared ownership; E.17
preserves unwinding where no local recovery is possible. Performance MEM.6 routes
real allocations through an injected boundary; MEM.8's lifetime and propagation
caveats motivate retaining the domain in the allocator rather than borrowing it.

## Catalog continuation (2026-10-23)

The authorized continuation closes the prior RAT/catalog exclusion. Catalog
payloads, decoded file/name/dependency/namespace storage, traversal scratch,
lookup-index nodes/keys and mount vectors receive
the same owning allocator contract. `CatalogBytes` is an alias of the same
allocator-aware vector type as `ArchiveBytes`; the legacy `Bytes` type is not
changed globally. Native `open_catalog(files, path, domain)` reads directly into
that buffer. `CatalogSet(domain)` rejects a catalog from a different domain,
preventing silent legacy ingress; legacy default construction remains explicit.

Source reinspection: O3DE tag 2605.0, commit
`3db6943249d8bd7960b9ed7e9aee310b7668586e`, `SystemAllocator::Create`, `allocate`
and `deallocate` separate routing/instrumentation from backing. The pinned source
snapshot and links are recorded in [modern references](memory-modern-references.md).
We retain that boundary and the existing STL owner-preserving allocator rather
than introducing a second catalog-specific allocator algorithm. Namespace and
catalog validation algorithms remain unchanged. Native filesystem paths (including retained mount paths) and standard exception
storage remain explicit external-library allocations. The public filesystem path
API is preserved; its internal allocation cannot be replaced by an STL allocator.

Design consultation retrieved Core R.20 and performance MEM.6/MEM.8 in full;
implementation consultation separately covers bounded traversal and allocation
propagation. Copies retain source domains, including nested strings; no view
outlives its decoded owner. Failed decoding/index construction unwinds all owned
storage. Tests extend existing in-process resource coverage with long names,
dependency vectors, namespace children, copies, mismatch rejection and failure
cleanup. Final integration and full checks remain the parent task's gate.

The [current-runtime completion record](memory-migration-completion.md) covers
subsequent Lua, task, media, image/presentation and device ownership. The initial
exclusions above are a historical slice boundary, not the current implementation
inventory. Profile/path/callback and external-library allocations remain explicit.
