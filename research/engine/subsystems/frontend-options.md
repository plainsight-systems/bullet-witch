# Frontend Options owner

The PC uses the same native Options task as mission pause: `4d4c10`. Its
immutable mission variant bit, set by `4d6560`, changes resource admission,
rows and child routes. Frontend loads `sprite.optiontit` in addition to
`sprite.option` (`4d3280`). It navigates ten rows without skipping entries:
subtitles, BGM, SFX/voice, controls, brightness, autosave, Leaderboards,
Power Up, Change Costume, Exit. The PC eligibility function `470e50` returns
one unconditionally, so Power Up and Costume are reachable. Ranking is a real
native task (`4c8ad0`), not an omitted Steam row. Its service belongs to the
frontend composition; Options must not fabricate ranking success.

The shared flow keeps settings and device controls, entry rollback, original
save dialogs and nested controller/calibration behavior. An immutable
`OptionsContext` chooses source rows and art. Frontend-only children publish a
typed stable `OptionsChild` after the source quarter-second conceal; completion
starts quarter-second reveal. Their gameplay/profile/network authority stays in
the actual child owner. The mission costume addition retains its accepted
separate behavior and does not gain frontend persistence authority.

Surface admission selects the original context's nine descriptor tracks. The
frontend detail panel is image 216, with the longer ten-row layout; mission uses
image 225. The final two frontend tracks last 1.4 seconds and begin immediately,
while the mission tracks begin 0.2 and end1.2. Shared rows retain original source
positions. Purchased localization identifies all labels/descriptions.

Core I.1 and performance MEM.9 were read for header design: explicit copied
context/requests preserve authority and fixed bounded row storage. Source
compatibility constants stay in shared Options, not the application host.

The final two frontend descriptors also select mode 1 in `572120`: quintic
remapping followed by the original tangent response with default unit shape.
This uses the shared math interpolation; it is not approximated by a plain
longer fade. Other tracks retain their source linear/quintic modes.

I.1 and MEM.9 were also read for implementation. The focused pause contracts
pass with all three frontend child requests, conceal/reveal input gating,
stable request ownership, clean Exit and rejected duplicate completion.
The runtime target builds. The complete runtime run currently stops earlier at
the frontend mission-departure assertion while that parallel lane is being
updated; this is not a claim that the complete suite passed.

Scoped fatal clang-tidy passes Options flow, surface and pause contracts. An
ignored resource-only probe loads purchased archives through the production
surface, submits all ten rows, calibration and native save/discard dialogs,
then retires everything. Peak residency is 29 textures, final residency zero;
external-child presentation clears the parent's draw list. No window, capture
or visible game was launched. Frontend composition and native walkthrough
remain the parent integration's responsibility.

## Power Up persistence on Options exit

Returning from Power Up does not immediately save: `4d4c10` state1002 retires
its child and returns through the quarter-second reveal. A successful purchase
calls `4d6580`, setting the Options Record-change flag. Exit checks that flag
alongside settings changes. Source states8018–8021 write System, then Record
when needed, before the existing manual/autosave acknowledgment and save cue.
The narrow optional `RecordSaveControl` adds only that persistence authority.
The coordinator compares copied progression before/after the actual Power Up
child and reports a change; Options cannot invent a purchase or replace Record.
System/device failure prevents Record publication, Record failure retains retry,
and success is acknowledged only after all required writes succeed.

Back's discard prompt also sees Power Up changes. Original `4bb600` restores
System only; purchases stay in memory. The implementation preserves this source
behavior, clears the local Options change flag on confirmed discard and leaves
the authoritative Record dirty for any later legitimate save. It does not
pretend to roll progression back. Focused tests cover actual purchase, failed
Record write after System success, retry, reload and discard without a write.
