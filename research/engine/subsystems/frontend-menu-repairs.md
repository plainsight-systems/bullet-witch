# Frontend prompt and costume presentation repairs

The October 26 menu reports exposed independent presentation defects. This work
preserves original assets and the accepted menu/action boundaries. No gameplay,
lighting, asset modification or new renderer is required.

## Recovered contracts and design

The root script dispatches native menu tasks. Their draw and update receivers,
not a Lua-authored widget tree, own these details. The read-only Steam executable
exports and native comparison captures are kept under ignored
`research/evidence/reconstruction/frontend-menu-repairs/`.

- `4e2cc0` draws costume row plates and overlays using a sprite depth of zero.
  Text initialized by `4ba1f0` receives base depth plus one; the aperture masks
  have depth two. Sprite admission `569620` preserves depth ordering. Our ordered
  frontend incorrectly put text before the opaque plates, hiding labels.
- Costume sprite 56 is genuinely pink. It is a confirmation flash, not a
  persistent cursor. Draw opacity comes from receiver `+0x1c4`; `4e36f0`
  initializes its type-two half-sine from zero to one over 0.4 seconds, disabled
  until confirmation. The completion flag gates the ensuing yes/no dialog.
  Sprite 54 is the ordinary pale selection, and sprite 451 marks the equipped
  costume. Their independent meanings must remain visible.
- Prompt receiver `472830` establishes the original anchor (840,636).
  `471c20` applies the table's positions, minus fourteen in X for the icon,
  then puts localized text six units after the resolved icon advance and
  thirteen down, at font size 22. Frontend copied the Options footer's *relative*
  offsets as absolute coordinates, leaving prompts at the top left.
- Main task `49b320` selects prompt set 0: navigation (`sys.0026`), confirm
  (`sys.0027`) and back (`sys.0028`). Costume, stage/concept and difficulty use
  set 1, confirm/back. Main navigation occupies the second line, forty units
  lower. Static table initialization `401ba0..401d22` resolves these original
  localization keys and entry counts; raw table zeros are not missing labels.
- `472b80`/`472c70` reveal/conceal the prompt receiver over 0.25 seconds, and
  child tasks admit it after entry and conceal it when accepting/leaving.
  Prompt presence is therefore legitimate; permanent placement above every
  page is not. Shared Options and dialog-owned prompts remain their own owners.

MenuFlow retains the supplied presentation clock and publishes bounded footer
opacity and costume confirmation opacity. The surface consumes these values,
uses the original task-specific footer layout, and explicitly orders costume
plates, labels and masks. Shared device prompt values gain the original fixed
navigation icon run; no device access or gameplay remapping reaches rendering.
No sorting framework, extra textures or per-frame allocation is introduced.

Design and implementation each retrieved Core I.1 (`ri-explicit`) and
performance MEM.9 (`guidelines/memory/MEM.9-embedded-allocate-at-init.md`).
They apply to explicit copied animation/icon observations and fixed prepared
text/sprite storage. Original trace resolves these contracts, so no substitute
engine algorithm is necessary.

## Reopened source and temporal audit

The first live review rejected the initial repair. Static captures had missed
independent text and timing defects; their earlier readiness conclusion is
superseded. The reopened audit traced the complete list receiver, text state,
parent/child handoff and native draw path rather than treating the blue marker
as an explanation for the reported scrolling problem.

- `4bb390(0x30)` sets horizontal/vertical centering flags. It does **not** set
  a 48-pixel font. `4ba1f0` defaults to 32 pixels; `4bb3a0` is the size setter.
  Costume labels now use the default 32 and center both axes on their authored
  row anchor. Stage headings retain their separate explicit size setters.
- `4ba1a0` translates the current text anchor additively. Costume shadow is
  offset by (4,4), then the inverse translation restores the foreground anchor.
  We previously subtracted from the original anchor, doubling the displacement.
  The source anchor remains (227,80); no item-specific correction is applied.
- Shared native dialogs had inherited the same font-size misinterpretation.
  Rich-text preparation `4644a0` explicitly assigns 32; `4ba440` measures line
  count with that size and zero default line gap. Choice labels also use the
  default 32. Shared panel geometry and all frontend/options/controller/pause/
  ranking consumers now use one 32-pixel contract. Geometry publishes first-line
  center and choice center; frontend/options translate those to their emitter's
  top-left coordinates. Other surfaces already perform that translation.
- `4e36f0` and `4c5210` accept navigation before evaluating response 572660.
  Our order advanced the previous selection before consuming the current input.
  Input, response evaluation and copied publication now follow source order.
- Stage/concept retain their parent scroll response while difficulty owns input.
  Previously entering difficulty reset that state, froze the old backdrop and
  returned to a snapped value with unrelated velocity. The retained parent
  response continues during child updates and cancellation restores its state.
  Temporal native capture also exposed the omitted parent obscuration layer:
  draw 4c42b0 uses authored gray (.35,.35,.35,.65), packed to 89/165 color/alpha
  bytes, with a type-zero response advancing at 1.4 units/second. It reaches 1
  while difficulty owns input and returns to 0 after that task finishes. The
  parent publishes this value; its surface draws the layer before its title.
  Child widgets are not arbitrarily tinted.
- `4c6610` clamps costume/stage/concept selection at the first/last real row;
  costume page movement is two rows and stage/concept is three. Stage draw
  `4c42b0` uses a remainder to recycle carrier positions while retaining real
  item identities. This is not selection wrap and remains source behavior.
  Costume draw uses continuous absolute row positions and authored aperture
  masks. Adjacent rows are intentionally partially visible.
- Equipped and browsing are separate. Blue 451 compares against persisted
  costume identity; pale 54 follows browsing selection. Original PC footage at
  [1:20 in the costume showcase](https://www.youtube.com/watch?v=6PljUusoSOQ&t=80s)
  shows Costume 4 selected with Costume 5 equipped, matching that code contract.
  It also shows the smaller centered labels. The footage observation does not
  excuse our former oversized/overlapping labels or incorrect temporal order.

The additional design/implementation consultation retrieved Core E.4
(`re-design-invariants`) and performance MEM.9 in both phases. Their application
is fixed state, explicit transitions and one owner of each retained response.
No new animation framework or invented scrolling algorithm is needed.

## Active device and accepted bindings

The user additionally rejected choosing controller prompts merely because a pad
is connected. Original 472270 selects an input-manager device mode;472930/472dd0
resolve menu mode 2 from fixed UI bindings and Help mode 0 from saved gameplay
bindings. Those contexts remain distinct. The existing reconstruction published
connection availability instead of active prompt-family and independently
hardcoded glyph IDs. It also advertised W/S while omitting the corresponding
fixed keyboard UI navigation aliases from action translation.

The game input owner publishes active prompt-family alongside copied actions.
Both action translation and glyph lookup read one immutable original-menu
binding descriptor. UI remapping is not invented: source fixed UI defaults and
its Enter/Escape/arrow aliases remain distinct from saved gameplay controls.
Help continues to resolve actual saved bindings, using the same active family.
Frontend, its child screens and mission/pause receive the copied family rather
than independently interpreting connection availability. Controller taps released
within one native pump now retain their explicit pressed edge, matching the
existing keyboard edge contract and original UI's separate sampled banks.

The user-requested activity-based family selection is an explicit adaptation.
Inspected authorized Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Plugins/Runtime/CommonUI/Source/CommonInput/Private/CommonInputPreprocessor.cpp`:
`HandleKeyDownEvent`, `HandleMouseMoveEvent`, mouse-button handlers,
`HandleMouseWheelOrGestureEvent`, and `RefreshCurrentInputMethod`. Adopt their
input-owner activity observation and copied current-family publication, not
Unreal's widget/filter/editor infrastructure. In our sampled input boundary,
explicit keyboard/mouse activity wins when both families occur in one pump;
idle connection and already-held controls do not steal family selection.
Disconnect returns to keyboard/mouse. No extra clock, allocation or device owner
is introduced. The existing original controller artwork remains its Xbox
compatibility set; additional controller-specific artwork needs its own admitted
family metadata rather than guessing a vendor from a positional button.

## Verification

The final production-Metal temporal witness completed 5,425 policy frames at
60Hz, with a 20fps native video and inspected frame sheets. It uses purchased
assets and actual MenuFlow/FrontendMenuSurface, not a fixture renderer. Routes:

- Every costume row in both directions, first/last boundaries, two-row page
  steps, held input through the real ActionRepeat owner, rapid reversals.
- Costume confirmation No and Yes, acknowledgment, declining a disk save,
  exit/reentry with the equipped identity preserved separately from focus.
- All six campaign stages and 18 concept rows, both directions/endpoints,
  three-row concept page steps, nested difficulty selection/cancel/reentry.
- Difficulty navigation and cancellation back to the main shell.

The independent witness executes original x86 functions 4c6610,585eb0,572430
and 572660 against the same inputs. All 2,280 list/repeat/page observations have
identical selections; largest displacement difference is 0.000489 canvas pixels.
All 914 nested-stage veil observations match the original response within
5e-11 opacity. These are sampled frames, not thousands of separate tests.
GPU execution completed with 107 peak textures and zero retained textures/device
resources after teardown. No game window or desktop input was driven.

The follow-up active-input witness feeds production `ControllerInput` samples
into `MenuFlow` and the same Metal surface. With a pad connected it captures
keyboard idle, pad navigation, W while a pad remains held, the following idle
sample, renewed pad input, mouse movement, costume navigation and a short pad
confirmation released within one pump. The copied family and accepted actions
match each step; inspected artwork changes with that family. Teardown retains
zero resources (107 peak textures). These are offscreen component captures;
physical controller/keyboard switching in the user's game remains part of the
final walkthrough. Saved-binding Help icon contracts also pass.

Generated source exports, comparison scripts/results, traces, video and captures
remain ignored under `research/evidence/reconstruction/frontend-menu-repairs/`.
`temporal-native.mp4`, `temporal-trace.csv`, `temporal-comparison.json` and the
route contact sheets preserve the temporal evidence. Original-footage comparison
is limited to the directly inspected visual contracts above; no original-PC
pixel equality or whole-engine equivalence is claimed.

Final `python3 scripts/check.py all --jobs 8` passes: pinned formatting,
architecture checks, build, all six CTest cases, all 243 fatal clang-tidy commands,
five policy tests and the working-tree content guard. The purchased archive
corpus passes, and the final original-content `startup-corpus` traversal passes
with zero migrated CPU memory live after teardown. Its controlled media and
synthetic renderer establish integration/resource contracts, not native visual
parity; the separate Metal witnesses supply the rendering evidence above.

Ignored logs: `prompt-full-gate.log`, `final-purchased-corpus.log`,
`prompt-startup-corpus.log`, and `active-prompt-native.log`. The earlier
`final-startup-corpus.log` was shared by two overlapping verification invocations
and is superseded by the clean final prompt traversal log; it is not relied upon.
No lint family or content rule was disabled. The user accepted the repaired menu behavior on October 26, 2026
and authorized its scoped commit.

The user's walkthrough should use Options → Costume: move through all six rows
both ways, hold a direction at the endpoints, reverse quickly, confirm a change
and return to the list. Labels should stay centered on their moving plates;
blue marks equipped and pale marks browsing. Pink occurs only during the short
confirmation. Exit/reenter, then navigate Stage/Concept → Difficulty → Back:
the same mission/carousel position remains and the parent veil clears. Verify
main-menu navigation prompts and child confirm/back prompts at bottom right.
With the controller still connected, use W/S or the mouse, then the controller:
the displayed bindings should follow the input being used. Space/left click
confirm; Shift/right click go back, with Enter/Escape retained as aliases.
The user completed review and accepted the menu repairs on October 26, 2026.
The accepted menu scope is committed separately from Alicia appearance research;
no push is authorized.
