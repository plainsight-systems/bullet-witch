# Bullet Witch research

Start with the [engine architecture](engine/ARCHITECTURE.md), the main technical
entry point for the confirmed [project brief](../BRIEF.md). Script recovery is
complete; the major engine architecture is mapped, with bounded contract work
and runtime validation tracked explicitly. **Source reconstruction is selected**;
follow the [implementation plan](../IMPLEMENTATION.md) for the next phase.
SRC-01 now has a [buildable resource module](../docs/resources.md) and
[verification work log](logs/2026-10-22-source-reconstruction-01.md).
The [reconstruction architecture](../docs/architecture/README.md) is the working
structural baseline, organized around the finished engine. It lives in production documentation; this research index and
the original-engine map continue to own evidence and recovered behavior.
Engineering checks are documented in the [tooling guide](../scripts/README.md).
Their local diagnostics and adoption records live under `evidence/workflow/`;
corpus comparisons remain under `evidence/reconstruction/`. Both output trees
are ignored except for explicitly reviewed summary files.
Updated October 22, 2026.

## Organization

```text
research/
  README.md                 Navigation and recording rules
  engine/                   Living architecture, worklist and open questions
    subsystems/             Detailed maps and editable diagrams
  sources/                  External source summaries and provenance
  reports/                  Dated investigation reports
  logs/                     Incremental work records and corrections
  evidence/                 Machine-readable results and instruction witnesses
  extracted/                Recovered scripts and copied resource samples
  experiments/              Analysis workspaces and experimental outputs
  tools/                    Reproducible research scripts
  vendor/                   Local analysis tools and dependencies
```

Keep this index as the only document at the research root. Put current technical
knowledge in `engine/`, chronological findings in `reports/` or `logs/`, and
external references in `sources/`. Keep diagrams beside the claims they explain.
Generated corpora retain their internal indexes and stable paths.

## Current map and evidence

- [Architecture](engine/ARCHITECTURE.md): subsystem inventory and relationship maps.
- [Bootstrap/tasks](engine/subsystems/bootstrap-and-tasks.md),
  [mission lifecycle](engine/subsystems/mission-lifecycle.md),
  [background worker](engine/subsystems/background-resource-worker.md) and
  [resource dispatch](engine/subsystems/resource-dispatch.md): native system connections.
- [Execution and scripts](engine/subsystems/execution-and-scripts.md): contexts,
  coroutine scheduling and screen-task interfaces.
- [Mission and stage loading](engine/subsystems/mission-and-stage-loading.md):
  mission selection, stage requests, job submission and readiness polling.
- [Actors and animation](engine/subsystems/actors-and-animation.md) and
  [particles and effects](engine/subsystems/particles-and-effects.md): factories,
  concrete actor construction, effect packs and connected particle runtime.
- [Actor lifecycle](engine/subsystems/actor-lifecycle.md): frame/state/AI dispatch,
  pose and attachments, collision interfaces, deferred spawning and destruction.
- [Particle runtime contracts](engine/subsystems/particle-runtime-contracts.md):
  curves, emission, children, animation, shader/blend state and lifetime limits.
- [Model data](engine/subsystems/model-data.md): first PC mesh/skeleton map and diagram.
- [AI runtime](engine/subsystems/ai-runtime.md), [action index](evidence/engine/pass-06/ai/ACTION-INDEX.md),
  [physics](engine/subsystems/physics-and-collision.md) and [animation data](engine/subsystems/animation-data.md).
- [Rendering](engine/subsystems/rendering.md), [UI](engine/subsystems/ui-and-screen-tasks.md),
  [input](engine/subsystems/input.md), [audio/movies](engine/subsystems/audio-and-movies.md),
  [saves](engine/subsystems/saves-and-progression.md) and [platform services](engine/subsystems/platform-and-services.md).
- [Pass 06 native evidence index](evidence/engine/pass-06/FUNCTIONS.md): accepted
  pseudocode and instructions, with rejected interior fragments excluded.
- [Community tools](sources/community-reverse-engineering.md) and
  [Softimage developer interview](sources/softimage-bullet-witch.md).
- [Mapping worklist](engine/WORKLIST.md) and [open questions](engine/OPEN-QUESTIONS.md).
- [Engine provenance](sources/engine-provenance.md): publisher history, original
  developer references and middleware evidence.
  [Engine history](sources/cavia-engine-history.md) establishes Cavia's in-house
  engine through the development team's direct statement.
  The [reviewed CEDEC slides](sources/cedec-2007/README.md) include direct discussion
  of in-house engine motivation.
- [Per-script index](evidence/script-corpus/INDEX.md),
  [native API index](evidence/script-corpus/NATIVE-API.md) and
  [native export manifest](evidence/native-decompilation/status.tsv).

## Investigation history

| Report | Subject |
|---|---|
| [Repository setup](reports/2026-10-22-repository-setup.md) | Dedicated sibling repository, selective versioning and tested content guards |
| [Implementation strategy discussion](reports/2026-10-22-implementation-strategy-discussion.md) | Historical hybrid proposal and checked Xbox constraints; subsequent decision selects source reconstruction |
| [Engine documentation pass 06](reports/2026-10-22-engine-documentation-pass-06.md) | Major subsystem coverage, AI/action schemas, resources, physics, services, rendering and UI |
| [Engine documentation pass 05](reports/2026-10-22-engine-documentation-pass-05.md) | Selected actor lifecycle, AI connection, animation ordering and retirement |
| [Engine documentation pass 04](reports/2026-10-22-engine-documentation-pass-04.md) | Particle runtime contracts and return to concrete actor factories |
| [Engine documentation pass 03](reports/2026-10-22-engine-documentation-pass-03.md) | Actor construction and particle/effect architecture |
| [Engine documentation pass 02](reports/2026-10-22-engine-documentation-pass-02.md) | Bootstrap, tasks, mission lifecycle, queue and resource handlers |
| [Cavia community research](reports/2026-10-22-cavia-community-research.md) | Existing importers, historical pipeline and concrete PC layout comparison |
| [Steam inspection](reports/2026-10-21-steam-inspection.md) | Acquisition, layout and executable dependencies |
| [Asset layout](reports/2026-10-21-asset-layout.md) | CPR/RAT inventory, textures, packages and shader candidates |
| [Content inventory](reports/2026-10-21-content-inventory.md) | Costumes, bonus missions and model containers |
| [Startup scripts](reports/2026-10-21-startup-scripts.md) | Initial Lua startup flow and native boundaries |
| [Mac next steps](reports/2026-10-21-mac-next-steps.md) | Historical wrapper analysis and tooling proposal |
| [Offline unpacking](reports/2026-10-21-offline-unpacking.md) | Recovered code and preserved source hashes |
| [Native startup](reports/2026-10-21-native-startup-mapping.md) | Root loading, registration, Exec/Wait and screen tasks |
| [Complete script corpus](reports/2026-10-21-script-mapping-and-decompilation.md) | Validated source recovery, AI maps and native exports |

Work records: [script recovery](logs/2026-10-21-script-corpus-worklog.md) and
[engine mapping](logs/2026-10-22-engine-mapping.md), including the
[pass 06 checkpoints](logs/2026-10-22-engine-pass-06.md). Historical reports preserve
the state of knowledge at their date; subsystem notes carry current interpretations.

## Recording rules

Record source builds/hashes, addresses, tools, commands, evidence and unresolved
items as work proceeds. Distinguish verified artifacts, statically established
relationships and inferred behavior. A decompiler's proposed types are not
original source declarations; imports alone do not establish initialization order.

The downloaded game at `/Users/andyhunter/Games/BulletWitch-Steam` is read-only.
All research, tooling, copied resources and derived output belong under this
`research/` tree. Credentials and Steam session metadata do not belong here.
Acquisition tools remain at `/Users/andyhunter/.local/share/bullet-witch-tools/`.
The reorganization does not move the game or downloader.
