# Scene renderer

How one frame is made from a view of the scene: the fixed list of passes, the
data each pass writes for the ones after it, where light is evaluated, and
where the image changes resolution. Solid surfaces are lit deferred: they
write what the surface is, and one later pass lights every pixel from that.
Blended surfaces are lit as they are drawn. Both use one lighting function
and one light grid.

## Position

Above the render graph, which schedules the passes and owns their transient
targets, and the [RHI](rhi.md). Beside [materials](materials.md), whose
templates supply a shader for each pass, and the [lighting
system](lighting.md), which supplies lights, shadows and indirect light as
data and as visibility terms.
Fed by the [scene](scene.md), which supplies the views and, for each view,
the draw records of what is visible in it, and by the [geometry
feed](geometry.md), which supplies each dispatched instance's vertices for
the frame.

```mermaid
flowchart LR
    GF["Geometry feed: copies, bone buffer, boxes of dispatched instances"] --> SC["Scene: instance records, views, per-view draw records, depth hierarchy"]
    SC --> SR["Scene renderer: pass list, surface data, lighting pass, temporal stage"]
    MT["Materials: one pipeline per template, permutation, pass and blend mode"] --> SR
    LT["Lighting system: light grid, shadow maps, indirect light, visibility terms"] --> SR
    SR --> RG["Render graph: pass scheduling, transient targets"]
    RG --> RHI["RHI"]
```

## Invariants

1. An opaque or masked surface is never lit in its own pixel shader. It
   writes surface data, and the lighting pass evaluates light once per pixel
   from that data.
2. A translucent, additive or modulate surface is lit as it is drawn. It
   calls the same lighting function and reads the same light grid as the
   lighting pass. There is one lighting function.
3. Surface data is one set of per-pixel targets with a fixed meaning. A pass
   that needs to know the surface at a pixel reads those targets; no pass
   draws geometry again to find out.
4. The shading model of a pixel is an id in the surface data. The lighting
   pass branches on it; the set of ids is the shading models the material
   system declares.
5. The light grid is built once per view. It is the only way a pass finds
   the local lights that reach a point, for the lighting pass, the forward
   pass and fog alike. The sun is evaluated everywhere and is not in it.
6. Every opaque and masked draw writes a motion vector, including skinned,
   morphed and vertex-offset geometry. A motion vector points from a pixel
   to where that surface point was in the previous frame, with the jitter
   removed.
7. The scene is drawn at input resolution with a sub-pixel jitter on the
   projection. One temporal stage turns that into an anti-aliased image at
   output resolution. Every pass before it runs at input resolution and
   every pass after it at output resolution. There is no multisample
   anti-aliasing and no other anti-aliasing pass.
8. Shadowing, ambient occlusion, reflections and indirect light are
   visibility terms. Each has a rasterized source that runs on every target.
   A target that reports the ray tracing capability may run a traced source
   for a term in its place. Both sources write the same target with the same
   meaning, and the lighting pass cannot tell which ran. A target that
   reports no ray tracing capability runs the rasterized source of every
   term.
9. The source of each term is chosen at startup from the capabilities the
   target reports and the settings, and the choice is reported. A source
   that is chosen and cannot be created fails by name. A source never
   changes silently.
10. A stochastic source is filtered before the lighting pass reads it. The
    filter belongs to its term and reads surface data and motion vectors.
11. Light is accumulated in linear scene values. Tone mapping and the output
    transform run once, after post-processing. The user interface is drawn
    after them.
12. The pass list is fixed and declared in one place. A setting or a
    capability selects a term's source or leaves a pass out; nothing
    reorders passes.

## The frame

```mermaid
flowchart TD
    DF["Deformation: shapes, skinning and bounds of every dispatched instance, once"] --> CU["Culling: every instance and cluster against every view, draw records per view per pipeline"]
    CU --> DP["Depth pass: two draws of the main view's records, the depth hierarchy built after each"]
    DP --> SP["Surface pass: surface data, motion vectors, emissive, receiver stencil bits"]
    SP --> DC["Decal pass: visible decal boxes blended into surface data"]
    DC --> LG["Light grid and shadow maps"]
    LG --> VT["Visibility terms: shadowing, ambient occlusion, reflections, indirect light"]
    VT --> LP["Lighting pass: every opaque pixel, from surface data, light grid and terms"]
    LP --> SF["Sky and fog"]
    SF --> FP["Forward pass: blended draws sorted far to near, two pipelines, lit as drawn"]
    FP --> DA["Distortion apply"]
    DA --> TS["Temporal stage: input resolution in, output resolution out"]
    TS --> PP["Post-processing"]
    PP --> TM["Tone mapping and output transform"]
    TM --> UI["User interface"]
    UI --> PR["Present"]
```

| Pass | Reads | Writes | Resolution |
|---|---|---|---|
| Deformation | changed poses and shape weights, stream 0 of each dispatched mesh, bone maps | the bone buffer, the current copy of each dispatched instance, its cluster and instance boxes | its own |
| Culling | instance records, the frame's views (the main view, and the cascades and atlas slots the lighting system fits and assigns at frame start), the previous frame's depth hierarchy | draw records and counts per view per pipeline | its own |
| Depth | the main view's opaque and masked draw records, in two draws: phase one, then phase two after the depth hierarchy is built | depth, the depth hierarchy twice | input |
| Surface | the same records, depth, probe grids, each dispatched instance's previous copy and the bone buffer's previous slot | surface data, motion vectors, emissive into scene color, baked indirect light into the indirect light target, receiver stencil bits | input |
| Decal | the main view's decal records, depth, stencil | base color, normal and roughness of surface data | input |
| Light grid and shadow maps | lights, each shadow view's draw records | light grid, shadow maps | their own |
| Visibility terms | surface data, depth, the depth hierarchy, motion vectors, the previous frame's scene color, shadow maps or the ray tracing scene | one target per term | input |
| Lighting | surface data, light grid, term targets | scene color | input |
| Sky and fog | depth, light grid, shadow maps, probe grids, fog history | scene color, fog volume, fog history for the next frame | input |
| Forward | the main view's blended draw records, emitter particle data, depth, light grid, shadow maps, probe grids, fog volume | scene color, reactive coverage, distortion target and stencil bit, scene color as history for the next frame | input |
| Distortion apply | scene color, distortion target, stencil | scene color | input |
| Temporal stage | scene color, depth, motion vectors, reactive coverage, jitter, exposure | scene color | input to output |
| Post-processing | scene color | scene color | output |
| Tone mapping and output transform | scene color, exposure | display color | output |
| User interface | display color | display color | output |

## Surface data

What the surface pass leaves for every later pass. It holds one surface per
pixel, which is why blended draws do not write it.

| Datum | Read by |
|---|---|
| Depth | every later pass |
| Base color | lighting, filters of traced terms |
| Normal | lighting, visibility terms, their filters |
| Roughness | lighting, reflections, their filters |
| Metallic and specular | lighting, reflections |
| Material ambient occlusion | lighting |
| Shading model id | lighting |
| Shading model data: subsurface color or tangent | lighting, by shading model |
| Motion vector | temporal stage, filters of stochastic terms, post-processing |
| Stencil bits: receives no decal, refuses decal normal, refuses decal roughness, refracting pixel | decal pass, distortion apply |

Emissive is not surface data: the surface pass adds it to scene color. An
unlit surface writes emissive and its shading model id and nothing else.

## Passes a template provides

The passes below are the pass dimensions the renderer declares to the
material system. A template has a shader for each pass its blend modes
reach.

| Pass | Blend modes drawn | The template's shader writes |
|---|---|---|
| Depth | opaque, masked | depth, with the masked discard |
| Surface | opaque, masked | the attributes of its shading model as surface data, a motion vector, emissive |
| Shadow | opaque, masked | depth from a light's view, with the masked discard |
| Forward | translucent, additive, modulate | lit color blended into scene color, reactive coverage, and a refraction offset if the template writes one |
| Decal | the decal domain | base color, normal, roughness and an opacity for each, into surface data |

Renderer code in the surface pass, not the template, writes the pixel's
baked indirect light. Renderer code in the depth and surface passes, not the
template, applies the dither of a detail level transition, so one surface
still reaches each pixel. No template shader skins or morphs: a dispatched
instance's draws fetch the copy the geometry feed wrote, and a rigid-to-bone
cluster's draw applies its bone's transform from the bone buffer. A template's vertex
offset is evaluated by one function in the depth, surface and shadow vertex
shaders alike, and once more in the surface pass with the previous transform
and previous frame values for the motion vector. A forward draw writes no surface data and no motion
vector. Its reactive coverage tells the temporal stage where history
cannot be trusted. The forward and decal passes are each one pipeline across
their templates, branching on the template id, so their draws can be issued
in depth order; [transparency](transparency.md) and [decals](decals.md)
state the order and the pipelines. Every other pass has one pipeline per
template and permutation.

## Visibility terms

```mermaid
flowchart LR
    CAP["Capabilities the target reports at startup"] --> SEL{"Ray tracing reported and enabled for this term"}
    SET["Settings"] --> SEL
    SEL -- yes --> TR["Traced source"]
    SEL -- no --> RA["Rasterized source"]
    TR --> FI["Filter of the term, when the source is stochastic"]
    RA --> FI
    FI --> TT["Term target"]
    TT --> LP["Lighting pass"]
```

The scene renderer owns the selection and the meaning of each term's
target. What each source computes belongs to the lighting system. The
rasterized source of indirect light is part of the surface pass, which
writes baked light as it draws on every target; a traced source, where
selected, writes the whole target afterwards. Traced
sources need the positions of skinned and morphed geometry in buffers, so
the scene supplies deformed positions as buffers on targets that trace,
taking them from the geometry feed that computes them.
Shaders of traced sources are their own set, built only for targets that
can report the capability and loaded only when it is reported.

## Temporal stage

The frame hands the stage the same inputs on every target. How the stage is
implemented is each target's own code with its own tuning; nothing is shared
between implementations but this hand-off.

| Input | Meaning |
|---|---|
| Scene color | linear, before tone mapping, at input resolution, drawn with the frame's jitter |
| Depth | the frame's depth, with its direction stated |
| Motion vectors | from a pixel to its previous position, jitter removed |
| Jitter | the frame's sub-pixel offset, from a low-discrepancy sequence whose length grows with the ratio of output to input area |
| Exposure | the value the tone mapper uses |
| Reactive coverage | where blended draws covered the pixel |

Texture sampling in the passes before the stage uses a mip bias set from
the ratio of input to output resolution. At a ratio of one the stage is
anti-aliasing alone. One implementation is written in the project's own
shaders and runs on every target; it is the reference for image comparison.

## Components and ownership

| Component | Lives in | Consumed by |
|---|---|---|
| Pass list, surface data layout, term targets, source selection | `src/engine/rendering/passes` | render graph, materials, lighting system |
| Instances, views, culling, draw records, the depth hierarchy | `src/engine/rendering/scene`, described in [scene](scene.md) | every pass |
| Deformed copies, bone buffer, boxes of dispatched instances | `src/engine/rendering/geometry`, described in [geometry feed](geometry.md) | culling, every pass |
| Forward pass: record sort, the two pipelines, sprites, ribbons, mesh particles, distortion | `src/engine/rendering/passes`, described in [transparency](transparency.md) | render graph |
| Decal pass | `src/engine/rendering/passes`, described in [decals](decals.md) | render graph |
| The lighting function | `src/engine/rendering/scene` | lighting pass, forward pass, material templates |
| Light grid | `src/engine/rendering/lighting` | lighting pass, forward pass, fog |
| Temporal stage, post-processing, tone mapping and output transform | `src/engine/rendering/postprocess` | hosts |
| Depth, surface, shadow, forward and decal shaders of each template | `src/engine/rendering/materials/templates` | pass list |

## Boundaries

Above: the hosts, which present the result, and the world, which publishes
what the scene holds. Below: the render graph and the RHI. Beside: the
material system, which gives each draw its pipeline for a pass and never
sees a light; the lighting system, which decides what lights exist and how
shadows, indirect light, sky and fog are computed; the scene, which decides
what is visible and writes each view's draw records; the geometry feed,
which computes a dispatched instance's vertices once and hands every pass
the same copy; the effects system, which writes each emitter's particle
data. The effects inside post-processing and how passes are scheduled are
outside this document.

Full-screen video is not a scene. The media system decodes it, and its
frames enter at the user interface step as display color; while one covers
the screen, no pass before that step runs.
