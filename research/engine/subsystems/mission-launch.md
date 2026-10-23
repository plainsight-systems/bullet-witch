# Concept launch and mission ownership

October 23, 2026. Original executable/installation remain read-only. This record
separates recovered contracts, implemented ownership and outstanding native
consumers; loading definitions is not a completed mission.

## Menu status accessor: previous uncertainty resolved

The menu updater `0x49b320` stores internal result **5** for a completed extra
selection, eventually copying it from task+`0x90` to task+`0x94`. The native status
accessor `0x49a560` maps internal 5 to API result **6**, and internal 6 to API
result **5**. Results 1–4 and 7 pass through. `_GetTitleMenuStatus` callback
`0x479500` calls this accessor. The original root's result-6 concept branch is
therefore consistent; no bytecode change or guessed remapping is needed.

Evidence: existing `reconstruction/profile/export2` pseudocode and instructions,
re-exported in `engine/pass-concept-launch`. The actual jump table at `0x49a5a0`
contains destinations `49a573,49a579,49a57f,49a585,49a591,49a58b,49a597`;
`49a591` returns 6, while `49a58b` returns 5. Raw witnesses are retained under
`research/evidence/reconstruction/mission-launch/native-table-witnesses.json`.

`MenuSelection::original_status` remains internal 5. `StartupSession::status`
performs the native accessor translation only for the title-menu API. The
user-approved completion-gate removal remains a separate intentional policy
change. Verified challenge titles now identify the corresponding compatibility
IDs in the existing bounded menu rows.

## Selected package and concept alias

The extra child `0x4c5210` submits its selected ID through `0x471ac0`. Save/content
operation `0x4c2cf0` builds `concept:\\CM%02d_00\\cm%02d_00.rat` and mounts it with
priority key `0x28` (40). It then formats `cm%02d_00` and invokes `0x441720`, which
stores that name at resource-owner+`0xa878` and queues `0x441770`.

`0x441770` resolves `script.<selected>.dpk` and retains the package data at
owner+`0xa898/+0xa89c`. Mission script resolver `0x441a80` compares its requested
name with literal `concept`. If the selected package exists, it aliases that
payload into the mission script slot and copies the selected name into the slot's
member-name prefix. Thus root `_StartMission("concept")` plus extra ID 1 selects
`script.cm01_00.dpk` / `cm01_00.lub`, without changing the root string.

The captured request retains both original `mission` and resolved `script_name`.
The current host consumes One Shot Kill; other routes retain their existing
explicit handoff boundary. The selected catalog is scoped to the new mission
resource owner; the root/front-end resource owner remains alive independently.

## Mission VM and loading

Mission driver `0x43eea0` initializes a Lua context with capacity `0x40000`
(**256 KiB**), registers its native table, requests selected script slot0 and
`default_callback` slot1, loads both chunks in that order, then prepares the
mutable `load` entry. It does not immediately run `start`: intervening actor,
world, resource and modal gates have separate native states.

`MissionDriver` owns that bounded context and one exclusive script-resource tag.
It borrows `MissionLoadServices` and resources through destruction. Loading
original `load()` is resumable; named unsupported work remains blocked, a real
error is terminal, and completed load stops at `awaiting_world_setup`. No gameplay
start, event callbacks, mission outcome or WaitMission success is synthesized.
Cancellation destroys Lua polls first, cancels load ownership, then releases
script interests. The shared root/profile remain alive while mission work runs.

Recovered early operations:

| Operation | Recovered contract / implementation boundary |
|---|---|
| `_GetCheckPoint` / `0x478d20` | Reads checkpoint state. Current MissionDriver is used only for a fresh concept route, initial checkpoint0; Continue is not routed through it. |
| `_ExecLoadingScene` / `0x4787e0` | Immediate task activation. Argument5 is a loading item, **not** the title's randomly selected background-image index. Owner getter `0x43d7f0`, initializer `0x4a6b40`, update `0x4a7470`, draw `0x4a6c00`. Active item is retained; full animated presentation remains explicitly unready. |
| `_SetRiffName` / `0x47c610` | Phase0 copies the name through `0x43eac0` and returns pending; phase`0x466` resumes. Driver retains the copied name and one initial wait. |
| `_LoadCharaLight` | Worker `0x441160` requests `charalight.<name>_cl/_el`; `0x568030` retains each published USER pointer with scalar defaults. Retained raw CPU owners can satisfy that load without claiming a lighting evaluator. |
| `_LoadSky` | `0x442520` matches the exact model name, then uses `0x444580`. Actual `sky04_c` has one mesh, one texture and one material; no collision or material-animation catalog entries. CPU definitions are retained; original mesh/texture wrapper/device readiness is tracked separately. |
| `_LoadStage` | Shared stage decoder can retain a complete static CPU snapshot. Collision, area, material-animation and other consumers remain explicit; a static inspection snapshot cannot certify script/gameplay readiness. |

The loading draw references system sprites447/448 (`loading_15s_1.png`,
`loading_15s_2.png`) with rotation/animation, plus an indexed text table. Current
portable sprite commands lack rotation; this is a concrete presentation gap.
Background sprite332 belongs to the title-selected loading group and is distinct
from argument5. No arbitrary loading05 image is substituted for that argument.

## Reference implementation and local design

Read Doom 3 at pinned revision
[`a9c49da5afb18201d31e3f0a429a037e56ce2b9a`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/framework/Session.cpp),
`idSessionLocal::ExecuteMapChange`, `UnloadMap` and `LoadLoadingGui`. It separates
persistent session ownership from map shutdown/creation, loads render/sound
resources before marking a map spawned, and explicitly handles loading UI.
Adopt those lifetime/readiness separations. Preserve Bullet Witch's original
Lua load/setup/start contracts rather than Doom's map/script semantics. The
inspected source snapshot is local ignored research, not copied production code.

Both required MCP corpora were queried for design and implementation. Full E.6
(`#re-raii`) governs partial construction/cancellation; full MEM.6
(`guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md`) governs
injected context/package/decoded storage. Previously consulted I.1 and COPY.7
continue governing explicit menu policy and bounded value/borrowed presentation.

## Verification status

The existing original-root integration scenario now includes selected concept
routing after the retained campaign/costume persistence checks. It verifies
internal5/API6, original `concept` alias to `cm01_00`, actual original mission
chunk/default callback loading, RiffName/character-light progression, genuine
native pending state and cancellation. This extends the existing scenario; it is
not a new CTest scenario or native visual acceptance. Build/run results are
recorded under ignored `research/evidence/reconstruction/mission-launch/`.
The current original-script run reaches the genuine stage pending state after
character-light and sky publication. Its synthetic world-output fixture exercises
original sky HALF4 decoding, all stage CPU inputs and the selected opaque
inspection data. Cancellation returns all charged CPU memory to zero. Both
startup-corpus and native host build successfully. This is original-script/CPU
ownership evidence; native shader readback and visible stage acceptance are
separate records.

### Retained optional mission inputs

The selected mission package is now moved into the mission-loading owner after
its original Lua chunk is loaded; later member views borrow that package until
cancellation. Native reset `0x440b70`, called by cleanup/reset `0x444c40` and
`0x444f00`, clears all optional pointer/length pairs. Workers `0x442190` (score),
`0x442390` (sound environment), `0x442410` (reverb), `0x442310` (magic) and
`0x441dc0` (paths) replace those pairs only when `0x4ac480` finds a member.
The corresponding Lua callbacks append `.settbl`, except paths append `.arc`.
This supports a real retained-input load contract: an existing member remains
bounded by the owned package; a missing member preserves the reset empty value.
Missing optional input is not a fabricated registration success. Hide points,
background objects, placements, text registration and sound-bank creation have
additional consumers and remain separate.

The native sky load owner now decodes original pixel formats, uploads its mesh
and texture, binds the exact runtime table and requires confirmed table readiness.
Only then can the original sky load wait finish. Color-pass selection remains a
separate renderer contract. The CPU integration route uses an explicitly synthetic WorldOutput to exercise
that contract and reach the subsequent genuine stage wait. It does not establish
native sky presentation.


## Bounded static inspection

`StageInspection` selects the main baked `st04_c` geometry and the supported
original normal-mapped material subset. The independent source census found
25 matching materials and 164 draws; four have alpha blend state, accounting
for 60 draws. The current opaque subset therefore contains 21 materials and
104 draws, with 1,124 other main-world draws omitted. Selection reads the
retained material record ranges and rejects render-state/texture-animation/
texture-transform records outside this contract. The original integer tags
have explicit semantic byte order; the corpus caught an initial reversed-tag
filter and passed after fixing the filter, without changing the census oracle.

The view loads only selected original base/normal texture slots. Other table
slots remain explicitly absent, so complete model texture residency is not
claimed. Material constants come from original records; recovered global
position scale/offset, indirect color and texture-offset defaults are applied.
Authored light/fog/visibility contributions, alpha materials, shadow passes,
other stage definitions and instance placement remain omitted. Original vertex
illumination, material color and normal-mapping equations still execute.

The camera starts at authored spawn `(-61.91, 0.65, -97.57)` plus a declared
1.6-unit inspection eye offset. Its horizontal heading toward the main geometry
bounds center is a development choice; original heading conventions and gameplay
camera are not claimed. Perspective is 60 degrees vertical, 16:9, near 0.1 and
far 1000. Native player-position binding `0x47c470` copies xyz without scaling;
independent main-world bounds are x[-260,306.495], y[-9,113.852], z[-179.981,400].
The camera uses the positive-view-z, zero-to-one-depth projection from
[GLM perspectiveLH_ZO, pinned revision 0af55cce](https://github.com/g-truc/glm/blob/0af55ccecd98d4e5a8d1fad7de25ba429d60e863/glm/ext/matrix_clip_space.inl),
inspected before implementation. It is expressed as the renderer's row-dot
contract; no GLM dependency or source file is vendored.

The original mission remains in its stage load wait and black fade. Framework
presentation explicitly displays the owned inspection view at full opacity
without changing that script/fade state, completing load or starting gameplay.
The original loading-scene animation remains a separate presentation gap.


Final implementation checks on October 23 passed: `scripts/check.py all`
covered formatting, strict build, six CTest entries, fatal analysis of 69 authored
compilation commands and the repository content guard. The original-root corpus
also passed with 104 selected / 1,124 omitted draws, retained optional input
checks and zero charged CPU bytes after teardown. A native interactive scene
acceptance run followed through the normal menu, with the user selecting Concept
Mission → One Shot Kill. The log records original `concept`, selected stage 1,
difficulty/costume 0, then the genuine stage-consumer block and 104 selected /
1,124 omitted main-world draws. The inspected screenshot visibly shows textured
buildings against black. This establishes a limited original-geometry/material
view on Metal; it does not establish ground, sky, full environment, visibility
or gameplay. The ignored records are
`research/evidence/reconstruction/stage/native-scene.log` and `native-world.png`.


## Continuation: original load now returns

The earlier blocked stage/character states above are historical. The completed
consumer work is documented in [mission load consumers](mission-load-consumers.md).
The unchanged original mission now returns from `load()` into
`awaiting_world_setup` after real resource construction. The existing original
root integration requires that phase, 54 dependency models, 11 shared character
support sets, 178 NPC palette bindings and the inactive clear/gameover callbacks.
Its complete teardown returns tracked CPU allocation and synthetic device owner
counts to zero. The static view now borrows stage and sky residency; it performs
no duplicate asset uploads and selects all ten ordinary stage models using the
recovered classification: 4,097 draws, three custom-pass omissions and one sky.
The earlier main/cave-only selection omitted required background/detail geometry;
CPU ray evidence and the original constructor closed that selection error.

This completes resource-loading acceptance for the selected mission, not actor
creation or gameplay. Original `_WaitMission` remains pending until a future
world/outcome producer exists. Loading-scene animation and authored scene
presentation remain explicit work. Current native observations are recorded in
[stage view completion](stage-view-completion.md); synthetic corpus output alone
does not certify the native image.
