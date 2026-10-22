# Offline unpacking on the Mac — October 21, 2026

**Result: successful for static analysis.** The SteamStub wrapper was processed
offline on this Apple Silicon Mac using the upstream Steamless variant 3.1 x86
plugin. The recovered `.text` disassembles coherently, the original entry point
is restored, and all 51 previously located Lua native-handler candidates match
the same short x86 adapter structure. The game was never executed.

The original downloaded executable and the input research copy retain their
original SHA-256. All experiment sources, copied binaries, build output, logs,
and validation evidence are inside `research/`.

## Method and provenance

Upstream: [atom0s/Steamless](https://github.com/atom0s/Steamless), pinned at
commit `cd770bf9749d3e4f438d23ac643917ad1a804257`. Source is in
`vendor/Steamless/`; its working tree remains unmodified. Upstream's license and
attribution are retained there. This is a local personal-research build; neither
the game binaries nor the assembled tool have been published.

The existing .NET SDK 10.0.401 built a small arm64 command-line host from the
upstream API and x86 variant 3.1 plugin sources. The build excludes Windows UI
classes, assembly metadata, and the unused PE64 helpers. No NuGet packages were
required; package sources are disabled for this experiment. No Windows runtime,
Steam client, or compatibility layer was installed for the operation.

The [host](../experiments/steamstub/Program.cs) checks the expected source hash,
copies `witch.exe` into the experiment folder, invokes the plugin, and records
results. It refuses to overwrite an existing input/output. It never loads the
game as executable code. The plugin's own detector accepted the file; its decoded
header validated signature `0xc0dec0df`, app ID **696180**, original entry RVA
`0xffea4`, and encrypted code size **2,876,416 bytes**.

Options removed `.bind`, preserved section positions and the DOS stub, and did
not emit additional payload/DLL dumps or recalculate the PE checksum. The checksum
is zero in the output. The run used the normal entry-point path for this file;
the alternate TLS path was not needed. No unpacker algorithm changes were made.

See [build/run log](../experiments/steamstub/build-and-unpack.log) and
[unpack result](../experiments/steamstub/unpack-result.json). The build emitted
upstream obsolete-crypto and read-length warnings, but completed successfully.

## Output and independent checks

| Property | Original | Unpacked research copy |
| --- | --- | --- |
| File size | 7,373,968 bytes | 7,232,512 bytes |
| Sections | 6, including `.bind` | 5, `.bind` removed |
| Entry RVA | `0x18fa310` | `0xffea4` |
| Preferred entry VA | `0x1cfa310` | `0x4ffea4` |
| `.text` entropy | 7.999945 bits/byte | 6.488609 bits/byte |

Input/source SHA-256:
`f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc`

Output SHA-256:
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`

Output: `experiments/steamstub/witch.original.exe.unpacked.exe`.

The independent [validator](../tools/validate_unpacked.py) checks PE signatures,
architecture, file bounds, section alignment/non-overlap, image bounds, and an
entry point within executable code. All surviving section positions and sizes
are unchanged. `.rdata`, `.data`, `.rsrc`, and `.reloc` are byte-identical to the
original. All PE data-directory entries are unchanged, including imports. The
original installation and input-copy hashes still match the known source hash.
Results: [validation.json](../experiments/steamstub/validation.json).

Entropy reduction alone would not prove successful recovery. The stronger check
is that **51 of 51** distinct handler addresses from the earlier name/pointer
mapping now contain a valid, consistent instruction sequence: push a function
address, push the incoming argument, call a shared bridge, balance the stack,
and return. Both the function address and bridge destination fall in executable
code. The restored entry also contains coherent startup instructions.

## Startup handlers are now readable

Addresses below are preferred VAs at image base `0x400000`, not observed runtime
addresses. The column headed function argument is the address passed into the
bridge, not proof that the target is directly invoked on every call.

| Lua name | Adapter | Function argument | Shared bridge |
| --- | --- | --- | --- |
| `_ExecCompanyLogo` | `0x476720` | `0x4787a0` | `0x5607f0` |
| `_WaitCompanyLogo` | `0x476740` | `0x47d3a0` | `0x560780` |
| `_ExecMemoryCardCheck` | `0x476a80` | `0x478810` | `0x5607f0` |
| `_WaitMemoryCardCheck` | `0x476aa0` | `0x47d460` | `0x560780` |
| `_ExecTitle` | `0x476560` | `0x4788c0` | `0x5607f0` |
| `_WaitTitle` | `0x476580` | `0x47d750` | `0x560780` |
| `_ExecCharacterLoad` | `0x476a20` | `0x478790` | `0x5607f0` |
| `_WaitCharacterLoad` | `0x476a40` | `0x47d370` | `0x560780` |

The inspected Exec and Wait adapters use different bridges. This provides a
specific starting point for resolving wait/scheduling semantics; those semantics
have not yet been fully traced. See
[adapter disassembly](../experiments/steamstub/title-handlers.txt),
[bridge disassembly](../experiments/steamstub/lua-bridges.txt), and
[restored entry](../experiments/steamstub/restored-entry.txt).

## What this establishes

The packing obstacle to static native-code analysis has been removed for this
research copy. A runtime environment was not required to reach this point.
The next investigation can follow the Exec/Wait bridges and their target
functions using this image.

This does **not** establish that the rebuilt executable starts correctly, that
Steam is unnecessary, or that a Mac/Xbox port is feasible. Its Steam API imports
remain intact. No full-code semantic verification or runtime comparison has
been performed. Steam integration and runtime compatibility remain separate
questions from successful code recovery.

## Reproduction and files

The project file is [OfflineUnpack.csproj](../experiments/steamstub/OfflineUnpack.csproj).
From the project root, the build/run command was equivalent to:

```sh
DOTNET_CLI_HOME="$PWD/research/experiments/steamstub/dotnet-home" \
DOTNET_CLI_TELEMETRY_OPTOUT=1 \
DOTNET_GENERATE_ASPNET_CERTIFICATE=false \
dotnet run --project research/experiments/steamstub/OfflineUnpack.csproj
python3 research/tools/validate_unpacked.py
```

The existing successful experiment is preserved: the host intentionally stops
if its research input/output already exists. The validator can be rerun without
changing the executable. The disassembly was produced by Xcode's
`xcrun llvm-objdump`, not by executing game code.
