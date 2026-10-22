# Mac research route and Steam wrapper — October 21, 2026

**Update:** step 1/2 below has now succeeded. See the
[offline unpacking result](2026-10-21-offline-unpacking.md). The following records
the proposal before execution; runtime and Steam API questions remain open.

The research host reports arm64 and macOS 26.6.2. The next step does not have to
be running the game: offline unpacking may recover readable native code first.
This corrects the runtime-first recommendation in the startup report.

## Wrapper identification

A read-only comparison against the upstream Steamless variant 3.1 x86
[detector](https://github.com/atom0s/Steamless/blob/master/Steamless.Unpacker.Variant31.x86/Main.cs)
found the common signature at `.bind+0x310`, the pattern labelled 3.1.2 at
`.bind+0x3a0`, and its expected header size `0xf0`. The earlier version patterns
did not match. These results satisfy the inspected detector's signature/header
checks and strongly identify the SteamStub family; no unpacking has been tested.
See [the recorded comparison](../evidence/startup/steamstub-signature-check.json),
which includes the executable hash and source URL.

[Steamless](https://github.com/atom0s/Steamless) documents offline removal of the
SteamStub wrapper. It explicitly does not remove Steamworks API integration.
Our executable imports `steam_api.dll`, so removing the wrapper would not by
itself establish that the game can run without a Steam client. Valve describes
the wrapper's ownership/client-launch role in its
[DRM documentation](https://partner.steamgames.com/doc/features/drm).

## Proposed order

1. Work on a copied executable under `research/`, inspect the relevant upstream
   unpacking implementation and determine a practical Mac-hosted tooling route.
   Steamless's available Windows application is not assumed to run natively on
   macOS. Its file-processing logic can be assessed without running the game.
2. Attempt offline unpacking, retaining hashes, logs and the original. Validate
   PE structure, restored entry point, native instruction plausibility and the
   already located Lua handler addresses. Successful unpacking alone would not
   prove the resulting executable runs.
3. Continue the startup native-call analysis and identify actual Steam API use.
   This can clarify whether client startup is required and where it occurs.
4. When runtime evidence is needed, test CrossOver's trial in an isolated bottle
   using a research copy and the game's required legacy dependencies. First
   target: reproducible title/menu startup with logs. Steam client requirements
   should be determined from the binary and observed behavior.

CrossOver's current [Mac guide](https://support.codeweavers.com/en_US/crossover-mac-user-guide)
supports Apple Silicon and explicitly supports 32-bit Windows applications in
64-bit bottles. It offers a fully functional 14-day trial. Its
[system requirements](https://www.codeweavers.com/crossover/)
list Tahoe support with CrossOver 25.1.1 or later. Those are platform capabilities,
not evidence that Bullet Witch itself works. No game-specific compatibility
result was established in this check. Direct3D 9, legacy PhysX and media playback
remain concrete runtime uncertainties from the local dependency inventory.

CrossOver would be an observation environment for this Mac; it would not settle
the eventual Xbox implementation architecture. No runtime has been installed,
no game has been executed, and no source game file was modified in this check.
