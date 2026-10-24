# Live view publication

The moving player camera publishes a small world/sky view packet independently
of scene geometry, materials and skeletal poses. Each submitted draw explicitly
selects fixed, world or sky coordinates. Fixed draws retain their original
constants, including isolated device diagnostics.

Reference inspected: Unreal revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Renderer/Private/SceneRendering.cpp`,
`FViewInfo::SetupUniformBufferParameters`, `InitRHIResources` and
`CreateViewUniformBuffers`. These functions assemble view transforms and other
view constants separately from scene geometry. This reconstruction adopts that
ownership boundary; it does not copy Unreal's implementation or introduce its
multi-view/render-graph machinery.

The portable packet contains clip/world rows, camera-dependent fog and shared
shadow projection rows for world and sky. Materials retain their own shadow
enable/bias/filter values and pose palettes. The native renderer validates both
spaces before publishing one retained value. During the existing draw walk it
composes the selected constants into the shader's established parameter layout.
This preserves the current shader ABI while avoiding scene-list replacement,
texture lookup preparation, sampler recreation and geometry upload on camera
movement. A later dedicated view uniform binding can change native packing
without changing this contract.

Publication is constant-sized and allocates nothing. Encoding retains its
existing draw-count cost plus fixed-size constant composition per draw. This is
an algorithmic cost statement, not a measured frame-rate improvement. Shadow
preparation becomes invalid when the camera changes; caster and receiver passes
then consume the same published projection. Replacing or clearing the scene
resets the packet so a new scene cannot inherit a previous camera override.

The host and renderer remain exclusively owned by the main thread, and prior GPU
work completes before mutable state is published. No new synchronization or
worker ownership is implied. Verification belongs to the existing native renderer
scenario: visible camera displacement, distinct world/sky treatment, atomic
invalid-packet rejection, fixed-draw preservation and no allocation growth.

The existing `memory-tests` native Metal scenario passes these view contracts.
The implementation and test translation units also pass the pinned fatal
clang-tidy policy. These focused results establish publication behavior; actual
player-camera traversal is verified separately with the integrated scene.
