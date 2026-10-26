# Retained model draw replacement

The paused costume change replaces one model component's retained submissions.
Geometry and textures are prepared by the mission resource owner before publication;
weapon, stage, current camera and live material/palette updates remain resident.

## Reference and boundary

Inspected pinned Unreal `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Components/SkinnedMeshComponent.cpp`,
`USkinnedMeshComponent::SetSkinnedAssetAndUpdate`: its component render-state
recreation encloses asset assignment, transform/follower preparation and bounds
invalidation. This informs a discrete component publication boundary, not a claim
that Bullet Witch used this API. The reconstruction's serialized native owner
prepares replacement submissions and their lookup index before nonthrowing swaps.
No asynchronous render-state framework or whole-scene rebuild is required here.

## Contract

`replace_world_model_draws(previous, replacement)` requires retired prior GPU work,
a currently submitted previous model, and a nonempty batch naming one distinct
resident new model not already submitted. Every new draw passes the same material,
texture, palette and raster validation as full publication. Required fallback
textures must already exist; replacement uploads or releases no geometry/textures.

All old-model entries are removed, with new entries inserted at the first old
position. Unrelated order, native states, latest UV constants and owned palettes
are copied unchanged. The current view/output is retained. Validation/allocation
failure leaves the live scene intact. Publication swaps the prepared draw vector
and sorted update index together, then invalidates the cached shadow pass.

Cost is O(scene + copied palettes + scene log scene) during paused loading only;
subsequent pose/UV updates retain the existing indexed, allocation-free path.

Both header design and implementation queried the Core Guidelines and performance
MCP corpora and read I.1 (`ri-explicit`), C.85 (`rc-swap-noexcept`) and GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`): explicit failure/lifetime contracts,
nonthrowing publication, and retained GPU resources.

The explicit scene-lifetime facade adds a distinct `remove_model_draws(model)`
operation. Its backend representation is an empty replacement batch: validate a
currently submitted model, preserve unrelated state and residency, then remove
its submissions. This is separate from the nonempty replacement contract above.
Both operations retain any current view-lighting shadow-radius override and
otherwise recompute coverage from surviving draws. See
[retained scene lifetime](render-scene-lifetime.md).

## Retirement and verification

The first actual-content swap exposed an existing overly broad invalidation:
releasing any texture cleared every submitted draw. Old-body retirement therefore
erased the newly published body and the stage; candidate cleanup could also erase
the prior scene after a failed preparation. Texture retirement now removes only
submissions whose model binds that handle, clears those bindings and rebuilds the
retained destination index without allocating. Unknown/zero handles are harmless.

The existing `memory-tests` native fixture passes pixel-based checks for late
validation failure, retained view and live UV constants, nonadjacent old entries
being replaced at their first position, copied candidate palettes, retained latest
animated palettes, old destination rejection and private-texture retirement.
Pinned formatting and scoped fatal clang-tidy pass for renderer, host forwarding
and native fixture. `memory-tests` and `bw-startup` build successfully. The
integration lead owns the complete required gate.

The ignored `research/evidence/reconstruction/actor/costume_swap_probe.mm` adapts
the existing native secondary-motion diagnostic, using current production
libraries and shaders, original st04_c resources and no window/input automation.
It pauses during walking, changes PW5→PW0→PW1→PW2→PW3→PW4→PW5, presents each result,
then resumes 60 walking updates. All six replacements pass with identical frozen
body position, hips, camera and weapon identity. The final cancellation returns
device resource accounting to zero. Draw counts (body plus weapon) are
52/54/52/62/43/40 for PW0–PW5; differing texture counts reflect each costume's
resources. All six paused PNGs and the resumed PNG were visually inspected: the
stage/weapon remain visible and each selected costume appears. This verifies
publication, not exhaustive cloth intersection or original visual parity.

Reproduce after a coherent application build by relinking the command in
`research/evidence/reconstruction/actor/costume-swap-build-command.json`, then
running the resulting probe with the readonly game root, current
`build/host-debug/movie.metallib`, and an output prefix. The current run and images
are under `research/evidence/reconstruction/actor/costume-swap-native/`. This is
manually invoked evidence, not a new automated integration scenario.
