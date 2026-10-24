# Authored local-light producers

The displayed stage has three permanent authored emitters. An empty local-light
registry is not a valid approximation for this scene. The spatial character
light field and the directional stage light do not replace these emitters.

## Background ownership and placement

Original model setup `0x5ed8b0` reads the optional PHIT support record at header
word seven. Flag `0x200` constructs a light through `0x5f65c0` with lifetime zero
(persistent), intensity one and active byte set. The record supplies signed
joint at byte36, diffuse RGB at40, specular RGB at52 and attenuation parameters
at64. These are independent colors; no ambient-color substitution occurs.

`0x5ecdf0` resolves the selected model joint and copies its current world
transform; an invalid or unavailable joint uses the owning object's world
transform. Its basis-row flips leave translation unchanged. The update path
`0x5ee980` refreshes that transform after physics-body world poses have been
published to model joints. Thus initial placed emitters use the same body-to-
joint association and complete world transforms as the prop draw palette,
without inverse-bind or extra placement multiplication.

`PhysicsCollisionDefinition::local_light()` decodes the optional record once.
`prepare_placed_light` produces an owned `scene::LocalLight` from a supplied
body-world palette. It rejects a valid joint without a known body publication
instead of inventing a transform. The source fallback requires an explicit
object-world pointer; callers without a real object owner pass null and fail
if that fallback is needed. The current three lights all resolve valid joints.
This producer does not implement destruction, moving prop physics or a light
registry. The integrating scene owns retained light publication and lifetime.

The current original-content census reads all 28 placed PHIT definitions.
Only `bg4_lantern_light` has the emitter bit (flags `0x34f`). All three of its
placements have zero initial state words; joint1 resolves within its three-bone
model. Their source placement indices are **0, 1 and 120**. Source colors are
approximately `(1.72,1.43,1.3)` and `(170,140,130)`; attenuation is `(1.25,1,0)`.
The retained initial scene therefore needs three local lights, even before an
action occurs. This is a census result, not a name-based admission rule.

## Selection settings

`0x44fde0` obtains global lighting settings from `witch/general/setup`.
The installed record has fields `+0x64=0.05`, `+0x68=0.15`, `+0x6c=4`, and
`+0x70=false`. The constructor `0x4ae410`, reached through `0x45c660` /
`0x45ff00`, does not overwrite those fields. The direct-light limit is four;
overflow SH augmentation is initially disabled. Consequently these three
emitters cannot simply be added to the character field's SH coefficients:
the source direct-light selection and shader inputs must also be restored.
The shared selection/evaluation implementation is owned by the lighting work.

## Player and weapon action boundary

`0x617260` fills runtime weapon specification pointers from the same weapon's
customization paths: `+0x10c → 20_muzzleLight`, `+0x110 → 20_enchantLight`,
`+0x114 → 30_bulletLight`, `+0x118 → 30_explosionLight`. Raw `00_spec` pointer
words are zero before that setup; reading zero from disk does not prove absence.
All four installed classes have nonzero authored color records.

The ordinary weapon constructor `0x615600` initializes the enchant selector
nibble to minus one. Attachment `0x615aa0` calls `0x615c90` only when that
selector is nonnegative. Therefore an ordinary selected weapon does **not**
produce its permanent enchant light merely by being visible. Enchantment
transition `0x69cad0` can change this selector and recreate the light. Such
spell-world activation is not established by playing a spell animation.

Genuine successful shooting also has a concrete light producer:
`0x616530 → 0x615fc0 → 0x5f67a0`. It destroys the prior muzzle-light handle,
creates from `20_muzzleLight`, attaches to the weapon's muzzle-transform child
and retains the new handle. The constructor uses record float10 as duration,
initializes intensity just below one and sets decay to its reciprocal. Installed
class durations are 0.15, 0.23, 0.40 and 0.12 seconds. Both source colors are
scaled by the separate global light-color value in this constructor; the
background constructor takes its supplied colors directly.

The current animation-review shot is an explicitly accepted presentation
observation. It does not execute the complete shot/projectile/world-effect
function or enchantment state. Its missing muzzle/world effects must remain an
explicit capability boundary; neither the animation clip nor absence of an
implemented producer proves the original registry would be empty. The three
already-admitted background lights are a separate current reconstruction defect
and are being restored now. No future projectile or explosion behavior is
claimed by this bounded producer work.

## Verification

Existing in-process tests cover copied colors/attenuation, body-joint world
placement, invalid-joint explicit fallback, missing valid-joint rejection,
absent emitter and nonfinite record rejection. Resource and runtime targets
pass. An original-content probe uses the production decoder and producer for
all 28 definitions and all three source placements; it resolves actual lantern
joint1/bone count3, reports three emitters, and releases all domain bytes.
This establishes CPU publication, not final native light appearance.

Evidence is ignored under `reconstruction/character-light/`: source-census
JSON, `placed_light_probe.cpp`, command/compile log and `placed-light.log`, plus
the weapon-light records. Source functions are in passes61,138 and the full
native disassembly; settings assembly and constructor mapping were checked
directly. There is no new generic lighting algorithm in this producer: it
copies the recovered source format and uses the existing authored prop palette
contract, whose established scene-ownership reference is documented in
[stage scene presentation](stage-scene-presentation.md).
