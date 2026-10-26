# Leaderboards provider boundary

The original PC includes the Leaderboards row. `4c8ad0` owns its native task,
`4c7940` loads `sprite.ranking`, and `4c7a90` draws it. The provider is genuine
Steam integration: `4c29e0`/`4c2470` call `SteamUserStats011`, resolve `BID%02d`
and download entries. The current native host has no Steam provider. That fact
is supplied explicitly at admission; no fake scores, online success or simulated
connection is produced.

The unavailable path uses the original network failure text `net.0015`, shared
acknowledgment dialog and source quarter-second conceal. Source failure states
4000/4001 lead to 8000; source entry is 1.8 seconds, with quarter-second reveal
and logical cue4 delayed 0.4 seconds. Five sprite descriptors at `960878`
use original ranking images 268/270/275/276/269, original positions and quintic
interpolation. Availability and rendering remain separate; an actual Steam
provider and successful score pagination remain an explicit online boundary.

Core I.1 and performance MEM.9 were read for design: copied availability/time,
a borrowed output capability and prepared bounded assets preserve ownership.

Implementation consulted the same I.1/MEM.9 rules. Focused contracts pass for
entry admission, one-shot cues, acknowledgment-only exit, conceal ownership,
repeat admission and invalid time rejection. Purchased-content preparation and
submission passes for all five shell tracks and the failure dialog with both
keyboard and Xbox prompts: 11 textures at peak, zero textures/device resources
after retirement. No window or capture was created. Scoped fatal clang-tidy
passes the flow, surface and tests. The integrated runtime target and full runtime suite pass; final connected
verification is recorded in frontend-verification.md. Successful online ranking navigation is not claimed by this boundary.
