# Engine documentation pass 03: actors and particles

October 22, 2026. Main entry point: [architecture](../engine/ARCHITECTURE.md).

Continued the engine documentation milestone and added particles as a first-class
mapping domain. All research stayed under `research/`; the downloaded game was
read only. No game launch or graphics viewer was required for this pass.

## Results

- Identified original `cgl` effect classes and connected their native factory:
  sprites, crosses, traces, model particles (including motion models), and
  two-point traces. Six type codes select these implementations.
- Inventoried all four effect packs: **585 effect entries, 1,640 part records,
  833 definitions**. All effect-to-part and part-to-definition indices pass bounds
  checks. Entries contain 583 distinct stored IDs; duplicate IDs retain pack context.
- Connected pack loading, script-selected memory allocation, per-instance and
  per-part simulation, particle age/expiry and geometry-writing functions.
- Located five stage `effect.emt` files with **25 sequence entries** and connected
  a stage sequence object to the same native particle creation function.
- Mapped character resource assembly and `_SetEnemy` group/chara expansion to
  model lookup and eight active-game factory slots. Concrete actor allocation,
  the alternate spawn route and the full animation update remain open.

Current details and diagrams live in [particles and effects](../engine/subsystems/particles-and-effects.md)
and [actors and animation](../engine/subsystems/actors-and-animation.md).

## Process and tooling

1. Read the existing architecture, worklist, unpacked-image metadata, native
   disassembly and prior resource/lifecycle exports. Continued connected paths.
2. Used Python 3 standard-library binary parsing and SHA-256 checking to survey
   original MSVC x86 RTTI. Candidate table discovery follows type-descriptor
   references into CompleteObjectLocators and code-pointer tables. The output
   preserves mangled names; method meaning comes from subsequent native tracing.
3. Scanned headers, record tables and names in all 6,625 original CPRs read-only.
   The metadata search selected 872 members, including supporting textures/buffers.
4. Used `native_trace.py` against the existing LLVM disassembly to locate code and
   data references. Used Ghidra **12.1.4**, the existing analysis project and
   `ExportEngineSlice.java` to export three bounded slices with caller/callee
   context. Each slice retains pseudocode, instructions, references, status and
   its exact command/environment. No unrelated analysis-script directory was used.
5. Derived pack table strides and indices from `0x50ed10`, `0x50eb30` and
   `0x50f900`. Derived EVMT field-relative offsets/count from `0x50c520` and
   `0x50aa60`. Checked payload ranges against the independent existing CPR parser;
   recorded hashes of all nine inspected payloads and their archives.
6. Checked selected direct-call opcodes, virtual-table pointers and the actual
   `CALL [EDX+0xc]` instruction against image bytes with `engine_pass03_witness.py`.
   Used those witnesses to substantiate the map rather than relying on inferred C types.

Reproduction commands, run from the project root:

```sh
python3 research/tools/particle_survey.py
python3 research/tools/effect_pack_inventory.py
python3 research/tools/engine_pass03_witness.py
```

Ghidra commands and environment overrides:
[03a](../evidence/engine/pass-03a/command.json),
[03b](../evidence/engine/pass-03b/command.json),
[03c](../evidence/engine/pass-03c/command.json).
The analysis image SHA-256 remains
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.

## Verification and limits

All 6,625 metadata scans completed without errors. The four pack inventories
passed section/payload and cross-reference bounds checks. Three Ghidra slices
exported **292 rows / 257 distinct function addresses**, all with successful
pseudocode output; these are exports, not 257 fully understood functions, and
can overlap the prior baseline. Eight direct-call witnesses, two sprite vtable
slots and one indirect-dispatch instruction passed byte checks.

Documentation verification covered 269 Markdown files and 1,792 local links, with
zero missing links; the maps contain 12 Mermaid blocks (not a rendering test).

The stored packs use 703 sprite, 122 model, one type-2 trace and seven type-5 trace
definitions. Cross and two-point code is present but absent from these definitions;
that does not establish their absence from every possible runtime path. The five
EVMTs are stage sequence files, not a census of every particle emitter.

Unresolved contracts are tracked as ACTOR-01/02 and PFX-01 through PFX-05 in the
[worklist](../engine/WORKLIST.md). These include emitter equations/curves, randomness,
model-motion ownership, final draw dispatch/state, allocator limits and full
lifetime behavior. No game execution or visual comparison has corroborated this
static reconstruction yet. Those unknowns do not block continued engine mapping.

Incremental checkpoints: [pass 03 log](../logs/2026-10-22-engine-pass-03.md).
