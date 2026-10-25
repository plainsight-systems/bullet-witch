# Ammunition cockpit presentation

October 25, 2026. This is original native HUD animation, separate from live
ammunition ownership and from gameplay particles. It consumes selected weapon,
current count, the resource owner's accepted-recharge receipt and an explicit
enchanted-spec observation. It cannot debit ammunition or accept gameplay input.

## Native owners and update order

The main owner retains two children. Weapon change `0x4aabb0` exchanges their
roles, requests departure on the old child, and initializes the new child with
`0x4a9b30`. Each child owns three banks of twenty cartridge entries, a row
selector and independent arrival/departure/enchanted-indicator state. Drawing
`0x4a9870` publishes the outgoing child before the selected one.

The original row tables at `0x95da68` contain:

| Weapon | Cartridge image | Row size | Start x/y | Spacing | Ejection policy |
| --- | --- | --- | --- | --- | --- |
| Machine gun | 429 | 20 | 1135 / 114 | 9 | ordinary |
| Shotgun | 430 | 8 | 1131 / 110 | 18 | shell |
| Cannon | 427 | 12 | 1133 / 112 | 16 | ordinary |
| Gatling | 428 | 20 | 1123 / 115 | 9 | fast |

Visible cartridges are count modulo row size, with a positive exact multiple
showing a complete row. The final cartridge sits five pixels lower. A count
above the row size draws a tinted reserve row at offset `(7,-5)`.
Count setter `0x4aa910` ejects removed cartridges. Increasing the visible row,
or an explicit accepted recharge even with unchanged count, ejects the previous
row and advances the three-bank ring. This is why deriving recharge solely from
the displayed count loses source behavior.

Child phase update `0x4aa660` retains arrival, departure request, departure and
retirement as distinct transitions. Presentation update does not collapse the
phase boundary when a supplied interval crosses a duration. Arrival moves the
row over 0.15 seconds using the original `0x571c90` easing; departure moves it
left over 0.25 seconds. Digits are opaque during arrival and disappear once the
departure cursor becomes positive, independently of cartridge-row fading.

## Cartridge ejection

`0x4aa250` initializes ejection once per cartridge, taking three draws from the
shared original random stream: horizontal/vertical direction jitter and angular
speed. The direction is normalized through shared math. `0x5df900` uses the
same LCG as bounded integer draws, performs positive signed-range modulo,
converts to float and multiplies by `2^-31`. The reconstruction exposes this as
`SourceRandom::unit()`, borrowing the existing session stream rather than
introducing a separate HUD seed.

`0x4aa470` first enters flight, then updates movement on a later invocation.
Movement uses the original 60-based interval, retained speed, gravity 52 and
per-policy fade windows. Source speed damping caches `pow(0.9800000190734863,
first_flying_interval * 60)` on the first flying update and reuses that value.
The reconstruction preserves this unusual source behavior; it does not silently
replace it with a newly evaluated exponential each frame. Retirement also has
a separate phase before the active bit clears. Cartridge rotation pivots around
its own top-left position, not the sprite center.

The three ejection policies, from `0x95da08`, are:

| Policy | Fade start/end | x bias / jitter | y jitter | Speed | Angular base / jitter |
| --- | --- | --- | --- | --- | --- |
| ordinary | 0.2 / 0.7 | 0 / 0.8 | 0.6 | 15 | -4.5 / 4 |
| shell | 0.4 / 0.7 | 0 / 0.5 | 0.4 | 15 | -8 / 4.8 |
| fast | 0 / 0.1 | 0.1 / 0.1 | 0.1 | 17 | -8.5 / 4 |

Jitter follows the original `unit * range - range` expression, not a newly
chosen symmetric distribution.

## Numeric and enchanted presentation

The original count atlas is image 425; 426 supplies the flash and 434 the
centered expanding burst. `0x4aa180` derives enchanted indication from the live
weapon's active enchanted specification. Selecting a spell is insufficient.
The reconstruction accepts this observation explicitly; callers must keep it
false while that actual gameplay producer is absent.

`0x4a98a0` / `0x4a9980` own the 1.5-second sine flash, tint response and separate
one-shot burst. Shared `math::damped_response` supplies the original mode-two
response; shared `math::eased_tangent_interpolate` supplies `0x571c90`'s quintic
parameter remapping and endpoint-shape response. The generic math routines do
not choose HUD timings or own a clock.

`AmmunitionHud` owns fixed two-child state and a 192-command publication buffer.
Each child can contribute at most sixty active cartridges, twenty reserve
cartridges, six digit sprites (three digits plus flash) and one burst: 174
commands across both children, below that capacity. Command spans remain valid
only until the next advance/reset or owner destruction. The main HUD owns the
helper and the surface resolves source image IDs. No per-frame allocation or
resource acquisition occurs in this helper.

## Evidence and validation

Source exports are the existing ignored `pass-230-gameplay-hud`,
`pass-231-gameplay-hud-updates`, `pass-232-gameplay-hud-state` and
`pass-mission-load-first` batches. Direct executable reads verify the numeric
tables. Literal-x86 witness `research/evidence/reconstruction/gameplay-hud/
ammunition_witness.py` executes initialization, count reconciliation, update
and draw. Intercepts supply renderer/texture handles, capture draw publication,
route the RNG getter to synthetic session storage and supply standard host
sqrt/pow/sin calls. The random generator and HUD algorithms themselves execute
original instructions; this is not full-game execution.

Recorded states cover arrival, settled display, shot phase entry, subsequent
flight, departure, and two enchanted-indicator updates. With seed one, the first
machine-gun cartridge ejection consumes three random draws and reaches
`(966.026367,105.004166)` with rotation `-0.106589325` on its first moving update.
Three focused C++ contract cases verify these native observations, explicit
same-count recharge, outgoing-child lifetime, enchanted burst and rejection
without changing commands or random state. Strict warning compilation and the
isolated test binary pass. A separate command comparison covers all seven
recorded states: image IDs, final position, color, rotation and scale agree
within 0.0003 for geometry/rotation/scale and one original 8-bit color step.
The comparison records zero mismatches in ignored
`ammunition_comparison.json`. Both authored translation units pass pinned
clang-tidy with fatal diagnostics. Full slice integration remains the parent
gate.

Design consultation queried both C++ corpora and retrieved Core R.4
(`rr-ref`) and performance MEM.9 (`guidelines/memory/
MEM.9-embedded-allocate-at-init.md`). Implementation separately queried both
corpora and read Core E.4 (`re-design-invariants`); the fixed-storage MEM.9
guidance applies unchanged. These rules establish contracts, while native
witnesses provide the behavioral evidence.
