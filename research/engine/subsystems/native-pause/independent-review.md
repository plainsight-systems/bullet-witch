# Independent native pause review

Reviewed the uncommitted pause slice against accepted HUD base `b983c2d`, with
original recovered behavior as the reference. Standalone power-up changes are
excluded. This is a source and ownership review, not user acceptance or a claim
of complete gameplay reconstruction.

## Findings and closure

All demonstrated findings are closed in the frozen source recheck:

- Developer review costume observation could remain stale after a normal
  Options replacement. Successful publication now refreshes the validated
  observation before catalog binding; the public transaction regression covers
  an externally observed costume followed by selection of the prior costume.
- Options conflated Confirm and Start. Platform input now preserves separate
  bits; Start takes the existing save/exit route from any value row. Options
  retains source priority: navigation, horizontal adjustment, Reset, Confirm,
  Start, Back. Tests cover simultaneous Confirm/Start, navigation/Start and
  manual/automatic save admission.
- Reset incorrectly enabled Autosave. It now leaves that row and its sound
  output unchanged, covered by a public-flow regression.
- Prompt spacing treated a scale-pivot increment as translation. Following the
  original rectangle setter, pivot setter and transform gives effective pitch
  `128/3`, with the existing text advance leaving clearance. The shared resolver,
  geometry tests and source reports are corrected. Independent inspection of
  the corrected offscreen controller keyboard footer confirms separated icons
  and labels. The witness is the ignored
  `research/evidence/reconstruction/native-pause/controller-keyboard-prompts.png`,
  using the shared resolver. Older Options images under
  `research/evidence/native-pause/dialog-verification/` predate this correction
  and are not replacement evidence. The earlier overlap was a reconstruction
  error.
- A follow-up fixed-input review confirmed the original wheel navigation masks
  and found the missing Help scroll consumer. The original fixed receiver adds
  wheel impulses to positional stick Y; Help reads that observation and applies
  its 16-pixel conversion. The repaired adapter publishes a separate menu axis,
  preserving wheel contributions of +2/-2 and the unclamped stick sum. Composition
  forwards it to Help without using remapped gameplay movement. Source/code
  recheck closes this finding. Contracts cover both wheel directions, existing
  weapon cycling, combined magnitude, keyboard-movement independence and release.
  The coordinator corpus checks that gameplay movement cannot scroll Help and
  that the menu axis scrolls when the original topic has a nonzero scroll range.

The earlier missing menu-audio boundary was also addressed by the shared cue
facade, explicit presentation pause domain and existing mission audio owner.
This closes the missing implementation finding; it does not claim listening
acceptance.

## Ownership conclusions

No further demonstrated ownership or false-success defect was found in the
reviewed paths. Settings and Help use bounded Profile/DeviceOptions capabilities;
System save does not flush unrelated Record data. Partial-save rollback preserves
the need to persist the restored values. Presentation timing is supplied by the
existing coordinator. Menu flows do not own a clock or pump the audio device.
FACT gain publication uses its native lock; pause domain and gain role remain
independent. Composition retains one audio service call per host frame.

Transition admission precedes teardown. Gameplay borrowers, pause/HUD surfaces,
stage resources, audio and mission authorities retire before session completion.
Retirement releases root wait with status 2; restart preserves the wait and uses
fresh composition. No actor-only reset substitutes for world restoration.
Both C++ guideline corpora were consulted, including explicit authority,
invariants, RAII, data-race prevention and bounded steady-state storage. No new
warning suppression was found in the reviewed implementation.

## Verification limits

The reviewer inspected source, regression assertions and native offscreen image
evidence; the parent owns the final repository gate and its result. No visible
game launch, controller walkthrough or listening test was performed for this
review. The reported controller probe covers 33 rows and 242 animation frames,
an 80-texture peak and zero retention; these are probe results, not live gameplay
acceptance.

Full scene restart is source-reviewed and session-tested. The current integrated
startup corpus exercises retirement, not a complete teardown, reconstruction and
second pause cycle. Nonzero entity checkpoint restoration, subtitle production
and vibration effects remain explicit future consumers. The debug interface
remains pending user design; no speculative shortcut is accepted by this review.
