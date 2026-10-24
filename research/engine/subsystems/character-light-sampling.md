# Character light sampling

The actor render callback uses spatial SHLT data; it does not reuse static-stage
ambient RGB. `5e4430` samples the model's group-zero geometry center (`42a720`,
header+0x30), transformed by its submitted model world matrix (`56a8f0`). It calls
`567af0`, then publishes nine RGB spherical-harmonic coefficients through
`42bbc0`. `5453a0` packs the coefficients into seven shader vectors. The separate
stage-object callback `5e46d0` continues to use authored uniform ambient.

The recovered consumer is the blended sampler `566f90`, not the strict first-zone
sampler `567d90` used by other consumers. SHLT version1 contains ordered zone
references, a default sample, a float coefficient dictionary and intensity.
Each zone has an oriented admission box/sphere, an independently transformed
sample grid, and optional exclusion boxes. Grid points store 27 dictionary
indices (nine RGB coefficients), not vertex colors or height-map lighting.

Sampling preserves the source algorithms: sphere admission, strict box admission
expanded by one world unit, first four matching zones, and volume-overlap weights.
Within each zone, the original cell-center coordinate selection chooses eight
neighbors, clamps indices, reverses the stored Z index, excludes points inside
exclusion boxes and normalizes only positive surviving weights. The default gets
the positive remainder of the eight-cubic-unit blend volume. The nine resulting
RGB vectors are scaled by file intensity times the live owner multiplier. The
load initializer supplies one, but per-frame world-pass builder `45e130` writes
the current `witch/stage/<stage>` fields `+0x1c` (character) and `+0x20` (enemy)
to the respective owner `+4`. Stage initializer `45ff80→5f9c00` leaves these
floats unchanged but enables the AREA update gate. `4829d0` then changes the
live fields from AREA/STBL/FLAN data. Raw `st04_c` has one for both; its initial
area's target is character `0.005`, enemy one. Outside that area the targets
are `0.057/0.1`. The earlier raw-stage-only producer audit missed activation
and per-frame mutation; it did not establish effective runtime intensity.
See [the corrected producer trace](lighting-scene-producer-contract.md#scene-gates-and-remaining-limits).
The game maps source channels; engine scene lighting receives live multipliers.
No global unit multiplier or costume calibration can replace that producer.
No nearest-probe substitution or inferred interpolation is used.

The current installed fields contain 148 character zones and 154 enemy zones,
with no exclusion boxes; rotated grids occur in both. MissionLoading retains both raw resources independently; Stage registers available
fields with the engine SceneLighting owner using generic field identities. The literal-original witness also decodes/samples the enemy
field independently, without admitting an enemy runtime consumer. The player and its attached weapon use the character
field through the original character-model consumer. Weapon constructor
`5dfc60` installs vtable `850d68`; slot `+0x18` at `850d80` points to the same
`5e4430` observer. Enemy field admission is
retained for future enemy owners, not silently substituted into player lighting.
Direct local lights follow field sampling through a separate source consumer.
The current implementation supplies both the spatial field and the authored
active placed-light contribution described below.

Source snapshots are ignored evidence `pass-136-character-light` and
`pass-137-character-light-sampling`. A literal x86 witness executes `567af0`
and its original sampling helpers against installed bytes, hooking only library
sin/cos/sqrt and memset; original grid selection/interpolation executes unchanged.
Evidence and actual authored samples reside under `reconstruction/character-light`.
Preparation owns decoded data in the mission/view memory domain; sampling and
retained renderer updates allocate nothing and upload no geometry. Both C++
corpora were queried for bounded parsing, explicit representation and prepared
storage (SL.con.3, CDSA.32, MEM.9). The initial ten actual-data comparisons and 132 additional zone-center/boundary
witnesses, including rotated grids, match within maximum relative error
8.84e-7 (absolute3.06e-5). The oracle executes original x86 sampling, not a second
implementation of its equations. Synthetic tests cover interpolation, strict
boundary/default mixing, rotated grids, excluded-neighbor renormalization,
packing and malformed admission. Prepared sampling retains its allocation.

The native Metal fixture verifies pixel changes from retained irradiance updates,
whole-batch rejection for a late invalid destination, nonfinite rejection,
restoration of the original pixel, and stable memory charges. Actual-data captures
compare `prior-constant-0.png` and `sampled-0.png` at identical pose/view; only
body/weapon illumination changes. The synthetic native fixture restores the
original pixel exactly. The actual-data zero-step restoration also republishes
pose/view, so it does not establish byte-identical frames; `moving-0.png` follows
45 movement frames. A second
position repeats the same producer path. These ignored captures and manifests
live under `reconstruction/character-light`; they establish the spatial producer,
not original whole-scene visual parity.


## Authored direct local lights

The same actor observer calls `5f6e80` after spatial sampling. Installed
`witch/general/setup` fields `+0x64/+0x68/+0x6c/+0x70` specify contribution
threshold0.05, influence margin0.15, four direct lights and disabled overflow-SH
projection. The initialization callback leaves those fields unchanged. This
configuration selects the original direct pixel-light path; projecting these
lights into SH would be a different result.

[Placed-light ownership](placed-local-lights.md) reconstructs PHIT flag0x200,
joint attachment and source diffuse/specular/attenuation data without an asset
name filter. The installed scene contains three enabled emitters at placement
indices0,1,120. Stage prepares their frozen placed transforms once and retains
one generic `LocalLights` owner. Moving Alicia and her attached weapon each query
their own transformed group-zero center and **unscaled authored group radius**.
Future moving producer owners must publish updated attachment positions through
the same emitter-data boundary; the current placed-body scene is static.

The influence radius follows `546110`; the installed lamp gives approximately
5.592407 world units. Admission preserves the strict
`distance(center, emitter) - group_radius < influence_radius` condition, with the
source coincident-center branch. `5461f0` ranks attenuated RGBA luminance at the
sphere edge and `546070` performs the original descending comb sort. Its fourth
luminance coefficient is zero but remains an operation: zero-bias coincident
attenuation produces an unordered score, rather than a fabricated dominant
infinity. Selection retains up to the configured maximum of four. Every packet
starts cleared, so leaving the influence volume removes old light constants.
The current initial placement can correctly select zero lights; proximity must
be assessed from the original radius and actual group center, not a visual guess.

The renderer preserves the original `lightsListL` material loop (including
program74 and412; diffuse-only419/420). It computes emitter-to-surface direction
in the same camera-relative world space as the skinned vertex, source power-law
attenuation, componentwise diffuse threshold subtraction, and the material's
specular response. Local specular alpha is clamped before combining with the
separate directional contribution. Directional shadow visibility and ambient
occlusion do not multiply local light energy. The clothing423/424 family has no
such source loop and remains excluded by its material requirements. RGB values
such as the lamp's170/140/130 specular are preserved without normalization.

`WorldLightingUpdate` publishes SH and the bounded direct-light packet together.
The native backend validates the entire batch and all destinations before any
mutation, reusing retained draw bindings and current pose/view. No geometry
resubmission, resource lookup or per-query allocation occurs. CPU preparation is
O(emitters); the source comb sort has O(emitters²) worst-case work with prepared
scratch. Unknown enabled overflow-SH configuration rejects explicitly rather
than silently dropping its contribution.

Source snapshots are `pass-138-character-local-lights` and
`pass-139-local-light-irradiance`. `original_local.py` executes original546110
radius and5461f0/546070 ranking instructions, hooking standard pow/sqrt and aligned
stack allocation. The surrounding admission driver uses the documented source
equation; it is not a claim that the whole original registry executes. Cases cover
actual emitter positions, model movement, far empty selection, capacity and the
coincident ranking boundary. All nine queried selections matched the literal
original ranking, including the unordered coincident case; the original lamp
radius is5.59240723. Synthetic native Metal checks verify a point light's
pixel contribution, back-facing rejection, empty clearing and atomic rejection
of an invalid count alongside existing SH restoration checks.

This closes the active authored scene-light consumer, not full combat lighting.
The default weapon enchant state is source nibble−1 and does not create its
enchant emitter. A real source shot (`615fc0`) does create a timed muzzle light;
current accepted shot **animation presentation** does not execute that combat
owner, ammunition, projectiles, hits or world effects. The future full-shot owner
must publish that timed emitter through the same generic light interface. Its
absence from animation-only review is an explicit missing combat capability,
not evidence that the original game has no muzzle light.

Integrated Stage verification uses the actual loaded scene and ordinary
controller input: the fresh model lies outside the first lamp's source influence
radius (`5.5924072265625`), so its complete direct packet is empty. Walking toward
the lamp admits direct lighting; walking away clears it again. The existing
original-root corpus verifies these retained packet transitions with unchanged
domain footprint and balanced teardown. A preliminary test incorrectly assumed
that the initial pose was already inside the sphere; that rejected assumption
and the source-radius evidence remain in ignored logs. No source threshold was
changed to fit the initial pose. Final offscreen native captures additionally
exercise the approach, all four resident weapon classes and targeting on/off.

Scene lifetime and receiver policy now belong to `scene::SceneLighting`. Stage
maps the original character/enemy resources to generic scene field identities,
registers placed emitters and publishes the camera. Body and attachment
observations supply local bounds and final transforms; controllers never sample
fields or manage lighting. Static objects use the source global view-light
packet, spatial receivers use the separately admitted active list, and sky
retains explicit white ambient policy. See `lighting-scene-producer-contract.md`
and `scene-lighting-pass-contract.md` for original source ownership and pass
admission. This ownership correction does not establish visual brightness parity.
