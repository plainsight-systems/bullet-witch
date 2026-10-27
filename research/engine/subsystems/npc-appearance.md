# NPC draw selection and palette publication

The placed actor owns appearance. Immutable resident model geometry and texture
assets stay shared; renderer admission copies instance texture-table changes.
The original source is sufficient here; no new palette or visibility policy is
substituted.

## Original consumers

The hash-bound unpacked Steam executable is the evidence baseline recorded by
`engine_image.py` and the mission-startup trace. The placement constructor
`43b980` initializes four actor draw-mask words to `{05000083,0,0,0}`.
`47ef40` interprets column 15 for supported actor classes: a present string resets
those words to `{1,0,0,0}` before setting its hyphen-separated indexes. An absent
string keeps the constructor values. Empty tokens are skipped; the word index
clamps to the last word and the bit index uses its low five bits. The parser
retains presence separately so present-empty and missing remain distinguishable.

`44af80` classifies player, human/recruit and enemy families. `47ecf0` excludes
vehicle/special classes from ordinary draw customization, except `eb0`.
`4806e0` installs the four actor words through `42bd40`. The model constructor
`420ee0 → 42a8f0` initializes **eight** words to all ones (`42a9b4–42a9ee`),
so the upper four words remain enabled. Unsupported numeric tokens throw rather
than reproducing `atoi` overflow/invalid-input behavior. Group IDs beyond the
native 256-bit mask are rejected.

Human/recruit column 25 selects registry entries in ascending bit order.
`440910` skips missing entries and entries whose source texture name equals
its base material name. `5e19c0` queues up to 16 changes. `5e3570` consumes that
queue using `4263e0`, which visits model texture sets and calls `41e0b0`.
That last consumer compares the supplied material name as a **case-sensitive
prefix** of each runtime texture-table name. On a match it replaces every table
slot aliasing the current texture handle. Later entries see earlier changes.
This is not a comparison against model material labels or informational TLST.

`MissionLoading::npc_texture_slots` returns the actual `table._tm` runtime
ordering, names and already-resident handles from the retained stage model.
`PreparedNpcAppearance` applies that algorithm once and emits changed slots.
`WorldColorDraw::texture_overrides` consequently affects BTX0, AMOC, normal,
secondary and environment semantics whenever they reference changed slots.
The native renderer validates and copies a resolved table at admission; color
and shadow passes consume those private bindings. Texture retirement removes
submissions using the retired handles. Shared model bindings are never patched.

## Weapon defaults

`603900` selects a weapon motion slot; `615900` constructs the weapon model
through `5dfc60 → 5dfe60 → 56a030 → 420ee0`. Its setup selects motion using
`5e4760`, without replacing the model's initialized group masks.
`default_weapon` therefore selects all valid authored groups. The actor still
owns whether this attached weapon is visible and its pose; body column 15 is
never applied to weapon draws. Human attached-gun admission is a separate
source-owned decision.

## Ownership and guidance

Both C++ MCP phases queried Core and performance corpora and read I.1 and MEM.9.
Inputs and ownership are explicit; setup copies only small appearance metadata,
never geometry or image payloads. No frame-time palette search or allocation is
introduced. Native admission retains a resolved handle table per submitted draw
for constant-time slot resolution. This keeps all semantic aliases coherent
without extra per-pixel or per-vertex work.

Validation is recorded with the focused test results under the ignored
`research/evidence/reconstruction/mission-startup/render-instances/` directory.
Full application integration and the full gate belong to the mission-startup
owner; no visual validation is claimed by this helper alone.

Focused verification completed: native memory tests and runtime tests pass,
including private palette bindings, shared BTX0/AMOC slot aliasing, ordered
prefix replacement, constructor defaults, null/empty masks and weapon defaults.
The original `41e0b0` instructions were executed with a synthetic four-slot
table: `[10,10,11,12] → [20,20,11,12] → [99,99,11,12]`; the differently
cased name remained unchanged. The witness records the executable hash.

## Complete initial NPC material admission

The original-content census covers all draws (a superset of the selected masks)
from the nine resident body variants and four original weapon owners used by
this mission. Ten exact effective selectors were outside the existing audited
set. Their original registry routes are:

| Selector | Variant | Color vertex/pixel | Shadow vertex/pixel |
| --- | --- | --- | --- |
| `82800210` | `2` | 477 / 418 | 8 / 0 |
| `80802610` | `1` | 475 / 414 | 8 / 0 |
| `80822610` | `1` | 475 / 120 | 8 / 0 |
| `82800290` | `1` | 476 / 248 | 8 / 0 |
| `80800610` | `1` | 475 / 412 | 8 / 0 |
| `80802690` | `1` | 476 / 47 | 8 / 0 |
| `c0802290` | `1` | 482 / 45 | 10 / 0 |
| `c0822290` | `1` | 482 / 111 | 10 / 0 |
| `00800680` | `1` | 452 / 21 | 2 / 0 |
| `c2800290` | `1` | 482 / 248 | 10 / 0 |

The hash-bound executable's registry, constant tables, declared inputs and
instructions were the oracle. Executing those color programs on 30 independent
synthetic inputs, with both shadow visibility states, agrees with the existing
shared equations within `2.39e-7` for pixels and `1.20e-7` for vertices. The
secondary layer selects the original variant-2 alpha interpolation. One-, two-
and zero-influence transform paths retain the original accumulation order.
Shadow stages already belong to the admitted source program set.

These findings justify admitting precisely those ten keys, including their
samplers and attribute dependencies. They do not justify admitting arbitrary
neighboring flags or approximating an unknown program. Independent semantic
signatures extend the in-process contract test. The ignored witness bundle is
under `research/evidence/reconstruction/mission-startup/render-instances/`:
`material-source-routes.json`, `material-equation-audit.json`, original stage
dumps and the diagnostic execution script. Synthetic equation agreement does
not establish original runtime pixels, filtering or GPU rounding parity.

WG2's zero-influence surface is a rigid transform consumer even though its
resident model contains skeleton data. Requiring a skin palette merely because
a model has a skeleton is incorrect. It retains the completed weapon attachment
as an explicit instance model transform; no synthetic joint or alternative
shader is introduced. See [animated instance publication](animated-instance-publication.md).

A separate offscreen Metal probe also decoded all thirteen resident body/weapon
models and executed all 667 draw surfaces, including groups not selected by
current NPC masks. It uses neutral one-pixel textures and identity palettes to
validate material/layout admission and completed GPU execution; it is not a
claim about final gameplay appearance. All ten new source selectors also run
through the native in-process fixture, including the rigid weapon transform,
camera-update retention and rejected-update rollback contracts. The real
population replay separately exercises authored trigger admission, selected
draws, immutable residency, per-instance palettes, pause and teardown.

The focused real-content replay completed all 58 authored placement groups.
It uses original trigger centers as explicit test player positions and checks
all candidates for each group, including source ground projection and exact
checkpoint group identity. Seven candidates remain outside their authored
camera-distance admission threshold at those trigger centers. They are checked
as deferred records, not forcibly made resident. Private palettes, appearance
texture tables, light/shadow packets and pause behavior are checked on admitted
actors; every case checks draw retirement and unchanged model/texture residency.
The declaration for archetype110 has no authored group and is prepared without
inventing a spawn location. This covers all nine referenced body families in
placement data, but does not claim a live factory run for distance-gated actors.
The 667-surface native probe separately covers every resident model family.
