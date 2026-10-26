# Retained scene lifetime and actor publication

Architecture slice 1 introduces an explicit lifetime over existing retained
render submissions. It does not change lighting equations, output compositing,
resource decoding, or the synchronous native submission model. The working
ownership map is [current ownership](../../../docs/architecture/current-ownership.md);
the approved scope is [the reorganization plan](../../../docs/architecture/reorganization-plan.md).

## Evidence and reference boundary

The reconstruction already retains native draws, palettes, lookup indices and
resource handles in `WorldRenderer::State`. The recovered game-side light and
view producers are documented in [live view publication](live-view-publication.md)
and [the scene lighting producer contract](lighting-scene-producer-contract.md).
The new C++ facade is an ownership reconstruction, not a recovered Cavia class name.

Inspected Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Renderer/Private/RendererScene.cpp`,
`FScene::BatchAddPrimitivesInternal`, `FScene::RemovePrimitive`, and
`FScene::RemovePrimitiveSceneInfo_RenderThread`. Component publication and scene
retirement have explicit owners; callers do not receive the scene's general
mutable registry. This supports a narrow actor publication boundary. Unreal's
queued render-thread proxy lifecycle is not adopted: this host already completes
submitted native work synchronously before presentation returns. Adding a second
registry, mandatory full-world snapshot or new fence would not solve a current
lifetime requirement. The earlier [model replacement study](model-draw-replacement.md)
records the separate discrete asset-change boundary.

## Implemented contract

| Owner or borrower | Authority | Lifetime |
| --- | --- | --- |
| Mission resource owner | Upload/bind/release model and texture residency | Outlives scene submissions |
| `RenderScene` | Register, replace/remove retained draws; publish view/light/material and scene visibility policy; retire | Noncopyable, nonmoving; one admitted owner per output |
| `ActorPublisher` | Publish pose, shape positions and visible/hidden state | Embedded in and borrowed from the stable scene owner |
| `WorldRenderer::State` | Sole native retained draw/resource registry and synchronous submission | Outlives scene and resources |

`RenderScene` claims exclusive scene admission and starts preparing. Empty output
is a caller precondition; direct backend test fixtures are outside this facade's
contract. Initial output configuration and geometry observations are available in
preparing state. One successful transactional registration activates publication.
Rejected registration remains preparing. Actor publication before registration
or after retirement rejects before touching the backend. Actor visibility rejects
shadow-caster and owner-opacity policy fields as a complete batch.

Retirement clears only a successfully registered scene and releases its admission
once. Constructor unwinding therefore retires already-published submissions; a
failed initial registration does not clear unrelated state. A rejected second
owner cannot release the first owner's admission. A retired owner's later
destructor cannot clear a subsequent scene. Borrowed publishers must not outlive
their owner. Calls are serialized on the host thread, not concurrent capabilities.

Explicit removal uses the existing backend replacement operation with an empty
batch. It validates membership, calculates surviving shadow coverage, then removes
only that model's submissions and rebuilds compact destination/order indices.
The retained view-lighting override takes precedence over per-draw radii for both
removal and replacement. Model buffers and textures remain resident, unrelated
live UV/palette state survives, and stale destinations reject. Full resource
release remains a separate mission residency operation.

The facade allocates no storage and retains no copied draw packets or model map.
Per-frame calls preserve existing native update costs. Removal is O(draw count)
plus rebuilding existing sorted indices; discrete nonempty replacement retains
its existing temporary vector/palette preparation and transactional swap. No
performance improvement is claimed from the ownership split.

## Verification

The focused in-process scene contracts pass: exclusive admission, registration
failure, constructor unwinding, capability restrictions, whole-batch visibility
rejection, stale publication and idempotent retirement/reentry. Compile-time
checks reject copying/moving the owners and broad scene authority on the actor
capability.

The existing offscreen native `memory-tests` fixture builds and passes. Its
component replacement scenario now also verifies explicit removal by pixel:
current view and unrelated live UV state survive; removed/repeated destinations
reject; native residency accounting is unchanged; removed model geometry and
textures can be submitted again without upload or rebinding. This extends the
existing scenario, without a new application launch or integration scenario.
Pinned formatting and scoped fatal clang-tidy pass for the new scene implementation,
its focused test, the native renderer and the native removal fixture. The integration
lead owns the complete repository gate and game-side composition validation.

Both design and implementation queried the required C++ corpora. Design read Core
I.1 (`ri-explicit`), F.7 (`rf-smart`), E.6 (`re-raii`), performance MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) and COPY.7
(`guidelines/copy-move/COPY.7-eliminate-hidden-copies.md`). Implementation reread
E.6 and MEM.9: RAII admission/retirement, borrowed narrow capabilities, no facade
allocation or copied packets, and validation before backend mutation.
