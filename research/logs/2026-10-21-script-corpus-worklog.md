# Script corpus mapping and decompilation — work log

Scope authorized: map and decompile all discoverable game scripts, continue past individual blockers, and preserve research incrementally. Game installation stays read-only. All derived data and tooling remain under research/.

## Initial scope

- Expand the earlier 57-chunk stable/script inventory to every CPR payload, nested DPK and loose script candidate.
- Preserve extracted chunks, structural decoding, per-prototype calls and references, and per-script status.
- Attempt Lua 5.0 source reconstruction with unluac, validate reconstructed output where possible, and retain failures individually.
- Analyze the unpacked x86 executable with Ghidra and connect script globals to native registration tables and decompiled handlers.

## Initial blockers and limits (resolution recorded below)

- The system Java launcher has no installed runtime. A project-local runtime is being acquired.
- The Adoptium assets API returned HTTP 403; use its official GitHub release assets instead.
- Automatic decompiler output is provisional until validated; native pseudocode is not recovered original C/C++ source.
- No game execution or dynamic behavior validation has occurred on this Mac.

## Checkpoints

- Started autonomous corpus pass. Earlier startup mapping and offline unpacking evidence retained unchanged.

- Whole-installation scan checked 6,922 files and 6,625 CPR archives. Found 76 stage/root/test/bonus-mission Lua occurrences in 74 DPKs, with one exact duplicate. All structurally decoded.
- Built unmodified unluac revision 744c8149d93f7739f17b7f0f506822fb93ba7193 with local Temurin 25.0.4.1. Stock stripped-variable mode emitted Lua 5.2-style goto syntax in 31 scripts; retained failed Lua 5.0 compilation logs.
- Added an external unluac harness to test the no-locals hypothesis (no upstream edits). Recompiled using unmodified Lua 5.0.3, decoding native 64-bit serialization separately. First pass: 73/76 occurrences exact normalized bytecode match; root, sample and test_save_01 differed and remain unverified.
- Exact normalized comparison includes instructions, constants, stack, parameter, upvalue and vararg fields and nested prototypes; only source/debug metadata excluded. It is stronger than syntax checking.
- Found all 176 native API names referenced by the initial corpus in PE name/function pairs. Built Ghidra 12.1.4's native decompiler for arm64 from its bundled source. Saved Ghidra project; exported 1,707 reachable native functions with no export failures, including 361 research-labelled anchors. Indirect calls and missing original types remain unresolved.
- Raw signature audit exposed another 155 Lua 5.0 chunks in stable/ai/pack.cpr. Its 155 .amb payloads are ai containers with length-prefixed work/head/labl/tree/data/str/luac tags. Lua tag has a big-endian byte-length field inside the otherwise little-endian tag layout. Expanding extraction and decompilation to these; retaining tree metadata and original wrappers.

## Final checkpoint — completed Lua corpus pass

- AI expansion succeeded: 155 wrappers, 27,079 tree records linked exactly to generated Lua function names by node byte offset. All AI chunks round-tripped exactly.
- The three remaining mismatches were localized to five missing locals: root/titlemenu, root/ending, sample/load, test_save_01/load and test_save_01/start. Reconstructed locals from bytecode register lifetimes; all repaired chunks now match normalized bytecode exactly. Initial decompiler output is retained as failed evidence.
- The independent signature audit discovered a further valid 531-byte chunk inside witch.exe at file offset 0x566244, defining null_0/null_12/null_24/null_36. Extracted, mapped, decompiled and verified it. Final coverage: 232 occurrences / 231 unique chunks; 30,575 prototype occurrences / 333,118 instructions. All 236 raw Lua signatures accounted for: 232 chunks, four invalid headers.
- Final native scope expanded to all three tables (165 mission, 65 root, 60 AI entries), not only names used by scripts. 273 distinct API names, 272 unique handlers; 214 API names called by the recovered corpus. 2,398 native pseudocode functions exported with zero exporter failures. 461 indirect callsites remain explicitly unresolved.
- Canonical Lua is in extracted/scripts-recovered. Every one of the 231 unique files passed a separate final compilation and normalized-bytecode comparison. Per-script provenance, SHA-256s, maps, resource literals, callback links and a full index are saved. No game code was executed.
- Source preservation recheck: all 6,625 CPR archive hashes match the earlier inventory; original and unpacked executable hashes are unchanged.
- Main report: 2026-10-21-script-mapping-and-decompilation.md. Machine-readable remaining issues: evidence/script-corpus/unresolved.json. Native runtime contracts, indirect dispatch, AI tree field semantics, unknown packed logic and runtime/gameplay validation remain open; they do not block completion of this static Lua pass.
