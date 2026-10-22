# Implementation strategy discussion — playable first, improvements second

October 22, 2026. Historical proposal, not the selected implementation choice.
**Subsequent decision:** the user chose [source reconstruction](../engine/SOURCE-RECONSTRUCTION.md),
followed by a direct port and later upgrades. The comparison below is retained
as the discussion that preceded that decision.
The user clarified the intended sequence: get Bullet Witch working on Series X,
then improve it, including graphics. This preserves the destination in the brief.

## Recommendation

Pursue preservation of gameplay behavior, with native platform and graphics
boundaries. The earlier translate-or-rebuild framing was too binary. Translated
native game code can coexist with a replacement renderer, native Lua runtime and
host services. Specific boundaries still need implementation contracts.

Preserve original Lua, mission data, AI/state logic and timing where practical.
Provide Xbox-compatible graphics, input, files, sound and lifecycle services.
First reproduce existing appearance and behavior; subsequently improve graphics
through the owned renderer interface. This recommendation is engineering judgment
based on the mapped architecture, not a measured schedule or performance claim.

| Approach | Benefit | Cost / risk |
|---|---|---|
| Broad PC binary translation | Retains game-specific behavior without manually rewriting it all | Guest 32-bit memory/calls/exceptions, indirect dispatch, OS/middleware dependencies and translation overhead |
| Native subsystem reconstruction | Explicit data/ownership, easier debugging and deeper modifications | Reimplementing many gameplay details before a faithful complete game; behavioral drift |
| Preserve game logic with native boundaries | Retains behavior while allowing renderer/platform improvements | Must design and validate translated/native transitions; dependency handling remains real work |

## Source checks and implications

Microsoft's published [Xbox UWP resource documentation](https://learn.microsoft.com/en-us/previous-versions/windows/uwp/xbox-apps/system-resource-allocation)
includes Series X|S, requires x64 targets and lists D3D11/D3D12 feature level 11.0.
It lists 1 GB for apps and 5 GB for games, not the historical proposal's assumed
10 GB. This is legacy documentation retrieved today, not measurement of the
user's console. Actual allocation/capabilities should be queried on target.
The original 32-bit Windows/D3D9 executable therefore cannot be treated as an
Xbox-ready package merely because the console CPU is x86-64.

Microsoft DirectX engineer Chuck Walbourn's
[Series X|S UWP article](https://walbourn.github.io/directx-and-uwp-on-xbox-series-x-s/)
also describes D3D11/12 feature level 11.0, 4K swapchains and directs HDR/DXR/Ultimate
work to ID@Xbox. Its updated tooling note requires Windows SDK 22621 or later
for Xbox communication after the August 2023 recovery. The future improvement
pass must use exposed capabilities rather than assume full retail-game access.

[Remill](https://github.com/lifting-bits/remill) translates x86 instructions to LLVM
bitcode and explicitly describes itself as a library for other tools. It is a
possible building block, not an established Bullet Witch-to-UWP pipeline.
[McSema](https://github.com/lifting-bits/mcsema) documents Windows PE lifting and
recompilation, but its repository is archived (August 23, 2022); its old README's
maintenance claims should not override that status. Neither was built or tested
on this game in this discussion.
[XenonRecomp](https://github.com/hedge-dev/XenonRecomp) works on Xbox 360 PPC/XEX
input. It does not directly supply a translator for the acquired Steam x86 image.
Its documented hook mechanism illustrates that translated logic can coexist
with custom functions, without establishing PC toolchain suitability.

## Specific risk from our recovered engine

The [platform map](../engine/subsystems/platform-and-services.md) identifies
D3D9Ex, Windows tasks/events/files, Steam, PhysX 2.6.4 interfaces, XACT/XAudio and
DirectShow. Translating instructions does not implement those external services.
The 321-import inventory is a starting list, not the full compatibility surface:
COM and virtual middleware calls extend it.

A translation runtime must preserve guest 32-bit object layouts and addresses,
map calls/callbacks across the host boundary, and preserve relevant floating-point
and exception behavior. Simply recompiling inferred Ghidra C as 64-bit C++ would
not establish those contracts. Mapping is complete at architectural scope;
executable translation and behavioral equivalence remain unproven.

Legacy PhysX deserves an early implementation decision. Replacing it with newer
physics can change collision/destruction behavior; translating the main executable
alone does not make external physics DLLs usable. Audio/video similarly need an
explicit adaptation, translation or replacement path.

## Proposed implementation sequence

1. Define the guest/native interfaces and a deployable x64 host, maintaining a
   desktop debug build and early Xbox deployment. Keep Mac as research host;
   establish Windows build/deployment tooling when implementing the Xbox package.
2. Build one connected preservation path: original resource loading and script
   callbacks through task scheduling into an interactive title/menu and native
   sprite presentation. Include representative indirect calls and native callbacks.
   Isolated arithmetic translation is not sufficient evidence for this path.
3. Resolve the physics/middleware execution plan early, then extend through one
   playable mission, combat, particles, audio, save/load and broader progression.
4. Preserve a faithful baseline, then add graphics improvements individually.
   Separate higher-resolution targets/texture filtering/postprocessing from
   changes to materials/lighting, and validate frame-rate changes against timing.

Translation work can yield reusable executable routines, reference outputs,
comparison tests, guest-memory/callback adapters and platform/render interfaces,
even if selected subsystems later become native. It becomes poor investment if
most effort goes into general-purpose Windows compatibility without advancing
this game's runnable path. Review against concrete integration milestones and
redirect the obstructing subsystem; avoid open-ended generic emulator work.

No code translator, dependency or runtime was installed or selected here. The
brief's implementation choice remains open pending discussion with the user.
