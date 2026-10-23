# Mission load consumers

The original load entry constructs resource owners before gameplay begins. A
retained package member, a constructed definition and a running gameplay
consumer are separate milestones. This continuation traces each callback's
actual completion contract; it does not substitute an inspection view for load
completion.

## Method and source

Use the Steam executable provenance, Ghidra project and reproducible export tool
recorded in `mission-launch.md`. Additional function exports are retained under
ignored `research/evidence/engine/pass-09-mission-ancillary/` and
`pass-10-mission-definitions/`. The selected package was independently inspected
with `bw-resource cpr`; bounded member copies/census belong under ignored
`research/evidence/reconstruction/mission-launch/ancillary/`.

## Section tables and cover-point construction

The original section table constructor `0x475b80` checks `STBL`; section count
and directory offset are header words at `0x1c`/`0x20`. Directory entries are
64 bytes: a 32-byte name, row count at `0x20`, column count at `0x24`, absolute
cell offset at `0x28`. Cells are eight bytes: presence word, then a value word.
`0x475f10` performs exact section-name lookup; `0x475d20`, `0x475d80` and
`0x475e40` interpret floats, integers and file-relative string references.
Missing float/integer cells return zero at the native API. The shared parser
preserves absence so each schema applies that fallback deliberately.

`_LoadKakurePoint` (`0x479990`) retains `<name>.hide`, waits for resource work,
then invokes the manager constructor `0x5b26a0`. It copies columns zero through
five into a bounded array of at most 256 active records. It does not start AI.
The manager virtual name getter `0x47d940` returns `kakure`. The selected file contains section `kakure`, 42 rows and seven columns. The
reconstructed owner stores those six scalar values and active prefix. Later
AI use remains separate.

Selected package section census:

| Member | Sections (rows × columns) |
| --- | --- |
| `st04_c_kakure.hide` | `kakure` 42 × 7 |
| `cm01_00_0.settbl` | `trap` 3 × 31 |
| `cm01_00_0_ene.settbl` | `chara` 16 × 33; `group` 58 × 9; `trap` 39 × 33; `level` 5 × 4 |
| `score.settbl` | `score` 18 × 3; `killpoint` 7 × 3; `timebonus` 7 × 5; `timebonus_mul` 1 × 2; `survival` 7 × 2; `survival_mul` 1 × 1; `citizen_ratio` 1 × 1 |

## Background object dependency preload

`_LoadBgObj` (`0x479840`) retains `.bgb` then calls `0x448b40`. Its modern-file
branch `0x4489e0` parses `BGOB` through `0x4ad360`, obtains model names through
`0x4ad3b0`, skips names beginning `musi`, and queues model dependency loads via
`0x441030`. It does not instantiate gameplay background actors at this point.
The selected file has 28 names and 276 placement records. Its name count and
four-byte name-offset directory are at header offsets eight and twelve; each
string offset is relative to the file. The placement record schema is a later
world-creation responsibility, not a condition to fabricate during preload.

## Text registration

`_LoadTextBinary` (`0x47a440`) loads `.bin` through `0x4433d0`, then registers it
using `0x444d70` → `0x464080`. The registry owns up to 128 registered tables.
`0x46ed40` relocates six table/pool bases and nested references. The Steam
loader `0x463f00` can select `data/text/<name>.bin`; it retains those loose bytes
and patches the package's header to reference them. The reconstruction must
retain the selected loose owner directly, without editing original bytes.
This is real text-table registration, independent of drawing text.

## Stage, sound and placement completion contracts

Stage construction, area, collision and material animation are traced in
`stage-content-decoding.md`. Collision construction is a bounded CPU relocation
step, not a PhysX simulation requirement at load time.

Sound load is more than retaining encoded bank data: `0x442660` requests
`audio.soundbank.<name>`; memory-bank constructor `0x4acd60` calls
`0x44d920` → `0x507c10`/`0x507b50` to construct XACT wave/sound banks. Refer to
`audio.md`. Placements similarly require registered tables, difficulty scaling
and deduplicated character/weapon dependency construction. The selected mission now constructs these owners from their actual native
contracts. Unsupported resource families still produce explicit blocked states.

## Current constructed owners

The shared section-table decoder and text-tree decoder have in-process malformed
reference, lookup and allocation-lifetime coverage in the existing resource test
executable. The first continuation build and resource tests passed; those checks
do not certify the later native scene or unfinished character closure.

The game load owner now retains the validated text registration and 42 cover
points, constructs stage/sky geometry and texture residency once, and publishes
borrowed model handles to the view. It acquires the actual STAGE4 CALL's bank
slot and USER dependencies and constructs the typed audio-bank definition before
registration. See `audio-bank-reference.md` for the pinned FACT reference and
bounded formats. Cue playback and native audio mixing are later consumers.

Background preload follows the four original suffixes `""`, `_w`, `_w1`, `_w2`
from `0x956b30`. `0x443640` probes `.chit`: missing primary stops the family;
missing later variants continue. Found variants request `.chit`, `.ehit`,
`.phit` and `.vfx.default`. Selected stage gimmicks' motion list and motion
parameter requests are all absent (durable census in the ignored ancillary
folder), so there is no motion-evaluation requirement for those resources.

Placement registration retains private table copies before applying native
`0x4810b0` difficulty scaling. `0x47ee90` chooses the `level` row matching the
selected difficulty; column one is multiplied by one half after the original
nonpositive fallback. Character health/count column three changes only when
above one and classified as the native enemy family; human/player families
remain unchanged. It sets the original table processed bit. Exact subsequent
load requests are character model/mesh/control-point assembly, motion/shape
lists, motion parameters and NPC palette textures. Resource constructors are
separate from the later `_SetEnemy` actor instantiation described in
`actors-and-animation.md`.

Selected character names are `ea0`, `ea1`, `ea2`, `hm0`, `hm1`, `hm2`, `hw0`,
`hw1`, `hw2`; selected weapon definitions are `wg1`, `wg2`, `wg5`, `wg7`.
`0x5e2110` aliases all `ea` support names to `ea0`; meshes retain their selected
stage suffix (`_4`). The selected common motion list contains 177 motion definitions. Those and
the shape/parameter dependencies now receive typed construction before
placement load completes; see `character-load-constructors.md`.


## NPC palette and callback registration

Placement table registration resets the original event registry via `0x43eb20`
→ `0x482000`: 64 inactive slots, with event 1001 bound to `clear` and event 1000
to `gameover`. Those are retained script bindings, not event execution.

Final preload helper `0x441cc0` requests the NPC palette texture table, then
walks seven program-defined family lists. It preserves each family index while
skipping absent named texture lookups. `npc_palette.cpp` reconstructs those
compatibility rules and binds real uploaded textures; source table census and
lookup evidence are ignored under `ancillary/npc-palette-table.json`. The rules
contain 240 entries, including repeated mappings, shared textures and original
spellings. Texture data remains in the purchased installation.

## Completed original load acceptance

The existing original-root integration scenario now requires the original
`cm01_00.load()` to return into `awaiting_world_setup`. Blocking at a constructor
fails this acceptance. The production loader owns 54 dependency model variants
(41 background and 13 character/weapon models), 11 deduplicated character support
sets, 178 NPC palette bindings, six waves/five cues, 18 text entries and 42 cover
points. It verifies the inactive clear/gameover callback registration as well.

The ordinary-model source classification selects ten models. Their independent
material census contains 4,097 supported indexed draws and three unsupported
custom passes (`nosub`, `sub`, `water`). The borrowed static view submits those
4,097 plus one sky draw to the synthetic device fixture. This is device-contract integration, not native image
acceptance or authored environment parity. CPU payload peaks at 229,763,116 bytes
(requested backing 234,233,644) and returns to zero after mission cancellation.
Original-root output is retained in ignored
`research/evidence/reconstruction/mission-launch/continuation-corpus.log`.

The final boundary deliberately excludes world/actor setup, physics simulation,
runtime animation and cue playback. Loading-scene animation is also still a
presentation gap. None of those are substituted with successful bindings to
make the load return. Malformed input fails and unsupported constructors block;
resident resource ownership is unwound on cancellation or failure.
