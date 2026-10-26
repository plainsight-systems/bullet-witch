# Entire frontend verification

October 26, 2026. The user accepted the complete frontend following the native
walkthrough. Automated verification below was completed on October 25; acceptance
does not claim frame-for-frame screenshot parity or unsupported online/gameplay
services.

## Connected scope

Original root Lua retains orchestration. Native owners now implement title/main
feedback, purchased localized descriptions, difficulty, Stage/Concept carousel,
Continue confirmation/no-data acknowledgment, costume cards/change/save dialogs,
and frontend Options. Its original ten rows compose real controller settings,
brightness calibration, costume, Power Up and the unavailable online Ranking
route. Source load/create/overwrite/autosave dialogs replace the earlier authored
storage error UI. Details and source identities are in the linked frontend
subsystem reports. No game art/text or raw recovered implementation enters Git.

Independent review found and closed three connected defects: Power Up retained a
global black overlay on return, Costume save persisted parent Options edits, and
storage dialogs inherited Continue's artwork. The first now releases only its
own conceal; the second uses RecordSaveControl; the third explicitly presents
CharacterLoad's black task backdrop. Review covered ownership, input/lifetimes,
child return, storage failure and copied presentation; it did not launch the app.

## Evidence

- Native host rebuild and all six CTest suites pass. Tests follow the actual
  timed profile load/create/acknowledgment lifecycle rather than immediate load.
- The complete 233-command fatal clang-tidy run passed 225 commands initially.
  The remaining eight affected commands passed after their concrete fixes;
  final repair results are recorded separately so the initial failure remains
  visible. Final pinned formatting, five repository-policy cases and the
  proposed text-only content snapshot also pass.
- The purchased-content startup corpus passes original root/title/menu routing,
  all frontend Options children, costume 5, campaign and concept handoff, and
  ordinary mission/pause teardown. The added connected transaction changes BGM
  volume, saves Costume, verifies disk System is unchanged while Record changed,
  then discards Options and verifies both live and persisted System boundaries.
  Power Up return also asserts the global conceal is released. Prepared texture
  and migrated domain ownership retire to zero; peak domain payload was
  369,685,211 bytes. Rendering/audio observations here are controlled fixtures.
- Separate real FACT/FAudio disk-output verification exercises the exact
  twelve-cue frontend bank closure, including upgrade purchase cue 17, plus
  menu music intro-to-loop, mute/unmute, world pause and stop. Admission used
  9,692,580 bytes in 0.159 seconds. Captured PCM is finite and nonzero. This
  establishes native audio generation, not a user listening check.

Ignored logs live under `research/evidence/reconstruction/frontend-audio/`:
`full-menu-corpus.log`, `final-rebuild.log`, `final-ctest.log`,
`full-menu-check.log`, `final-tidy-repairs.json`, `final-format.log`,
`final-policy.log`, and `full-menu-output-analysis.json`. Initial failures are
preserved in their logs; later scoped checks supersede only the repaired units.

## Native review

Use `./run` from the repository root. Check title Start sound, animated main
selection/description and continuous music; enter and back out of difficulty,
Stage/Concept and Continue; select/save a costume; enter each Options child,
change and discard/save settings, then return to the same parent selection.
Power Up must return visibly to Options. Confirm loading a mission retires the
frontend music. The original source's admission animations intentionally gate
input while entering/closing; do not collapse them to instantaneous fixtures.

The user subsequently completed the native walkthrough and accepted this
frontend slice. This includes the reachable Options Power Up route; mission
result production and its post-result world lifecycle remain future work. The
automated verification itself did not launch a native window.

## Genuine external boundaries

Steam leaderboard networking and achievement import have no native provider.
Ranking uses the original unavailable-provider acknowledgment, not fake scores.
Achievement-dependent unlocks retain explicit supplied state. Continue consumes
validated existing checkpoint data; this slice does not invent a world
checkpoint producer or claim unsupported world restoration. Power Up is reachable
from Options; earning mission results and the post-result world lifecycle remain
with the still-unfinished gameplay work.
