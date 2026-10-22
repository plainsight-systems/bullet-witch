# Engine mapping checkpoint — October 22, 2026

## Direction and structure

Updated [BRIEF](../../BRIEF.md) to record the user's confirmed Steam PC engine
mapping direction. The original Xbox 360 proposal remains explicitly historical
and unapproved. Established [architecture](../engine/ARCHITECTURE.md), a subsystem
worklist and four editable Mermaid diagrams across three map documents.

Moved dated reports to `reports/`, the existing worklog to `logs/`, and living
architecture/questions to `engine/`. Updated Markdown links after relocation.
The one-time [relocation script](../tools/organize_research.py) and
[relocation record](../evidence/engine/research-relocation.json) document the move.
Do not rerun the one-time migration against the reorganized layout.
Generated corpus paths and source game files were not moved.

## New mapping

Followed recovered root/mission Lua and native pseudocode, then inspected x86
instructions with `tools/native_trace.py` against the existing full disassembly.

- `_StartMission`: callback `0x47cf10`, owner mission selection and completion flag.
- `_WaitMission`: callback `0x47d4c0`, phase `0x42c` and owner `+0x61410` polling.
- `_LoadStage`: callback `0x47a3e0`, **yielding** request and phase `0x723` polling.
- Resource request `0x442720` submits `0x4427a0` and copies the stage name.
- Submission helper `0x475b30` / `0x475a90` allocates and copies argument data;
  pending helper `0x475870` / `0x475820` counts occupied slots.
- Resource-owner getter `0x432220` is distinct from orchestration getter `0x432130`.

Instruction witnesses: [mission path](../evidence/engine/2026-10-22-mission-loading-witness.txt)
and [job anchors](../evidence/engine/2026-10-22-stage-job-witness.txt).
Details, fields and diagrams: [stage loading](../engine/subsystems/mission-and-stage-loading.md)
and [execution](../engine/subsystems/execution-and-scripts.md).

The full submitted callback is not in the existing direct-callee export set.
Its bounded disassembly establishes initial enumeration/filtering only; full job
execution, dispatch, release and mission completion writers remain local work.
No game process was executed and no runtime timing is asserted.

## External provenance

Searched release/publisher/engine references and followed primary sources:
Steam, publisher launch and development posts, CEDEC's original developer session,
the original website and archived staff blogs. Summaries, dates, URLs and access
limits are recorded in [engine provenance](../sources/engine-provenance.md).

The PC date is April 25, 2018. The publisher describes Japanese-build lineage and
PC changes. No inspected primary source establishes an overall engine name.
CEDiL slides require login and were not read; archive access was intermittent.
These limits do not stop native mapping.

Saved the reproducible binary identifier scan as `tools/engine_provenance.py`.
It reads the research copy, hashes it and writes seven source-path strings and
69 keyword matches to `evidence/engine/binary-provenance-strings.json`.
Lua 5.0.2 identification and PhysX imports are evidence about components;
the game-specific Cavia/witch core remains an explicitly labeled inference.

## Verification scope

Documentation relocation/link checks are saved in
[documentation check](../evidence/engine/documentation-check.json). The source
copy hash matches the established analysis image. The prior 231-source compiler
verification remains the script baseline; documentation changes do not require
repeating it. Mermaid relationships were reviewed against their nearby evidence
and uncertainty labels; no graphical rendering is claimed in this checkpoint.


## CEDEC download follow-up

The user supplied `/Users/andyhunter/Downloads/r35.pdf`. Preserved a byte-identical
copy in `sources/cedec-2007/r35.pdf`. All six slides were extracted and visually
reviewed. The reproducible Poppler inspection, hash and diagnostics are recorded
in `evidence/cedec-2007/inspection.json`; the tool is
`tools/inspect_cedec_2007.py`. Poppler warned about a default configuration but
returned success and rendered every page legibly.

Page 2 explicitly lists motivation for an in-house-developed engine. This is
primary-source support for the Cavia-core inference, without a product name or
technical architecture. The earlier access blocker is now closed. Updated the
source catalog, architecture, worklist and questions; retained earlier log entries
as history. [Full review](../sources/cedec-2007/README.md).


## Direct developer interview — correction to engine classification

The user correctly challenged the inference-only framing. Followed the original
release-era references and located Play's direct Bullet Witch team interview,
whose first answer explicitly confirms from-scratch engine development. The
August 4, 2006 official programmer blog also identifies NovodeX physics and AI
scripting work. The official TGS 2006 list supplies the July 27, 2006 release date.
Saved findings, links, a short quote and concrete technical leads in
[engine history](../sources/cavia-engine-history.md). Updated the brief,
architecture, provenance, worklist and questions to treat Cavia's in-house engine
as established. Earlier qualified log entries are superseded by this evidence.


## Pass 06 — architectural coverage complete

Connected the remaining AI/physics/service/render/UI domains and closed the
RAT, named shader registry and concrete startup paths. Architecture revision 7
and the brief now distinguish completed architectural coverage from remaining
contract reconstruction and runtime validation.

[Detailed checkpoints](2026-10-22-engine-pass-06.md) ·
[Report and reproducible tooling](../reports/2026-10-22-engine-documentation-pass-06.md).

## Direction decision — source reconstruction

The user selected source reconstruction as the route: readable, buildable engine
source, then a direct port and later upgrades. This supersedes the hybrid
translation recommendation in the preceding strategy discussion. Recorded in
BRIEF.md, architecture revision 8 and engine/SOURCE-RECONSTRUCTION.md; updated
current worklist, open questions and research navigation together.

First milestone defined: a buildable resource/data foundation using the mapped
RAT/CPR contracts and existing corpus evidence. Later increments connect startup,
Lua/tasks/UI, a playable mission and full behavior before improvements. Native
implementation has not begun in this decision-recording turn. No game files changed.

## Dedicated repository setup

Moved the project to `/Users/andyhunter/repositories/bullet-witch`, retaining an
old-path symlink for historical analysis references. Promoted the reconstruction
plan to root IMPLEMENTATION.md, configured explicit Git exclusions and checked
staged content. Added and tested local commit/push content guards. Details:
[repository setup](../reports/2026-10-22-repository-setup.md).
