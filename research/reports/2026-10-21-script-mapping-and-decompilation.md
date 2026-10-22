# Complete script mapping and decompilation

The downloaded Steam build contains **232 discovered Lua chunk occurrences, representing 231 unique chunks**. All have reconstructed Lua that recompiles to the original normalized bytecode. The scan includes stage scripts, startup, bonus missions, developer tests, AI, and a chunk embedded in the executable.

This completes the static Lua corpus pass. It does not establish a working port or complete the reverse engineering of the native engine.

## Start here

- [Complete per-script index](../evidence/script-corpus/INDEX.md): source, provenance, instruction counts, functions, calls, branches, resource literals and callback links for every script.
- [Verified reconstructed Lua](../extracted/scripts-recovered/): **canonical output**. Earlier experimental source folders contain failed or unverified reconstructions and are retained only as evidence.
- [Native API index](../evidence/script-corpus/NATIVE-API.md): addresses, immediate/wait behavior, observed caller argument counts and links to native pseudocode.
- [Accepted-source manifest](../evidence/script-corpus/accepted-sources.json): input/output hashes, validation method and provenance.
- [Work log](../logs/2026-10-21-script-corpus-worklog.md): incremental checkpoints, including failed attempts and corrections.

## Coverage

| Family | Occurrences | Scope |
|---|---:|---|
| Root and regular stages | 19 | Startup, default callbacks and regular-stage variants |
| Test and sample scripts | 38 | Developer areas, characters, maps, save tests and samples |
| Bonus mission packages | 19 | Scripts inside 18 mission packages; one package contains two chunks |
| AI packages | 155 | Every `.amb` payload in `stable/ai/pack.cpr` |
| Embedded executable | 1 | Four `null_*` functions at original executable file offset `0x566244` |
| **Total** | **232** | **231 unique chunks** |

One `cm01_00.lub` chunk is byte-identical across two locations. Both provenance records are retained. Different chunks sharing a name are kept separately by SHA-derived identifiers.

The full scan read 6,922 files and 6,625 CPR archives, traversed 74 DPK containers, and decoded 155 AI wrappers. It accounts for 30,575 prototype occurrences and 333,118 instructions; unique chunks contain 30,567 prototypes and 332,840 instructions.

An independent scan of every file found 236 raw Lua signatures. **232 resolve to the inventoried chunks; four fail the Lua 5.0 header check. No valid unaccounted signature remains.** This supports coverage of visible, uncompressed Lua 5.0 chunks in this installation. It does not establish that unknown compressed/custom formats contain no additional logic. See [signature audit](../evidence/script-corpus/signature-audit.json) and [scan summary](../evidence/script-corpus/summary.json).

## Mapping results

All statically identified Lua calls resolve to functions defined in that script or to a verified native adapter name. The corpus calls **214 native API names**: 176 underscored stage/startup APIs and 38 AI APIs. The maps also retain 4,405 same-script function-name argument links and 1,459 resource literal arguments. These are static references, not evidence that every path or resource is exercised at runtime.

Each script has a per-prototype map with original instruction PCs, global reads/writes, callsites, arguments that can be recovered by straight-line register tracking, and branch targets. Register inference is cleared at branches and merge targets. Unknown argument values remain explicit. The original instruction listing remains available alongside the reconstructed source.

The three registration tables are:

| Scope | Preferred VA | Entries | Registration evidence |
|---|---|---:|---|
| Mission | `0x959748` | 165 | Passed to `0x560860` at `0x43ef6c` |
| Root | `0x959c78` | 65 | Pushed at `0x43fcf1`; registration call at `0x43fcfc` |
| AI | `0x96b508` | 60 | Passed to `0x560860` at `0x5dad44` |

Across the tables: 290 entries, 273 distinct names, 272 unique adapter addresses and 272 unique callback addresses. Every accepted adapter matches the full 18-byte push-arguments/call-bridge/cleanup/return pattern. There are 232 immediate entries using `0x5607f0` and 58 wait entries using `0x560780`; repeated entries are included in those counts. The previously mapped coroutine scheduling behavior applies to these bridges.

The AI table binds `getTargetKnowledgeLevel` twice. Its later entry points to callback `0x5d8130`, superseding the preceding `0x5d44c0` binding when the sequential registration loop runs. Both entries are retained in the evidence. A separate candidate pointer pair named `z` was excluded from the API count because the scripts define it as a variable and it lacks the verified adapter pattern.

See [registration xrefs](../evidence/script-corpus/registration-table-xrefs.txt), [instruction witnesses](../evidence/script-corpus/registration-call-witnesses.txt), [raw tables](../evidence/script-corpus/native-api-tables.json), and [native summary](../evidence/script-corpus/native-final-summary.json).

## AI wrapper mapping

Each `.amb` wrapper starts with `ai\0\0` and a little-endian size, followed by bounded tagged sections: `work`, `head`, `labl`, `tree`, `data`, `str`, and `luac`. The `luac` section stores its inner bytecode length as a **big-endian** u32, followed by the Lua chunk and zero alignment padding. All 155 wrappers passed the extraction bounds and padding checks.

Inside `tree`, a `call` table precedes 12-byte node records. Across **27,079 records**, each node's byte offset exactly matches the numeric suffix of a Lua function name. The call-table offsets consistently address the third u32 field of those records. This provides an explicit structural link between AI nodes and decompiled conditions, including perception, distances, actor life, messages, timers, callbacks and script-data checks.

The raw node fields, label pairs, string lists, call-slot links and complete original wrappers are preserved. Their behavioral meanings are not assigned merely from those structural relationships. See [wrapper metadata](../evidence/script-corpus/ai-wrappers.json), [node mapping summary](../evidence/script-corpus/ai-node-summary.json), and [per-wrapper node maps](../evidence/script-corpus/ai-nodes/).

## Decompilation and validation process

1. `script_corpus.py` reads the source installation, extracts exact chunks, decodes Lua 5.0 prototypes, emits instruction listings and maps, and scans raw signatures independently. All outputs remain under `research/`.
2. unluac was built from unmodified source at commit `744c8149d93f7739f17b7f0f506822fb93ba7193`. Its stock stripped-variable mode produced provisional output but emitted unsupported `goto` syntax for 31 initial stage/mission chunks. Those outputs and compilation errors were retained.
3. The external `UnluacCorpus.java` harness tested a no-locals hypothesis while allowing reconstructed parameter names. This produced exact round trips for 228 of the final 231 unique chunks. It does not change the game or upstream decompiler source.
4. Register lifetimes identified five missing locals across `root` (`titlemenu`, `ending`), `sample` (`load`), and `test_save_01` (`load`, `start`). `repair_script_locals.py` restores them in derived source. All three repaired chunks then matched exactly. Their earlier unverified outputs remain separate.
5. Every accepted source is compiled with **unmodified Lua 5.0.3** on arm64. Its native 8-byte `size_t`/instruction serialization is decoded separately from the game's 32-bit format. Comparison includes every instruction word, constant, prototype, stack size, parameter count, upvalue count and vararg field. Only source/debug metadata is excluded. **All 231 unique chunks pass.** Compilation does not execute game scripts.

Variable names, comments and formatting are reconstructed; exact normalized bytecode does not recover the developer's original source text. Native API behavior still depends on the original engine.

The executable was analyzed with **Ghidra 12.1.4** using the previously validated unpacked copy. The distribution's native decompiler was built for Mac arm64 from its bundled source. A local **Temurin JDK 25.0.4.1** runs both tools; Ghidra and JDK downloads were checked against official release SHA-256 digests. No global Java installation was needed.

Ghidra exported **2,398 native functions**, including all registered callbacks, their adapters, startup anchors, and recursively reachable direct callees. All exports completed. There are **461 unresolved indirect callsites** in that selected graph. Native output is automatic pseudocode with inferred names/types, not buildable original C/C++ and not a claim that the entire executable has been decompiled.

The saved project is `experiments/ghidra-project/BulletWitch.gpr`. Per-function C, status, call edges and analysis logs are under [native-decompilation](../evidence/native-decompilation/). `status.tsv` defines the current export set; older experimental files may remain on disk. The analyzer currently identifies 11,469 functions across the executable, more than this script-focused export set.

Tool versions, download hashes, upstream links and build commands are in [toolchain.json](../experiments/decompilation/toolchain.json). The original headless invocation and project-local runtime/cache settings are in [ghidra-command.json](../experiments/decompilation/ghidra-command.json). Native build logs, Lua compiler build logs and per-script diagnostics are preserved in [experiments/decompilation](../experiments/decompilation/).

## Reproduction

From the project root, using the retained local toolchain:

```sh
python3 research/tools/script_corpus.py
python3 research/tools/decompile_scripts.py no-locals
python3 research/tools/repair_script_locals.py
python3 research/tools/map_script_apis.py
python3 research/tools/finalize_script_research.py
python3 research/tools/native_api_index.py
python3 research/tools/verify_recovered_scripts.py
```

The first four commands need only the downloaded installation and local tools. `native_api_index.py` uses the saved Ghidra exports. Native re-export uses `ExportScriptNative.java`, the saved project, and `evidence/script-corpus/native-labels.tsv`; the initial import command is recorded separately. Individual failures are written to status files so later scripts continue.

## Unresolved work

- **Runtime validation:** no game process has run on this Mac; timing, input, rendering and end-to-end mission behavior remain untested.
- **Native contracts:** 461 indirect callsites remain unresolved; Ghidra types, ownership, virtual dispatch and engine side effects need further analysis. Original PDB symbols were unavailable, and dependent DLL bodies were not imported.
- **AI behavior tree semantics:** node/function links are exact, but the meanings of raw node fields, label values, action data and their evaluator remain incomplete.
- **Resources:** script literals are mapped; texture/material/model/animation formats, resource lookup semantics and loading success remain separate investigations.
- **Unknown formats:** the Lua signature audit covers visible chunks. Logic in native code or unidentified packed formats is not covered by the Lua completion claim.
- **Port architecture:** these results materially improve the PC-based option, but do not decide between compatibility, translation or reimplementation, or approve the original Xbox 360 plan.

These are recorded limits for continued research, not prompts for approval.
The [unresolved ledger](../evidence/script-corpus/unresolved.json) preserves every
unresolved indirect-call address and the remaining research boundaries.

## Source preservation

Original `witch.exe` SHA-256: `f39bf4affcc534bf973c20752f9e6424684096acfa1d4607b67d5241a4c7f6cc`.

Unpacked analysis copy SHA-256: `bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.

The game directory was read only. Extracted chunks, wrappers, reconstructed source, local runtimes, vendor sources, projects, maps and reports are all under this research folder.

A separate [final verification](../evidence/script-corpus/final-verification-summary.json)
recompiled all 231 canonical sources successfully and rechecked all 6,625 CPR
hashes, the source executable hash and the unpacked copy hash. None changed.
