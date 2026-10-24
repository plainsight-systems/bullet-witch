# Authored collision for placed props

The traversal world uses the initial authored physics-body poses. It does not
infer solid geometry from render bounds or simulate prop dynamics.

## Source records

Bounded native passes78 and83 retain shape construction and raw convex cooking.
`0x4eedb0` consumes each body descriptor's shape count+20/table+24. Shape kind0
is a sphere (radius+64), kind1 is a box (half extents+64/+68/+72), kind2 is a
capsule (radius+64 and cylinder height+68), and kind3 selects a cooked convex
mesh by index+68. The shape's local pose occupies twelve floats at+16.
`0x4ee0b0` copies that pose into the SDK descriptor unchanged.

`0x4f3a90` supplies the convention proof: a game row-vector4x4 rotation is
transposed into the SDK's packed3x3, followed by translation. Decoding local
shape poses therefore transposes the first nine floats into the engine's
row-vector basis and retains the final three as translation. The already
recovered placement path sends placement matrix i to physics body i; local
shape points transform by local pose then that body-world matrix.

`0x4ee1e0` consumes the outer root's child records. A uint16 kind at+2 equal
zero selects a convex cloud, uint16+4 is its point count, and pointer+8 addresses
float3 points. That pointer is child-relative on disk. Convex entries receive
consecutive cache indices separately from triangle meshes. The descriptor uses
12-byte point stride and requests a computed convex hull; `0x4f7920` calls
`NxInitCooking`, `NxCookConvexMesh`, SDK creation and `NxCloseCooking`.
The selected prop resources contain137 boxes,18 convex shapes,3 spheres and
one capsule before placement instancing. All18 convex clouds are present as
raw points (7–35 points each), with no need to decode a cooked SDK blob.
All159 shape-local transforms are orthonormal within6.1e-8 in this corpus.

## Preparation design and reference

Before implementation, read Jolt `ConvexHullBuilder::Initialize`, `AddPoint`
and horizon construction at revision
`9962c11a647324cc55dc1d29f5bb87e1021cdf23` in
[Jolt/Geometry/ConvexHullBuilder.cpp](https://github.com/jrouwe/JoltPhysics/blob/9962c11a647324cc55dc1d29f5bb87e1021cdf23/Jolt/Geometry/ConvexHullBuilder.cpp).
The useful algorithm is a well-spread initial tetrahedron, outward face planes,
visible-face removal and horizon expansion. The reconstruction uses double
precision during load and independently implements that incremental hull;
it retains triangles rather than Jolt's merged polygon faces. Degenerate
volumes or invalid closed hulls fail explicitly. This is geometry preparation,
not an attempt to reproduce PhysX's cooking topology or rigid-body solver.

PreparedPropCollision builds box faces and convex hulls once per source model.
Spheres and capsules retain analytic radius/axis endpoints. It owns prepared
local geometry and reusable transformation scratch through the engine domain.
Each placement applies local shape pose then authored body pose and registers
world geometry with StaticCollision before its immutable query hierarchy is
built. Proper rigid poses are required for analytic primitives. The query owner
copies the submitted geometry; no asset or preparation lifetime escapes into
runtime queries. The geometry uses the traversal world's ordinary collision
flags; PHIT material/contact groups are not guessed as legacy triangle masks.

This preserves authored geometry and initial poses. Destruction, moving rigid
bodies, joints, material restitution/friction and simulation state are later
physics-provider work. Unsupported mesh shape kinds fail registration rather
than silently becoming missing solids. Both MCP phases apply I.1 (ri-explicit)
to the source/scene boundary and MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) to setup-only allocation.

## Verification

Synthetic in-process runtime cases cover transposed local rotation followed by
body-world composition, twelve outward box faces, convex clouds containing
interior/duplicate points, analytic round clearance and explicit rejection of
coplanar hulls. Existing resource and runtime targets pass. Targeted fatal
clang-tidy covers the decoder, preparation helper and new test translation unit.

A separate ignored corpus probe loads all28 selected prop physics definitions
through the production decoder/preparer, registers identity body poses and
builds the production query hierarchy. All159 authored shapes pass, yielding
2,038 local triangles and4 analytic round primitives. Every one of the18 convex
clouds passes closed-manifold and source-point containment validation. The
injected domain returns to zero live bytes after the complete probe. This
checks actual shape decoding/preparation; application traversal additionally
checks the authored per-instance body matrices and player contacts. Generated
probe source/output remains under `research/evidence/reconstruction/stage/`.

A second ignored probe uses the original `cm01_00` package's `st04_c.bgb`
placements and registers each representative instance through the same
production preparation/query owners. It records the source local pose, body
matrix, dimensions, transformed bounds, supporting face or analytic axis,
and camera/character inputs and results in `placed-shape-witness.jsonl`:

| Original model | Placement | Primitive witness | Camera clearance | Character wall clearance |
| --- | ---: | --- | ---: | ---: |
| `bg4_bigcontainer` | 101 | Box at authored 135-degree yaw | 0.049999 | 0.410000 |
| `bg4_barrel` | 28 | Capsule with authored tilted body axis | 0.049997 | 0.410032 |
| `bg4_drum01` | 5 | Supporting face of original convex point cloud | 0.050000 | 0.411163 |

Distances are measured along the source surface's outward normal after local
shape and body-world composition. Camera requests use 0.05 clearance;
character requests use radius 0.4 and the query's contact skin. All three
camera rays clip and all three character requests report wall contact. The
convex witness independently selects a supporting plane from source points;
it does not infer collision from render bounds. Each isolated scene includes
all authored shapes for that prop. The domain returns to zero live bytes.
