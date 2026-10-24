# Animation review exits and simulation ownership

The native review exposed three separate boundaries: preview versus live aim
intent, pose representation admission, and complete simulation substeps.

## Preview and live aim intent

The first repair only removed a preview/live refresh conflict and proved eventual
weapon lowering. User retest rejected that behavior because a second RS click
must leave targeting, not wait for raised-posture expiry. The source-backed
correction now uses an independent targeting-mode owner and original camera
context; see [player targeting](player-targeting.md). The earlier twelve-second
expiry witness is retained as historical diagnosis, not toggle acceptance.


## Stop preview

With an active preview, Stop interrupts the exclusive presentation owner, clears the four motion banks
and their effects, restores the captured physical entry state, resets the pending
root interval, and returns body/weapon presentation to neutral. A production
PauseFlow sequence—open pause, choose Body clips, play, reopen pause, choose Stop
preview, then hold movement—resumes with both pause flags false and moves 4.94
world units in sixty float-host frames. Native snapshots show resumed locomotion.
The direct Stage path also selects ordinary movement after body, upper, additive,
facial, raised, damage, death, scripted-flight and spell previews.

Adversarial review additionally found that inactive Stop reset live player state
without an active preview. Inactive Stop is now a no-op apart from status;
explicit preview entry/exit alone owns neutralization. The original-resource
fixture checks preservation of targeting, posture, motion clock and velocity.

The reported Stop failure has not been reproduced as a separate reset defect.
No additional unconditional reset was added to hide it. The independent model
phase corrections below address real timing defects that could leave action
completion/cancellation observations stale; the user-reported path still needs
confirmation in the corrected build. The direct repeated walk probe eventually
meets a wall, so its reduced displacement is not interpreted as lost ownership.

## Complete simulation intervals

The prior double `1/60` split created a tiny remainder for a float-host `1/60`
frame. Parent movement could consume a retained root interval using that tiny
consumer duration, and model events/pose finalization ran only after all parent
substeps. The next parent therefore saw stale cancellation and controller state.
Balanced float-domain subdivision now completes each parent/model interval,
including pose, secondary motion, events and ring retirement. It publishes GPU
buffers once per outer frame. Authored velocity uses the retained root interval's
producer duration. See [source clock order](actor-model-clock-order.md) for the
original producer/consumer proof and pinned established subdivision reference.

Actual-content equivalence compares float 60/30 Hz, jitter and 50/100 ms batching
against separately completed steps: physical state, body clocks, ordered audio
cue identities and random consumption match. An evasive landing cancellation
crossed within a 50 ms outer frame is observed by the following parent substep.

## Verification boundaries

Ignored `interactive-animation/lifecycle-current.log` retains the raised-intent
failure before correction. `lifecycle-fixed.log` repeats the native sequence with
mixed float 60/30 Hz, jitter and 50/100 ms frames; ordinary and preview-created aim
both release, and the real menu Stop path resumes movement. Original-resource
corpus assertions verify incompatible bank requests preserve the active preview,
aim intent takeover, complete interval equivalence and balanced teardown.
These checks do not establish original Windows visual parity or identify the
user's unnamed problematic clip conclusively.


The preceding expiry-only correction passed `scripts/check.py all --jobs 6` (all native
unit targets, fatal clang-tidy, formatter and repository content policy), the
original-root startup corpus, and the native lifecycle replay. Native inspected
frames show ordinary locomotion after menu Stop and lowered weapon after releasing
preview-created aim. Those observations did not verify second-click targeting exit;
the later user rejection supersedes that acceptance. The current native
GameController/Stage context witness is recorded in [player targeting](player-targeting.md).
No game window was launched, restarted or controlled.
