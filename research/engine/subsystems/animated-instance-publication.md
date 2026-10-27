# Animated instance publication

The source mission trace distinguishes shared character support from live actor
instances. The existing retained renderer had instance-selected light/visibility
updates but broadcast palettes and mutable model-owned shape positions. That
cannot represent multiple animated actors sharing one resident model.

## Reference and ownership

Inspected Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Renderer/Private/RendererScene.cpp`,
`FScene::BatchAddPrimitivesInternal` and
`FScene::BatchRemovePrimitivesInternal`: create one scene proxy/info per primitive,
copy its transform into the creation command, remove that primitive's proxy and
retire its render resources separately from shared asset residency. We retain
that separation using the existing model/instance identity and synchronous
render owner. Unreal's queued render-thread machinery is unnecessary here.

`RenderScene` alone admits/removes instances. Its actor borrower only publishes
poses, shape positions and visibility. One admission names one instance identity
across one or more fresh model pairs
(body and weapon), and owns copied palettes and deformable positions. The whole
batch publishes atomically; no partially admitted actor survives a failure. Vertex,
index, rest-position and texture residency stays shared and immutable. Shape
storage is shared by draws within the same instance, never across instances.
Admission validates and allocates before committing; failure leaves every
existing instance, its latest pose and the current view unchanged. Removal
invalidates only that pair. Identities must not be reused while stale producers
can still publish. Mission generation/lifecycle owns that guarantee.

Pose and shape publications accept an optional instance selector, matching the
existing lighting/visibility contract. Omission explicitly broadcasts to live
matches; an explicit stale identity rejects the whole batch. Shape publication
requires live admitted shape storage, never mutates model rest geometry, and is
consumed by both shadow and color passes. The caller retires GPU use first.

## Cost and consultation

Admission is a discrete setup operation: O(scene + copied palettes + shape bytes
+ scene log scene), using existing engine allocators and budgeted Metal buffers.
No immutable geometry is uploaded again. Pose updates use the existing sorted
model/draw index; shape updates scan the compact unique-instance storage index and copy each
matching mutable stream once. Neither steady-state path allocates. Shared ownership is internal to
one serialized renderer; it prevents per-draw duplication of an instance stream.

Header design queried both MCP corpora and read Core I.11 (`ri-raw`) and
performance MEM.9 (`guidelines/memory/MEM.9-embedded-allocate-at-init.md`).
Implementation queried both again and read Core C.85 (`rc-swap-noexcept`) and
GPU.1 (`guidelines/gpu/GPU.1-keep-data-on-device.md`). These establish explicit
borrowed inputs, bounded admission allocation, nonthrowing final publication and
retained shared geometry. Validation is recorded after implementation.


Instance-specific palettes use runtime texture-slot overrides, not shared model
mutation. The full source chain ends in a prefix-and-handle-alias replacement
that affects every referencing material semantic, including AMOC. See
[npc-appearance.md](npc-appearance.md). Native admission validates and copies
the resolved table. Shadow alpha and color use the same selected bindings;
retirement removes consumers without changing another instance's bindings.

## Verification

The focused `memory-tests` and `resource-tests` targets build and pass. Completed
offscreen Metal pixels verify independent same-model poses and shapes, explicit
broadcast, stale update rejection, duplicate admission rejection, rollback after
a later invalid draw/model, multi-model actor batches, per-instance retirement/readmission, private palettes,
and private texture retirement. Device-budget observations show admission adds
only the mutable shape stream for the fixture, while pose/shape updates allocate
no CPU storage. Existing native shadow, lighting and replacement checks pass.
This establishes the render prerequisite, not a populated original mission or
NPC behavior; game integration and original-content/native acceptance belong to
the parent slice.

The host forwarding target builds. Pinned formatting and scoped fatal
clang-tidy pass for the scene facade, native renderer, window forwarding,
scene capability tests and native allocation/render fixture. Diagnostics are
under ignored `research/evidence/reconstruction/mission-startup/render-instances/`.
The integration owner runs the whole-repository gate after game integration.
No visible game window was opened; no commit or push was performed.

`WorldTextureOffsetUpdate` retains its explicit model/draw broadcast contract.
There is no independent NPC UV-animation producer in this opening-pose slice;
that producer must establish instance selection when implemented. The render
layer does not create an animation clock or infer actor material behavior.

## Rigid attachment placement

Original rigid vertex program 452 consumes position, normal, tangent and owner
transform constants with no joint address registers. A skeleton elsewhere in
the same model does not change that draw's contract. `WorldColorDraw` therefore
retains an optional affine `model_transform` for rigid instances; the existing
`WorldPoseUpdate` publishes it with an empty palette. A transform combined with
a nonempty palette is rejected because reconstructed skin palettes already
include owner placement. Nonfinite or projective rows and stale destinations
reject the whole update batch before mutation.

At encoding, the native owner composes retained model-to-world rows with the
current camera, relative-world and shadow projection rows. Both passes consume
the same placement, and changing the camera cannot erase it. This uses the
shared math matrix product, fixed storage and no allocation. The reference
boundary is also visible in pinned Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/PrimitiveSceneProxy.cpp`,
`FPrimitiveSceneProxy::SetTransform`: the scene proxy retains `LocalToWorld`
independently of views. We adopt that ownership, without its unrelated queued
render-thread machinery or bound calculations. Design and implementation each
read Core I.1 and performance MEM.9 for the explicit, copied, allocation-free
publication contract.
