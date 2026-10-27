# One Shot Kill startup and population ownership

October 26, 2026. This report distinguishes resource preparation, script startup,
trigger activation, deferred placement, and live actors. It corrects an earlier
mapping in [mission start admission](mission-start-admission.md): the post-load
`0x480ec0` task does **not** instantiate one live actor for each `chara` row.

## Source and method

Read the hash-bound Steam pseudocode together with instruction exports. Fresh
exports are ignored `research/evidence/engine/pass-318-mission-spawn-contract/`
and `pass-319-mission-position-distribution/`; each retains seed list, command,
status, pseudocode, and instructions. The unchanged recovered script is
`research/extracted/scripts-recovered/cm01_00.lub--105c3988e33f.lua`.
Independent read-only STBL census outputs are under ignored
`research/evidence/reconstruction/mission-startup/`. No original game was run
for this trace; this is not live Windows gameplay evidence.

## Actual startup sequence

1. Original `load()` lines 7–21 requests loading presentation, riff `st04_c`,
   character lighting, `sky04_c`, stage, text `cm01_00`, score, cover points,
   STAGE4 audio, sound environment/reverb, magic table, background definitions,
   paths, and placement `cm01_00_0` (including its enemy table).
2. Native `0x43eea0` states `0x26e`–`0x289` reset placement storage, activate
   the environment-table manager, and run/wait for setup task `0x480ec0`.
   That task visits `chara` rows in both retained tables, resolving shared
   character/controller support through `0x600ff0`.
3. `0x600ff0` normalizes the character name with `0x5e2110`, searches a retained
   array of 0x38-byte records keyed by character and weapon/customization, and
   returns cached support fields. On a miss it constructs support via
   `0x601250` and inserts via `0x602ec0`. `0x5fe580` initializes that cache record.
   No group position is read here. This must not be called a placed-actor factory.
4. After loading presentation exits, native initialization restores profile/
   checkpoint consumers and callback registration, then prepares `start()` on
   the **same mission Lua context** (`0x43eea0`, `0x43c1e0`, `0x482000`).
5. `start()` lines 25–76 first disables pad/cockpit, creates background objects
   on checkpoint zero, then issues the script-owned player transform. It
   registers restrictions/goal/checkpoint triggers, shows Alicia/HUD, resumes
   world simulation while keeping input paused, performs waits/fade, pauses
   world for authored message 0, waits for actual acknowledgment, then resumes
   world/input, advances checkpoint to 1 and starts music. Do not skip the
   message or claim completion at the position command.
6. Active native updates run deferred actor admission **before** scanning enemy
   spawn traps using Alicia's current position. Triggered groups therefore enter
   storage in that scan and become eligible on a later admission pass.

The `0x4829c0` activation controls environment/AREA tables. It is not the enemy
spawn mechanism. Instructions at `0x43f9ad`–`0x43f9c5` explicitly pass the enemy
placement table (`mission + 0x54be4`) and player position into `0x482270`.

## Initial population: zero triggered NPC groups

The enemy table contains **16 archetypes**, **58 group rows**, and **39 spawn
trigger rows**. The groups declare **68 total placements**, not 16 actors.
These are future mission populations; they must not all appear at load time.

At the fresh Lua position `(-61.91, 0.65, -97.57)`, no enemy-table 2D shape
contains the player. Therefore no automatic enemy group is initially activated.
The initial cave can correctly be empty of NPCs. Walking forward activates
population through the original volumes; there is no `_SetEnemy` call in this
mission's `start()` to replace that behavior.

| Early trigger | Group / archetype | Authored behavior |
| --- | --- | --- |
| `Rectangle2` | 100 / 100 (`ea0`, `wg1`, `no_ea_m_000`) | X center −33.7911, half width57.1622; Z center−79.4650, half depth3.07239. Crossing it requests two placements in `Rectangle3`, centered (−22.1388,0,−47.8633). |
| `Rectangle6` | 101 / 100 | Later broad volume; one placement centered (−48.9610,0,−49.4726). |
| `Rectangle99` | 501 / 501 (`hm0`) | Human trigger centered (−57.0224,0,−79.9039), half extents(4.17724,0,11.9969); it does not contain the initial X position. |

Numbers above explain evidence, not production coordinate tables. Production
must read the purchased tables. Geometry has 97 shapes: 83 rectangle2D, 10 sphere2D,
and 4 rectangle3D. All 39 population trigger references are rectangle2D; the group
placement references include rectangle2D, sphere2D and four rectangle3D single-instance placements. All selected group
placement modes (column5) are0; actor facing column6 includes0,90,180 degrees.

## Table and group contracts

STBL's already-decoded section directory is independent of its shape directory:
shape count is header+0x0c, file-relative offset+0x10, stride0x60. Each shape has
center float4 at+0, extents/radius and rotation float4 at+0x10, name32 bytes
at+0x20, flags at+0x40. `0x475c50` selects flags&3 (box0,sphere1,cylinder2,point3);
`0x475c80` returns (flags>>4)&1 (3D versus 2D). Names resolve through `0x475cb0`.
Reject unsupported flags/shapes rather than interpreting them as a box.

| Section | Columns used by connected consumer |
| --- | --- |
| `trap` | 0 group/event ID; 1 shape name. Automatic enemy scan requests2D containment and ignores3D rows. Script trap callback IDs use their own registry against the primary table. |
| `group` | 0 group ID; 1 placement shape; 2 admission distance (nonpositive→100); 3 archetype ID; 4 textual count/grid descriptor; 5 distribution mode; 6 actor yaw in degrees; 7 path override; 8 later native option. |
| `chara` | 0 archetype ID; 1 model family; 2 customization selector; 3 life (load-time difficulty already applied); 4 AI resource name; 5 path name; 6 actor flags; 7–13 behavior scalars; 14 weapon; 15 customization tokens; 16 score adjustment; 17–24 behavior scalars; 25 secondary customization; 26/27 classification fields; 28 retirement distance (nonpositive→group distance); 29/30 boolean fields. |

Unimplemented behavior scalars must remain available as original table cells,
not silently reset to invented defaults. Model `ea*` receives stage suffix `_4`
in actor resource resolution; motion support aliases to`ea0`. Humans retain
`hm*`/`hw*`. Existing loading already retains 13 character/weapon models and support
metadata. Human shared-motion owners still need admission and retarget mapping
before those characters can animate; preloaded actor meshes are not sufficient.

`0x482270` loops trigger rows in file order. An unfired row passes its group ID
to `0x481530`; that helper checks any matching trap row's named volume. On a hit,
`0x481060` calls `0x47ef40` and the row is marked fired. Empty scans allocate
nothing. Duplicate group IDs can therefore have distinct row-fired state; do
not replace row state with a deduplicated group set without proving equivalence.

`0x47ef40` resolves all matching group rows, looks up archetypes by column0,
sorts by placement shape name (`0x4811a0`), and co-distributes rows sharing shape
and distribution mode. One point uses the region center; larger populations
use `0x47db80`'s original distribution algorithm. For mode0 it subdivides the
largest axis until at least4×count cells exist, shuffles cell indices through
original15-bit `rand()` output, and samples cell centers, with sphere/cylinder
rejection where applicable. This is neither random uniform sampling nor placing
all instances at the center. Ground projection follows through`0x44ae90`.

## Deferred versus live

`0x47ef40` enters direct factory`0x4806e0` only for its direct-call flag or
special classifier3/7. Normal automatic groups pass through`0x4466a0`: reserve
one of256 retained records, copy authored actor data and transform, retain life.
All selected enemy/human families use that deferred path.

`0x445580` observes mission admission, camera/player services, live count,
distance/visibility, delay, and record readiness. It admits at most one stored
actor on a successful pass, or leaves pending records intact. Its live-count
threshold0x31 is local to this path. Existing live actors can return to storage
when distant/eligible, retaining transform and life. This mechanism is not
asset unloading and should not be replaced with eager actor creation.

A useful slice can construct actual actor models/presentation after their initial behavior is resolved while keeping
unimplemented AI/combat explicit. It still needs original trigger/deferred
placement admission. A placeholder model at every archetype row or completing
native waits with no real consumer would reintroduce the prohibited shim.

## Integration boundaries

- Mission driver owns the retained original VM and `load`→setup→`start` lifecycle.
- Mission world owns stable entities, placement pending/fired state, and restart
  generation; immutable definitions borrow the load owner's bytes.
- Existing Alicia owner consumes `_SetPlayerPosition` before any simulation
  frame, updates its pose/camera dependencies, and preserves profile-selected
  outfit. The native setter does not reconstruct the player/controller.
- Simulation control owns world pause and pad admission separately. UI wait/fade
  services continue under script pause where the original requires them.
- Message0 must be resolved through the already-loaded original text owner and
  genuinely presented/acknowledged; no empty-success native binding.
- `_SetBgObj` can admit existing source-backed prop placements, while dynamic
  destruction remains explicitly inactive in this slice.
- Script callback registry must retain actual named functions and trigger IDs;
  mission-clear/checkpoint side effects beyond this slice must report their
  boundary rather than fabricate completion.

At investigation entry `MissionDriver` still binds only load operations,
`_GetCheckPoint` (fresh0), and `_SetMagicMask`; `advance()` stops at
`awaiting_world_setup`. The initial-position shim remains until the complete
ordered startup consumer path exists. This report itself does not remove it.


## Implemented placement decoder and source witness

`game/content/original/mission_placement` owns validated metadata in an injected
memory domain, borrowing immutable purchased table bytes and strings from the
mission load owner. Its public access is const spans; remaining behavior cells
remain available through the original section table. It resolves archetype and
shape references before publication. Unsupported shape flags/distribution modes
fail explicitly. The selected corpus includes four 3D boxes with one placement;
they use the authored center. Multi-placement 3D distributions remain unsupported.

`game/world/mission_population` prepares candidates using fixed storage. It
owns neither live actors nor trigger-fired state. An explicit `PlacementRandom`
provider supplies the original 15-bit `rand()` domain; the existing engine
`SourceRandom` is a different generator and is not interchangeable. Candidates
retain original row, archetype, facing, admission/retirement distances and ground
projection requirement. Failed preparation publishes no partial list; callers
must retain a successful list across admission retries rather than draw again.
Equal-name ordering outside the selected corpus is not claimed: selected group
regions are unique, while source `qsort` does not specify stable ties.

Ground helper `0x44ae90` queries ordinary world scenery through `0x5f3a40`:
2D placement casts from absolute Y2500 to −5000; 3D placement casts from authored
Y to Y−2500. The tolerance is 0.01. A real miss leaves authored Y unchanged,
and the distribution caller ignores the boolean result. Missing query capability
must not be reported as a successful miss. Grounding and deferred actor admission
remain subsequent world-owned operations.

Verification actually performed:

- Standalone strict C++20 compilation with `-Wall -Wextra -Werror` and the
  synthetic public-contract suite passed.
- The production decoder read all 16 archetypes, 58 groups, 39 triggers and
  97 shapes from the purchased enemy table; expansion produced 68 candidates.
- A Unicorn witness executed original `0x47db80` for every group under the same
  deterministic 15-bit random fixture as the production C++ expansion. All 68
  positions matched within 4.4e−7 (text float serialization precision). Ground
  was deliberately hooked as a real no-hit result, so this verifies distribution,
  not collision. It does not claim original runtime seed parity.
- Parent integration reported the host-debug runtime-tests build passed.
  Full required lint/build/test integration remains owned by the parent task.

The executable witness binds SHA256
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
Ignored diagnostics are `placement_probe.cpp`, `native-placements.txt`,
`distribution_witness.py` and `distribution-witness.json` under
`research/evidence/reconstruction/mission-startup/`.

## Player placement and startup audio

The exact adapter is retained at ignored
`research/evidence/native-decompilation/functions/0047c470.c`. It looks up an
existing player through `0x44b160`, prepares XYZ, calls the nearby resource-region
consumer `0x446080` with radius 1, converts script degrees to radians, constructs
rotation through `0x435110`, and calls player virtual +0x1c. If no player exists,
the original adapter does nothing. This is not justification for dropping the
request: world setup must have constructed the player before `start()`.

That virtual resolves to `0x604a40` (pass33). It updates actor translation and
previous translation, posts a pose message through `0x5fe880`/`0x5fde10`, updates
model forward, and copies the authored basis into the camera anchor. There is
no player reconstruction or controller initialization in this setter. No frame
at a fabricated origin is necessary before the Lua position call. Dependent
contact invalidation in our owner is an integration contract, not a claim that
the adapter resets every native movement subsystem.

The hash-bound PE's integer music table at `0x82f1a8` maps script music 7 to
cue `0x30005` and script music 3 to `0x30002`. These are numeric cue identities,
not filenames or string pointers.

## NPC construction boundaries: no universal idle substitute

`0x4806e0` dispatches positioned actor factories after resolving identity and
configuration. EA families use stage-suffixed mesh identities and shared `ea0`
motion support; HM/HW retain their model identities and use the human factory.
The normal enemy constructor `0x4563e0` creates the model/actor through
`0x5fe070`, runner/shooter components through `0x5ff170`, behavior services,
weapon attachment and selection. Model clip banks start unselected; the runner
constructor `0x663c50` initializes controller state. The bounded follow-up below
closes the grounded `StateStart` receiver for EA0: its slot-one selection is
explicit native behavior, rather than a convenient clip chosen by filename.
Later authored behavior and human shared-motion owners remain separate work.

Human shared motion is a concrete missing dependency, not evidence of absent
animations. `0x601250` resolves per-clip owner through `0x68bde0`/`0x68bdb0`.
The source table at `0x857ff0` supplies eight family maps. For `stand_l`, HM0/HM1
resolve to HM0S, HW0/HW1 to HW0S, and HM2/HW2 to HM2S. Other clips can use HBSE;
the basename handling for that owner is special. The full decoded mapping is
retained in ignored `shared-motion-owner-map.json` beside the witness files.

Loading those clips against the target skeleton is not enough. `0x5f9cd0`
retains a target/owner mapping; `0x5f9dd0` builds it from both models. Playback
`0x5e47a0` installs the mapping through `0x41f870` and, where supplied, its
vertical correction through `0x41f850`. That retarget contract must exist before
presenting human animation. Evidence is in pass318, plus pass108's
`0x5f9dd0` export.

Appearance also has two separate source paths. `chara` column 15 yields four
model draw-group masks: factory `0x4806e0` forwards record words 0x41–0x44
through model virtual +0x1c and `0x42bd40`. Column 25 yields the distinct 64-bit
palette selection at record words 0x3a–0x3b. Each set bit resolves an NPC palette
entry through `0x440910` and appends its material/texture identity through
`0x5e19c0`. Existing `NpcPaletteBinding` resources can feed that consumer; a
single generic material switch cannot replace both mechanisms.

The durable boundary is therefore explicit: definition decoding and candidate
expansion are implemented and numerically witnessed. Trigger state, deferred
admission, genuine actor construction, shared-motion retargeting and authored
behavior initialization are separate live producers. The initial cave has zero
triggered NPC groups; that fact permits honest script startup, but does not
make later triggered actors complete or justify displaying bind-pose placeholders.

## Mission instruction modal implementation

`_DispWindowMessage2` at `0x4785d0` builds the current mission namespace plus a
four-digit message key and submits shared dialog task `0x4a80d0`/`0x4a8340`.
The purchased `cm01_00` text table's key `0000` contains three page branches,
`00`, `01`, `02`; each has localized children. English uses identifier 200.
The pages describe the challenge, objective, and unavailable spells. They are
not three competing translations, and returning the first English leaf alone
would discard most of the instruction.

Bounded export pass327 records `0x464460`: it advances the text page index while
another page exists and returns zero at the end. The dialog updater resets its
input gate to 0.2 seconds after a page turn and emits logical sound event 9.
The hash-bound menu event table `0x8367b0` maps that event to common-bank cue 6.
The initial shared acknowledgment gate is 0.3 seconds. Final acknowledgment
runs the shared close transition; time passing alone does not dismiss a page.

`game/ui/mission/mission_message` owns that modal lifecycle while borrowing the
mission's immutable text table. It validates all English pages before starting,
retains the source child range, and delegates opening/closing and sounds to the
existing `MenuDialog` and `MenuAudio`. It has no world-pause or clock authority.
The integrating coordinator supplies one admitted control interval and waits
until `pending()` becomes false. Cancellation hides the modal before mission
text bytes can be released.

`mission_message_surface` owns resource tag 26, requests only `sprite.font`
and `sprite.system`, and uploads the original font atlas and dialog border.
It draws the existing native panel geometry without the pause-menu backdrop.
Page changes shape the original Unicode and color markup into bounded sprites;
ordinary frames only reposition/fade those prepared glyphs. Purchased markup
uses yellow, blue, hexadecimal ARGB and restoration to default color. Unsupported
markup is an explicit failure rather than raw text or silently omitted content.

Standalone strict compilation and focused modal tests passed. Tests cover all
three pages, English selection, early-input gates, no timed acknowledgment,
final close completion, exact page cue, missing text rejection, invalid interval
and cancellation. A separate original-content probe parsed the purchased text
and original font: all **194 visible/blank authored characters across the three
English pages** have glyph records. Full-width characters and U+3000 spaces are
retained; no text normalization is required. Ignored probes are
`message_probe.cpp` and `message_corpus_probe.cpp` in mission-startup evidence.
These checks do not claim a native display walkthrough.

The source continuation indicator is also connected. Default indicator type 0
(`0x49c4b0`/`0x49c520`) draws through `0x49c540` into input-icon dispatcher
`0x472270`, logical action 5, UI mode 2. It is the confirm binding, not a generic
triangle or a fixed controller button. The surface receives a copied active
input family and uses the existing `menu_prompts` binding resolution with the
purchased input atlas. It adds no input mutation capability. Native position
1080/final-bottom−32 plus the indicator offset −4/−32 gives canvas
1076/final-bottom−64, with original one-third icon scale. The indicator hides
when closing starts. The purchased encoded atlas is retained by the surface;
parent resource/device services outlive all of its GPU handles.


## First triggered EA0: grounded initial pose is source-selected

The first trigger's group 100 requests two EA0/WG1 actors with authored behavior
`no_ea_m_000`. This trace establishes their construction-time pose, without
substituting an invented AI idle policy. Existing exports were sufficient; no
new Ghidra session or gameplay run was required.

### Constructor to initial receiver

1. Normal-enemy factory `0x4563e0` constructs `0x5fe070`, calls `0x5ff170` for
   Runner/Shooter and related components, installs authored behavior, then
   attaches/selects weapon zero. Base construction `0x658290 → 0x658060`
   creates `StateStart` (state ID stored at `0x957414`; RTTI confirms the name).
2. Runner `0x663c50` has priority 2. Registration `0x667910 → 0x661740`
   binds Runner / StateStart / pre-update (`0x957408`) to thunk `0x65c4e0`.
   Raw instructions at `0x65c4e0` dispatch component virtual `+0xb0`;
   Runner vtable `0x8560f8 + 0xb0` contains **`0x665ed0`**. This is the same
   pre-update message used by the separately registered AiBehavior startup
   receiver; AiBehavior has priority 999. Initial pose selection does not
   require executing later AI decisions.
3. `0x665ed0` queries shared scenery from actor position down five units, with
   query tolerance 0.01. On hit it adjusts grounded height using actor control
   data `+0x2c`, queues **StateRun** through `0x44e190 → 0x4500b0`, and requests
   base motion **slot 1, start position 0, fresh true, blend duration 0.3 seconds**
   through `0x5e4760`. A missing ground hit instead requests the airborne
   transition through `0x5fdb30`, preserving the actor basis and setting its
   flag. Do not force the grounded pose onto a failed query.
4. `0x5e4760` supplies base channel zero and model motion owner `+0xa10` to
   `0x5f9290 → 0x5e47a0`. Slot one in the native table `0x975b28` is `stand_l`;
   the EA override table `0x852478` does not override it, and WG1 has no special
   EA weapon override. This proves the requested initial clip rather than
   guessing from its name.

**Argument-order correction:** the fourth `0x5e4760` argument is blend duration,
not a start offset. Instructions `0x5e49bf–0x5e4a08` forward it as the final
argument to `0x41f2f0`, which stores controller `+0xc38` and computes the weight
slope. The second argument supplies the start position. Existing prose that
labels the second argument blend and the fourth start should not be used as an
API contract. First playback begins at full weight in `0x5e47a0`; do not fade a
new actor from an arbitrary bind pose merely because 0.3 was requested.

Concrete ignored evidence: pass05a `0x658290`/`0x658060`; pass05e `0x4563e0`
and `0x5ff170`; pass74 `0x663c50`, `0x667910`, `0x661740`; pass121
`0x665ed0` (especially instruction range `0x666014–0x666036`); pass05b
`0x4500b0`; pass108 `0x5e47a0`; native-decompilation `0x41f2f0`.

### Retained support and skeleton contract

`MissionLoading::preload_character` in
`src/game/mission/loading/mission_package_inputs.cpp` retains the `ea0_4` model
and the `ea0` `CharacterSupport` owner separately. The latter already resolves
`model.ea0.motion.list.compress` and its bone list, ordered motions, and
parameters through `src/game/content/original/character_support.cpp`.

The required publication is **`model.ea0.motion.compress.ea0_stand_l`**. The
purchased EA0 compressed bundler contains it at CALL record 343, referencing
USER record 342 (`ea0_stand_l._ba`, 37,032 bytes). Its authored header contains
120 frames, 60 frames/second, repeat enabled, and 84 tracks. These records are
already in the retained motion support; loading another motion package for
this first pose is unnecessary. The earlier census is retained under
`research/evidence/reconstruction/mission-launch/ancillary/`.

`0x601250 → 0x68bde0/0x68bdb0` does **not** map EA0 to a human shared-motion
owner. Its family map contains HM0/HW0/HM1/HW1/HM2/HW2/HR0/HR1 only, so EA0
resolves to EA0. `0x5f9cd0` compares actor/owner identity and returns -1 for this
same-owner pair. Playback `0x5e47a0` then installs a null mapping via `0x41f870`
and clears the vertical correction via `0x41f850`.

Actual purchased EA0 and EA0_4 meshes both have 84 bones. Their `motion.bil`
bone-name/order payloads are byte-identical: 3,368 bytes, SHA-256
`035fbf66186b9e4bf168cdc8424fd160c28c63f10789c901eaf6c8dd8e7d86e5`.
Their `motion._bl` skeleton payloads differ. Therefore preserve the target
EA0_4 skeleton and bind matrices; identical names/order justify direct track
association here, not replacing the target bind pose with EA0's skeleton.
The read-only `bw-resource resolve` inspection used the stable catalog and
compared the payloads, not filenames alone.

For other owners, `0x41f870` itself only installs a mapping pointer. The actual
mapping producer is `0x5f9dd0 → 0x426420`, which uses both skeletons, matches
bone identities, and derives scale/translation corrections including HipsBone.
It is not an index-offset trick. Human shared animation must retain that
separate contract; EA0's direct binding is not a general retarget solution.

### Reusable ownership and remaining boundary

A live actor should own its admission result, transform, actor state and
animation controller; borrow immutable model/support from mission residency;
and publish evaluated pose/weapon attachment data to presentation. The
constructor-selected base pose is an animation request under the authoritative
world interval. Neither the renderer nor the loading owner should select AI
states or advance another clock. The original AI initialization receiver must
remain a separate behavior owner.

This closes the **first EA0 grounded initial-pose selection and track binding**
source blocker. It does not establish that the authored `no_ea_m_000` behavior
leaves an admitted actor idle indefinitely, nor implement its actions. A
non-combat population slice can expose the verified initial pose as its
explicit scope; it must not report the behavior tree as running. Ground-miss
transition, subsequent StateRun requests, weapon/appearance construction and
later human shared-owner retargeting must retain their own supported or
unresolved status. No production code was changed by this follow-up.


## Integration follow-through

The EA0-only follow-up above records the bounded source finding at that point,
not the present implementation status. Shared human motion, original appearance
and attached weapons, source trigger/deferred lifetime, per-instance native
presentation, serial Lua callbacks, checkpoint observations and original
goal/system-message UI are now integrated. See
[implementation and verification](mission-startup-implementation.md),
[shared motion](npc-shared-motion.md), [appearance](npc-appearance.md), and
[callback ownership](mission-script-callbacks.md). Actual authored group replay
checks all 58 group rows, keeping distance-gated candidates deferred. The
StateStart ground-miss transition and subsequent AI/combat decisions remain
explicit future receiver boundaries; initial animation is not a behavior-tree
implementation.
