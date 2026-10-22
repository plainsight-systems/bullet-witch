# Steam release inspection

Follow-up: [Asset layout and container research](2026-10-21-asset-layout.md)
resolves several format questions raised in this initial inspection.

Inspected October 21, 2026, America/Phoenix. The Steam release provides a concrete
starting point for evaluating the PC route: a 32-bit x86 Windows executable,
Direct3D 9 imports, legacy PhysX dependencies, and descriptively organized assets.
This inspection establishes the contents of the downloaded build; it does not
establish a working port or select an implementation architecture.

## Acquisition and build identity

The owner created a Steam account and purchased Bullet Witch. DepotDownloader
3.4.0 downloaded app **696180**, targeting Windows, all architectures, and English,
with validation enabled. Its local completion status is `complete`.

The download contains **6,922 files totaling 4,381,764,192 bytes**, approximately
4.08 GiB, excluding `.DepotDownloader` metadata and including redistributables.
The game has not been launched during this investigation.

The executable is `witch.exe`, 7,373,968 bytes. Its SHA-256 is:

```text
f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc
```

The PE header reports June 11, 2018 as its linker timestamp. This is a binary
metadata value, not an independently verified release date.

Evidence: [inspection summary](../evidence/inspection-summary.json),
[complete file inventory](../evidence/steam-file-inventory.tsv).

## Executable and dependencies

`file` identifies `witch.exe` as a PE32 GUI executable for Intel 80386. Its PE
header marks it large-address-aware. The linker major version is 12, and it
imports `MSVCR120.dll`. `NxCooking.dll` is also a 32-bit x86 DLL.

| Area | Observed evidence |
| --- | --- |
| Graphics | `d3d9.dll`, specifically `Direct3DCreate9Ex`; `d3dx9_43.dll` |
| Shader utilities | Imports include `D3DXCompileShader` and `D3DXCompileShaderFromFileA` |
| Input | `DINPUT8.dll`, `XINPUT1_3.dll` |
| Physics | `NxCooking.dll`, `PhysXLoader.dll`; `NxCreatePhysicsSDK`, `NxCookTriangleMesh`, `NxCookConvexMesh` |
| Audio | `X3DAudio1_7.dll`, `WINMM.dll`; executable strings include `IXAudio2VoiceCallback` |
| Steam | `steam_api.dll`; imports include initialization, callbacks, and interface access |
| C runtime | `MSVCR120.dll`; Visual C++ 2013 installers are bundled |
| Windows services | `KERNEL32.dll`, `USER32.dll`, `GDI32.dll`, `ole32.dll`, `COMCTL32.dll`, `ADVAPI32.dll` |

The executable imports `LoadLibraryA` and `GetProcAddress`, so its static import
table is not a complete runtime dependency map. The XAudio2 string is a clue to
audio implementation, not a traced call.

Top-level files include `witch.exe`, `NxCooking.dll`, `steam_api.dll`,
`sdkencryptedappticket.dll`, `SystemSetting`, and `runtime_installed`.
`PhysXLoader.dll` is imported but is not present as a loose file in the inventory.
The bundled legacy PhysX installer and an executable string referencing it
provide a likely installation path; this has not been exercised.

The executable contains the debug path `H:\bw\dev\project\witch\Release\witch.pdb`.
That string does not mean a PDB was shipped; no `.pdb` file appears in the inventory.

Evidence: [PE headers](../evidence/witch-pe-headers.txt),
[section headers](../evidence/witch-sections.txt),
[parsed imports](../evidence/witch-imports.json), and
[selected executable strings](../evidence/witch-selected-strings.txt).

## Asset layout

The `data` directory contains `concept`, `data`, `menu`, `movie`, `shader`, `sound`,
and `text`. Under `data/data/stable/graphics`, directories include `bg`, `bgobj`,
`chara`, and `posteffect`. Deeper paths identify meshes, textures, control points,
and collision-related resources, for example `model/mesh.cpr`,
`texture/default.cpr`, and `hit/cpe_hit.cpr`.

| Extension | Files | Total bytes | Observation |
| --- | ---: | ---: | --- |
| `.cpr` | 6,625 | 2,130,068,868 | Sampled files begin with `CPR1`; structure not decoded |
| `.ssu` | 18 | 1,881,827,792 | Movie directory; `file` identifies sampled `BW0_0100.ssu` as Microsoft ASF |
| `.xwv` | 24 | 53,599,318 | Sampled music files contain `RIFF` and `XWMA` headers |
| `.xwb` | 2 | 167,766,016 | Voice and sound-effects files begin with `WBND` |
| `.rat` | 25 | 989,248 | Sampled files begin with `RAT ` |
| `.bin` | 55 | 1,734,810 | Sampled text files begin with `STMANAGE` |

The shaders are stored in `data/shader/shaders.cpr` (275,120 bytes) and
`data/shader/shader.rat` (27,744 bytes). The executable also contains repeated
`Microsoft (R) D3DX9 Shader Compiler` strings. Shader source availability, bytecode
layout, and the relationship between those files and embedded shader data remain
unresolved.

Evidence: [file inventory](../evidence/steam-file-inventory.tsv),
[sampled asset headers](../evidence/asset-header-samples.json), and
[file type identification](../evidence/file-types.txt). Header observations apply to
the sampled files; they are not a full validation of every file with that extension.

## Implications for the draft brief

- The Steam build merits investigation before choosing the Xbox 360 route.
- The PC binary is 32-bit x86. Any proposed destination must account for its
  instruction set, address model, and Windows dependencies.
- The observed graphics imports are Direct3D 9. The original brief's “DX9/11”
  description was not verified for this build.
- Legacy PhysX, Steam integration, and dynamically loaded components need specific
  investigation before estimating compatibility work.
- The asset tree offers accessible starting points for format inspection, although
  descriptive filenames do not establish that the formats are easy to decode.
- Downloading and statically inspecting the release on macOS required no Steam
  desktop client or Windows compatibility layer. Running it is a separate question.

These findings neither prove nor rule out a PC-based route to Series X. The
original assertion that a complete private Win32 to UWP translation layer must be
built from scratch remains unsubstantiated.

## Sources and reproduction

- [Steam store listing](https://store.steampowered.com/app/696180/Bullet_Witch/):
  Windows release, DirectX 9.0c requirements, and 5 GB storage requirement.
- [DepotDownloader documentation](https://github.com/SteamRE/DepotDownloader):
  account authentication, target OS selection, and download validation.
- [Downloader release used](https://github.com/SteamRE/DepotDownloader/releases/tag/DepotDownloader_3.4.0).
- Local game files and the evidence linked above are the basis for binary findings.

The download script prompts for the account name and lets DepotDownloader handle
password and verification entry interactively. Its download arguments are:

```text
-app 696180 -os windows -all-archs -language english -username <account-name> -dir <game-directory> -validate
```

The executable header and section reports were produced with:

```sh
xcrun llvm-objdump --private-headers /path/to/BulletWitch-Steam/witch.exe
xcrun llvm-objdump --section-headers /path/to/BulletWitch-Steam/witch.exe
```

The inventory recursively records relative paths and byte sizes, excluding
`.DepotDownloader`. Sample headers record the first 48 bytes of the first two
files in path order for each selected extension. The executable hash identifies
the inspected binary; asset hashes and Steam build/depot manifest identifiers
have not yet been recorded.
