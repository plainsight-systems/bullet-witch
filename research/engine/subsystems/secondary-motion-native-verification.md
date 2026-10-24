# Secondary motion: native verification

October 23, 2026. This checks the integration described in
[costume secondary motion](costume-secondary-motion.md), using actual resources
and the production CPU solver, pose owner and Metal renderer. No gameplay,
facing, shader or collision policy was changed by these diagnostics.

## Space and update contract

The solver consumes clean animated world bones and inverse-bind skin matrices,
including the original model Y half-turn and its current translation. Weighted
control points use skin matrices; the six animated body capsules use unskinned
bone matrices. Reset uses the costume bind pose at that same model transform.
The original ground bound is model-world translation Y plus 0.03.

The original `0x4f6530` runs ordinary model evaluation before `0x4fca10`, then
`0x4ff490` converts each simulated world frame through its current parent's inverse
and writes a local secondary matrix. The reconstruction's absolute-world override
boundary is equivalent in ownership: it installs those world targets, preserves
untouched descendant local relationships and rebuilds skin matrices. It must not
multiply a returned override by model world a second time. The current clean body
pose must be supplied each update; the previous corrected outfit is not an anchor
input. Weapon attachment and palette publication follow this correction.

The adapter advances once per consumed positive game delta. Zero-time publication
retains the last result and first-update flag. Pausing must not integrate secondary
motion; teleport/costume replacement must rebuild/reset at the new actor transform.
The latter live lifecycle behaviors remain owned by the actor/outfit integration,
not by this probe.

## Actual-asset CPU witnesses

`secondary_pose_witness_probe.cpp` runs all six costumes against stand, walk, dash
and the twelve directional evade phase clips. Each of these fifteen clips runs
120 updates at 60 Hz in two translated model orientations. One includes the
original half-turn plus initial authored yaw; the other rotates it by another
75 degrees. This gives 180 costume/clip/orientation cases and 720 recorded frames.

All cases completed with forty declared output bones, finite final skin matrices,
unchanged body bones 0–84, zero steady-state memory-domain allocations and zero
live domain bytes after teardown. Zero-time calls preserved published overrides.
The diagnostic also records 111 node positions and all six capsule frames for
representative forward-evasion poses, retaining their actual world coordinates.

Masked node/capsule signed distances are observations, not pass criteria. They
include pinned anchors, and the original solver projects the first matching
capsule before subsequent cone/segment-length correction. Negative distances can
therefore remain. Neither nonnegative node clearance nor finite matrices would
prove that interpolated outfit triangles never intersect the body.

## Native mesh witnesses

`secondary_outfit_probe.mm` uses the ordinary stage loader, actor/controller,
secondary integration and renderer with original st04_c resources. For each
costume it records idle, walk, dash and four directional evades at frames 14, 29,
60 and 119 after a 45-frame settling period: 168 PNGs total. All six runs completed.
Walk uses analog magnitude 0.35; dash and evade approach use full magnitude.
No substitute shader, outfit visibility mask or physics approximation is added.

Visually inspected walk frame 60 for all six costumes and PW5 idle, forward and
lateral action frames. The PW5 lower panels now deform outward and trail behind
the hips; the previous frame-matched forward-evasion capture retained a rigid,
narrow bind-pose panel. No collapsed or exploding secondary geometry appeared in
these inspected views. Some lateral frames near the entry wall crop the character,
so those frames cannot establish complete outfit clearance. These observations
support restored secondary motion, not universal intersection-free cloth or
original-game visual parity.

A comparison against the previously captured `evade-facing-current.log` finds
1,020 overlapping body-bone translation records unchanged exactly: four evasions,
three matching frames and 85 body bones. This isolates the outfit correction from
the separately unresolved forward/back visual-facing question. Corrected scenario
indices 3–6 match previous direction indices 0–3.

An attempted fresh baseline executable exited 139 before capturing frames while
headers and linked archives crossed the concurrent integration boundary. It is
excluded from evidence. The successful corrected probe was relinked after the
actor lead declared the complete libraries coherent. Existing preintegration
captures supply the comparison; no successful fresh baseline run is claimed.

## Reproduction and retained evidence

All generated/proprietary evidence remains ignored under
`research/evidence/reconstruction/actor/`:

- `secondary_pose_witness_probe.cpp`, `secondary-pose-witness-command.json` and
  `secondary-pose-witness.log` reproduce the CPU integration checks.
- `secondary_outfit_probe.mm`, `build_secondary_outfit_probe.py` and
  `secondary-outfit-build-commands.json` build against the current application
  link manifest; `secondary-outfit-shader-library.json` records the packaged
  production shader hash.
- `secondary-native-after/pw0` through `pw5` contain the logs and captures.
  Probe arguments are game directory, compiled Metal library and output prefix;
  `BW_COSTUME_PROBE=0..5` selects the costume. `BW_SECONDARY_SIDE=1` provides an
  optional alternate camera settling direction, not used in the recorded 168.
- `secondary-witness-summary.json` records the case counts, matrix observations
  and matched preintegration body comparison. The comparison source remains
  `research/evidence/reconstruction/traversal/evade-facing-current.log`.

These diagnostics do not replace the required repository checks, real controller
review, in-session costume replacement or original-game comparison. Those are
reported by their owning implementation tasks.

## Original-release clipping reports and current review

On October 23, 2026, the user confirmed the pause menu works, noted its bounded
Resume/Costume choices, and still observed dresses clipping. Secondary movement
and successful resource publication therefore remain distinct from garment
parity. No intersection-free or original-equivalent outfit acceptance is claimed.
The baseline menu scope and deliberate in-mission costume improvement are
recorded in [pause and costume switching](pause-and-costume-switching.md).

The following pages were opened and verified on October 23, 2026:

| Source | Direct observation | Evidence boundary |
|---|---|---|
| [WorthPlaying Xbox 360 review, March 14, 2007](https://www.worthplaying.com/article/2007/3/14/reviews/40502-xbox-360-review-bullet-witch/) — Erik “NekoIncardine” Ottosen | Specifically describes Alicia's dress passing through her leg when moving. | Strong firsthand evidence that dress/leg clipping existed in the released Xbox 360 game. It does not identify the tested motion, frame, costume index or quantitative severity. |
| [WorthPlaying Xbox 360 preview, February 22, 2007](https://worthplaying.com/article/2007/2/22/previews/39973-xbox-360-preview-bullet-witch/) — current page byline Alicia | Reports clipping in the default costume in a complete pre-release build. | Same outlet and earlier build; not counted as independent corroboration. The displayed byline differs from the retail review, so common authorship is not established. |
| [Save or Quit PC review, May 21, 2018](https://saveorquit.com/2018/05/21/review-bullet-witch/) — Genkipro | Reports general clipping, frequent shadow flicker, main-menu-only costume changes, and facial animation problems with some costumes. | Relevant firsthand PC-port context, but its clipping description does not specifically identify dress/leg intersections. The costume-menu observation also agrees with the recovered native menu exclusion. |

These reports establish an original-game defect category, not that the
reconstruction matches its frequency, severity, affected costumes or motion
phases. In particular, the Xbox description cannot certify the Steam port's
precise garment behavior. A matched original-PC capture with the same costume,
action phase and camera would be needed to classify the remaining visual
intersection as equivalent or regressed. Do not retune body capsules, constraint
lengths, damping or timestep from review prose alone. Preserve the recovered
solver while recording the current user-visible clipping as unresolved parity.

The subsequent [bounded collision-improvement assessment](secondary-collision-improvement.md)
compares published PBD/XPBD methods, actual-content endpoint and skinned-vertex
proxies, and inspected original PC footage. Its extra-projection candidate was
rejected because mesh-contact proxies did not improve consistently.
