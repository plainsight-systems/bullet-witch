# Engine documentation pass 04 — particles and actor factories

October 22, 2026. The connected particle mapping pass is complete, with precise
unresolved numeric/format edges retained. Work has returned to actor construction
and shared animation. Main entry point: [architecture revision 5](../engine/ARCHITECTURE.md).

## Results

The [particle runtime reference](../engine/subsystems/particle-runtime-contracts.md)
connects pack binding and curves to emission, shared random state, motion/collision,
child attachment, model animation, geometry, GPU submission and cleanup.

- Inventoried 623 scalar curves and 327 vector tables in the four effect packs;
  checked all 833 definitions' three curve references. Documented initial load
  precedence for duplicate IDs 7 and 12000.
- Recovered the shared 32-bit random generator, periodic/history emission paths,
  16-step catch-up cap, authored timing fields and per-particle life setup.
- Connected collision and child-effect creation/attachment to their shared engine
  interfaces. Animated model particles reach the shared pose evaluator and samplers.
- Resolved part geometry virtual dispatch through CEffectCommand to D3D9 upload
  and triangle drawing. Recorded six blend modes and seven SM3 vertex/pixel pairs,
  constant bindings, program sizes and hashes.
- Mapped the 512-instance limit, optional 16-per-group limit, eligible eviction,
  retained children, graceful/forced stop, instance recycling and manager teardown.
- Decoded all 25 stage EVMT names and effect IDs; all IDs resolve in the packs.
  Separated G particle dispatch from the S type-5 service path and recorded nested
  EVMT consumers. Detailed channel encodings remain a local question.

The [actor map](../engine/subsystems/actors-and-animation.md) now connects task
mode construction to publication of the active game pointer. Nine game vtables
share the same eight actor factory targets. The factories resolve to concrete
allocation/constructor paths. Following constructor 0x5fe070 establishes two
registration nodes, a model-component pointer/back-pointer and named weapon
attachments. This supplies the next connected route into actor update/removal.

## Process and tooling

1. Used the existing LLVM instruction index and native exports to identify missing
   particle dispatch, command execution, simulation and lifetime edges.
2. Expanded bounded Ghidra exports to the discovered cgl vtable methods and
   connected helpers, then exported the concrete game/actor factory slice.
   [04a command](../evidence/engine/pass-04a/command.json),
   [04b command](../evidence/engine/pass-04b/command.json) and
   [04c command](../evidence/engine/pass-04c/command.json) retain exact command lines,
   Java paths, environment overrides, logs, seeds, incoming references and x86
   instruction listings beside inferred C.
3. Read original CPR payloads through recorded offsets and struct decoding.
   [particle_contract_audit.py](../tools/particle_contract_audit.py) reproduces
   curve bounds, EVMT metadata/ID checks, duplicate binding sequences, class method
   catalog and SM3 token/hash manifest. It also decodes the x86 D3D9 method order
   from the retained primary-source Wine header.
4. Checked critical dispatch bytes and game vtable entries independently against
   the hash-verified image using
   [engine_pass04_witness.py](../tools/engine_pass04_witness.py).
5. Updated living subsystem notes, diagrams, architecture, worklist and open
   questions; retained detailed checkpoints in the [work log](../logs/2026-10-22-engine-pass-04.md).

Both scripts run with Python 3 from the project directory. Tools and outputs are
under research; the installed game is only read. No wrapper or game execution was
needed for these static findings. No changes were made to the game files.

## Validation and boundaries

Native exports report **442 + 213 + 60 successful function exports**, with
**606 unique function addresses** across the three overlapping slices. These are
export counts, not a claim that 606 functions were fully interpreted. The audit
catalog covers 41 candidate cgl vtables; excluded shared methods are three return
stubs and an import thunk. Original RTTI descriptors are not counted as separate
particle systems.

The [raw witnesses](../evidence/engine/pass-04/native-witnesses.json) verify seven
instruction sequences, command executor binding and the eight factory entries
in all nine game vtables. Curve references and all stage entry IDs pass the
reproducible audit. The [validation result](../evidence/engine/pass-04/document-validation.json)
records current local Markdown link checks. The [function index](../evidence/engine/pass-04/FUNCTIONS.md)
links the named native functions to their C and instruction exports.

Preserved local questions include remaining authored-word names/units,
specialization-specific math, the unusual scalar terminal index, precise EVMT
channel/axis semantics, allocator fragmentation/failure behavior and S service
classification. Shared collision, mesh rendering and animation codecs continue
under their engine domains. Shader translation and visual comparison remain
implementation work. There has been no dynamic gameplay validation.

## Next connected engine work

Follow actor constructor registration nodes into their owners' update/removal
passes, then recover ordering across AI, physics, pose and attachments. Continue
MODEL-03 through 0x423d20 and samplers 0x429fe0/0x4299b0. The particle interfaces
now provide concrete consumers for those shared systems; graphics implementation
remains deferred as agreed.
