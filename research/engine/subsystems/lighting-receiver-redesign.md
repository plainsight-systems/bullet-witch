# Receiver ownership and native scene replay

The redesign retains the original distinctions between terrain, actors and
placed objects. Terrain publishes the scene ambient polynomial and view-selected
lights. Actors sample the blended spatial field at their transformed group-zero
center. Each placed object retains its own strict-field observation at control point zero,
with local lights refreshed independently on every observation. Instance identity
is separate from immutable resident model identity, so two copies of a mesh do
not overwrite one another's receiver packet.

## Strict sampling is a separate grid kernel

The original `567d90 → 567810 → 566b50` selects the first containing zone, then
uses a fixed five-point neighborhood. This is an additional implementation-level
finding beyond the audit's distinction between strict and blended zone selection.
Reusing the actor's trilinear/exclusion sampler produced an actual-data mismatch;
that reuse was removed before acceptance.

For each grid axis, form the source cell coordinate
`((1 / extent) * local * divisions + divisions + 1) / 2`, convert toward zero,
decrement negative coordinates, then clamp to zero and, when nonzero, the division
count minus one. Reverse the Z cell index. Retain the center with weight two and
its four X/Z neighbors with weight one. Only neighbors are bounded by strict
`0 <= index < divisions`; the center remains unconditional. Y is a single layer.
Normalize after accumulation. This path does not apply the actor sampler's
exclusion volumes. The decoded field remains shared and immutable; the two
queries have distinct algorithms and fixed scratch.

The source tables at `840804` and `84082c` establish the offsets and weights.
The independent executable witness uses actual installed `st04_c_cl` bytes and
three supplied authored lantern matrix translations at source owner intensity .005.
This establishes the sampler at those inputs; it does not establish the
control-point-derived original receiver position. Production
strict sampling matches all 81 RGB coefficients; maximum printed-decimal error
is below `5e-12`. These are authored placements, not an original live frame.
The ignored reproducible comparison is
`lighting-source-fidelity/native_strict_comparison.py`, alongside the retained
original `placed_receiver_witness.py` and native helper. An asymmetric synthetic
grid separately checks the five-point kernel, terminal neighbor exclusion and
absence of actor exclusion-volume filtering.

The per-instance cache uses `(root XYZ, 10 * owner intensity)`. Source accumulation
order is Y² + X² + Z² + intensity², and refresh includes equality at .25. The first
observation always refreshes. Tests cross a zone boundary below and exactly at
that threshold, then change only live intensity. Direct lights remain per-call.
Receiver-position cubemap selection now uses the source region/table lookup and
mandatory slot-zero residency gate. Props retain the selected slot with the SH
cache; actors query their own center immediately. Material-authored environment
bindings override the receiver default. See [reflection field](lighting-reflection-field.md)
for native instance-isolation, binding-precedence and rollback verification.

## Scene preparation and raster ownership

The native renderer replays all admitted opaque S geometry, then masked S,
before either T family. Sky remains first. Preparation uses the restored RGBA16F
scene target and shared hardware depth, not the temporary R32F setup target.
Its depth-valued color can survive where final masked color rejects. The native
contract demonstrates that survival and distinguishes all-S-before-T ordering
from incorrectly pairing each preparation family with its color family.

The scene's integer-center policy shifts the geometry viewport by half a pixel
on Metal. Original clip coordinates remain the interpolation/shadow lookup input;
fullscreen postprocess sampling retains its separate recovered convention. A
production HDR edge witness checks both sides of a sloping triangle boundary,
with no energy adjustment. The shadow receiver's plane footprint uses the same
sample convention as its caster.

Shadow setup, cull and normalized-depth bias are typed scene publications. Setup
disable retains both the map and its prior projection. Invalid view publication
validates all fields before replacing view or filter guard. Native tests exercise
setup retention, resumed map generation, both cull modes and nonzero source shader
bias. These checks establish reconstructed native behavior; original D24 versus
native Depth32 precision, original unsupported sampler address six, and matched
whole-frame evidence remain separate limitations.

Both C++ design and implementation consulted Core I.1/R.20 and performance
MEM.9/CDSA.32/CACHE.2. Owners retain prepared immutable data, receiver observations
borrow only their scene lifetime, and publication paths use fixed packets rather
than rebuilding geometry or allocating per draw.
