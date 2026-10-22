# Startup script evaluation — October 21, 2026

**Later result:** [offline unpacking succeeded on the Mac](2026-10-21-offline-unpacking.md).
The native-code limitation documented below applies to the original on-disk
image; the unpacked research copy now provides readable startup adapters.
The subsequent [native mapping](2026-10-21-native-startup-mapping.md) resolves
the static root-driver/registration path and cooperative Lua wait mechanism;
the runtime caveats below remain applicable.

The Steam build contains readable Lua 5.0-format startup logic. `root.lub`
defines a small state machine that requests company logos, a storage check,
an advertise sequence, the title screen, character loading, and the title menu.
The requests cross into native engine functions; the script does not implement
those screens itself. This makes the PC build a useful behavioral reference and
provides a concrete boundary to investigate before selecting an implementation.

This was a static evaluation. The game has not been launched, and its selection
of `root.lub`, invocation schedule, wait behavior, and visible screens have not
been confirmed at runtime. The implementation plan remains unapproved.

## Sources and validation

Source installation: `/path/to/BulletWitch-Steam`, used read-only.
All generated scripts, extracted copies, and evidence are under this research
folder. Source executable SHA-256:
`f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc`.

The three inspected packages are under `data/data/stable/script/`:

| CPR member | Bytes | Role observed in the script |
| --- | ---: | --- |
| `root.lub` | 4,235 | Startup, title/menu dispatch, campaign and bonus mission routing |
| `root_riff.lub` | 1,235 | Alternative test-map sequence; active use unproven |
| `default_callback.lub` | 641 | Mission-clear and game-over callbacks |

Archive/member hashes and byte counts are recorded in
[extraction.json](../evidence/startup/extraction.json). The
[decoder](../tools/lua50_read.py) follows the official Lua 5.0
[chunk loader](https://www.lua.org/source/5.0/lundump.c.html),
[opcode definitions](https://www.lua.org/source/5.0/lopcodes.h.html), and
[limits](https://www.lua.org/source/5.0/llimits.h.html). These chunks use
little-endian 32-bit integer, size and instruction fields, and 64-bit numbers.
Lua 5.0 puts operand A in the high eight instruction bits and uses 250 as the
register/constant operand boundary; applying later Lua layouts would misdecode
these files.

The reader consumed all 57 `.lub` members in the stable script directory without
trailing bytes or structural errors: 1,751 function prototypes and 73,883
instructions. Checks cover chunk boundaries, supported opcodes, direct constant
and child-prototype indexes, and explicit jump destinations. This is structural
validation, not a complete VM verifier or proof of native compatibility. Scripts
elsewhere in the game were not included. See
[validation.json](../evidence/startup/validation.json).

## Startup sequence

The top-level chunk only defines nine functions; it does not call `init()` or
`main()`. `main()` performs one dispatch and returns. The engine must supply the
invocation mechanism for this to act as a recurring state machine; that mechanism
is still unresolved.

When `init()` reads zero from native global slot 1, it sets `demotype=0`,
`mode=0`, and `stage=1`. Subject to subsequent host calls, the script requests:

```text
mode 0: company logo → wait → memory-card check → wait
          ↓ mode becomes 1
mode 1: advertise sequence → wait → title screen → wait → title status
          ↓ only status 1 changes mode to 2
mode 2: character loading → wait → title menu → wait → menu status
```

The labels above describe the native call names. In particular,
`MemoryCardCheck` does not establish that the PC build accesses a memory card;
its storage implementation is unknown. `Advertise` plausibly denotes an attract
sequence, but its contents have not been observed.

If slot 1 is nonzero, `init()` clears it and starts at `mode=1`, `demotype=1`.
That skips the logo/storage branch and skips one advertise sequence. A title
status other than 1 leaves mode unchanged. On status 1, `demotype` becomes 1 and
mode becomes 2; returning to the title through the menu therefore skips one
advertise sequence as well.

Exact instruction references and the reconstructed startup portion are in
[root.lub.txt](../evidence/startup/root.lub.txt) and
[startup-flow.lua](../evidence/startup/startup-flow.lua). The reconstruction is an
analysis aid, not recovered original source or a replacement game script.

## Menu and mission routing

| Menu status | Script action | Interpretation |
| --- | --- | --- |
| 1 | Set stage and native slot 0 to 1; mode 10 | Likely new campaign |
| 2 | Read stage from slot 0; mode 10 | Likely continue |
| 3 | Read `_GetLatestStageSelectNo()`; mode 11 | Stage selection |
| 6 | Mode 12 | Concept/bonus mission route |
| 7, or any other value | Mode 1 | Return to title |

These are status values, not proven menu positions or visible labels. Modes 10
and 11 map stages 1–6 to `st01_00` through `st06_00`. Mode 12 sets native slot 3,
starts the mission named `concept`, waits, stops music, unloads concept mission
resources, and eventually clears slot 3. The root script does not identify which
of the 18 inventoried bonus missions the native menu selects.

Campaign mission status 1 leads through newspaper/results and increments the
stage. Passing stage 6 resets the stage to 1, sets native slot 7, and chooses
ending mode 3. The ending function increments slot 4, calls `_ExecBonusOpen(0)`
and its wait, saves, plays `BW6_0600`, then returns to title mode. This establishes
a bonus-opening call in the script; it does not establish costume unlock rules,
Steam entitlement checks, or the behavior of `_ExecBonusOpen`.

`root_riff.lub` sequences 12 named test maps and ends in a loop that displays
`Finished` and waits one frame. Its structure suggests a development/test route;
presence alone does not prove it is reachable in the shipped game.
`default_callback.lub` sets mission status 1 after a fade on clear, and its
game-over callback coordinates event cancellation, audio/magic changes, object
removal, and the game-over screen. Neither package was executed.

## The native boundary

The root script references 35 distinct underscore-prefixed native globals.
Seventeen occur in `init`, `first`, `title`, and `titlemenu`:

| Responsibility | Referenced functions |
| --- | --- |
| Engine state | `_GetGlobalValue`, `_SetGlobalValue` |
| Logos | `_ExecCompanyLogo`, `_WaitCompanyLogo` |
| Storage check | `_ExecMemoryCardCheck`, `_WaitMemoryCardCheck` |
| Advertise sequence | `_ExecAdvertise`, `_WaitAdvertise` |
| Title | `_ExecTitle`, `_WaitTitle`, `_GetTitleStatus` |
| Character loading | `_ExecCharacterLoad`, `_WaitCharacterLoad` |
| Menu | `_ExecTitleMenu`, `_WaitTitleMenu`, `_GetTitleMenuStatus`, `_GetLatestStageSelectNo` |

The executable contains all 51 distinct native names used across the three
inspected scripts. Each has at least one candidate name/handler pointer pair in
non-executable data, with the handler address in an executable section. For
example, at preferred image base `0x400000`, `_ExecTitle` has a candidate table
entry at VA `0x959ce0` pointing to VA `0x476560`; `_WaitTitle` follows at
`0x959ce8`, pointing to `0x476580`. Full file offsets, preferred virtual
addresses, and script use sites are in [native-map.json](../evidence/startup/native-map.json).
These are candidate registration records, not demonstrated function signatures
or runtime registrations. Runtime relocation may change the addresses.

The `_Exec...`/`_Wait...` pairs establish call order. They do not reveal whether
waiting yields a script task, blocks a thread, polls, or resumes through another
mechanism. The 17 functions are the visible startup-script interface, not a count
of all engine services needed to reach the title. Graphics, audio, input, asset
loading and other native initialization may happen before or inside these calls.

## Native-code limitation found during this evaluation

**Follow-up:** a subsequent read-only check matched Steamless's SteamStub 3.1
x86 detector, specifically its pattern labelled 3.1.2 and expected header size.
See [Mac next steps](2026-10-21-mac-next-steps.md). Offline unpacking should be
investigated before requiring a runtime baseline to recover readable code.

The on-disk `.text` section has entropy of approximately **7.999945 bits per
byte** across 2,876,416 bytes. Disassembly around the candidate title and loading
handlers contains implausible privileged instructions and random-looking
operands. The PE entry point is RVA `0x18fa310`, preferred VA `0x1cfa310`, in
`.bind`; its initial instruction sequence has a coherent save/call/restore
structure. The file-offset and VA mapping was checked against the PE sections.

Together these observations strongly suggest packed or encrypted code that is
transformed by a loader. They do not identify the protector, prove its algorithm,
or establish that the loader transforms all of `.text`. No native handler logic
is claimed from the on-disk disassembly. Evidence:
[section measurements](../evidence/startup/native-map.json),
[entry disassembly](../evidence/startup/on-disk-entry-disassembly.txt),
[title-region disassembly](../evidence/startup/on-disk-title-disassembly.txt), and
[loading-region disassembly](../evidence/startup/on-disk-loading-disassembly.txt).

This adds a prerequisite for native analysis or recompilation of this PC build:
obtain an interpretable view of its actual runtime code and verify the loader
handoff. The current bytes are not a reliable basis for native control-flow
recovery. Readable imports, assets, Lua, and pointer tables remain useful evidence.

## Implications for the revised plan

The PC release remains a worthwhile candidate and reference. We can recover
substantial script orchestration without reconstructing it from x86. A port or
reimplementation could potentially reuse that logic, provided it supplies the
expected Lua behavior, native API, state, scheduling, and assets. This finding
does not yet establish the effort or feasibility of replacing the native engine.

The original next-step recommendation was a controlled runtime baseline. The
[wrapper follow-up](2026-10-21-mac-next-steps.md) now puts offline unpacking first.
When runtime observation is needed, its acceptance points should be concrete:

1. Observe loader handoff and determine when native code becomes interpretable.
2. Confirm which root package is loaded and who invokes `init()` and `main()`.
3. Trace the first logo/title Exec/Wait pair to establish scheduling and completion.
4. Record the actual files, DLLs and platform APIs needed to reach the title and
   title menu; compare them with this static sequence.
5. Only then estimate the work for compatibility, native translation, or engine
   reimplementation, and compare the Xbox 360 route on equivalent evidence.

Selecting a practical runtime environment on this Mac remains open. This study
has not installed a compatibility runtime or established Xbox deployment
feasibility. A mock Lua harness could test routing independently, but would not
answer the native startup questions above.

## Reproduction

Run from the project root; outputs are fixed beneath this research directory:

```sh
python3 research/tools/startup_extract.py
python3 research/tools/startup_validate.py
python3 research/tools/startup_native_map.py
```

The extraction reads CPR/DPK members and saves their exact bytes plus decoded
JSON and instruction listings. The validation scans the stable script packages.
The native mapping reads the executable without executing it. All tools use the
local source installation path noted above.
