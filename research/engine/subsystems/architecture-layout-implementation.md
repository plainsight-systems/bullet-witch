# Architecture layout implementation

The revised whole-source architecture was approved on October 26, 2026 after the
user rejected the earlier mechanical game grouping. This implementation adapts
that uncommitted tree directly; the rejected intermediate layout is not a commit.
The [approved proposal](architecture-layout-proposal.md) records the ownership
trace and source/reference basis. This is an ownership refactor, not a claim of
token-equivalent file moves.

## Implemented boundaries

- Engine mechanisms live under `src/engine`, original game policy under `src/game`,
  native implementations under `src/platform`, and preview tooling under
  `src/developer`. Singular portable engine contracts remain under `include/bw`.
- Concrete application composition and its process loop are game-owned. World
  simulation and presentation are grouped together, while the authoritative
  world/frame clocks remain engine timing. No clock authority was added.
- [Neutral input](neutral-input-boundary.md) separates device sampling from saved
  bindings, fixed menu actions and repeats. One application mapper handles both
  ordinary and loading pumps. Its wall and presentation intervals stay distinct.
- [Developer compilation](developer-compilation-boundary.md) removes concrete
  preview owners from production. A bounded command/const-observation capability
  adapts existing actions only in the enabled build. Ordinary feedback is owned
  by simulation. No new tool UI or activation gesture is introduced.
- Alicia raised lifetime/facing lives with actions; aim target/blending and the
  support-hand adapter live with animation. The common character publication
  adapter requires an explicit semantics policy, supplied by Alicia at setup.
  Unknown semantics stay unknown. Alias identities and numerical algorithms are
  retained, with one prepared byte per clip and no new frame allocation.
- Original loader/interpretation code lives under `game/content/original`.
  Schema decoding and residency stay engine-owned. The checked binary reader is
  an explicit portable importer primitive; game code no longer reaches into a
  private engine source directory. `/assets/` remains ignored, covering optional
  local `original/` and `overrides/`; no data or resolver was added.
- Engine spatial, lighting and render build owners are distinct. Profile contains
  permanent state/bindings, with UI and effective mission policy removed from its
  build membership. Game application/playback/presentation targets have enduring
  names. Native shaders are built as `bullet_witch.metallib`, and `./run` launches
  the `bullet-witch` executable. Existing fullscreen/capture behavior is retained.

## Build and dependency enforcement

Private `src` include roots no longer propagate through target interfaces. CMake
rejects any authored target exporting that root. `scripts/check_architecture.py`
resolves authored includes and enforces portable-header, engine, game, native and
common-character dependency directions. Native main is the intentional concrete
composition exception. The shared reader replaces all game-to-private-engine
includes rather than allowlisting them.

Developer implementations form an enabled-only object target, preventing a
reverse static-library link into game. The lint inventory checks the actual
configuration: OFF rejects developer compilation entries; ON requires all of
those translation units. Complete gates must run in both configurations, so this
is compilation exclusion rather than suppression of diagnostics.

The [source map](architecture-layout-file-map.json) covers every existing source,
header and shader including split outputs. The
[test/tool map](architecture-test-tool-file-map.json) records all relocated test
and inspector files. Scenario inventory and six CTest group identities are
retained. Tests and fixtures are owner-grouped without adding path-only tests.

## Verification completed

Both developer-disabled and developer-enabled Debug configurations passed
`python3 scripts/check.py all`: formatting, strict compilation, architecture
include-direction checks, all six CTest groups, fatal clang-tidy and repository
content policy. The disabled configuration analyzed 243 compilation commands;
the enabled configuration analyzed 246, including every developer implementation.
These are compilation commands, not counts of tests. Existing native tests include
in-process Metal texture, scene replay, edge sampling and compositor checks, plus
Cocoa/controller input contracts. No visible game window was launched.

The purchased-content `startup-corpus` passed in both configurations. Disabled
mode retains ordinary weapon rollback/switch/fire and verifies inert preview
hooks; enabled mode additionally traverses existing preview admission, layers and
action phases. Both retain original Lua startup/menu traversal and end with zero
migrated CPU payload after teardown. These use controlled media/output fixtures;
they are not evidence of visual parity with the original game.

The final unstripped developer-disabled executable **and game archive** contain
none of the checked concrete preview/flow/access/factory symbols or preview labels.
The disabled compile database has no developer translation units. Enabled game
artifacts contain preview/capability symbols, and enabled runtime tests contain
the review-flow implementation. Release plus developer activation is rejected.
The final working build is developer-disabled.

The real top-level launcher succeeded with `--build-only` from outside the
repository, selecting that ordinary configuration and the renamed executable.
All 385 source/header/shader paths are covered by the implemented map; every
current documentation file link resolves. The 16 lighting/timing/spatial/native
shader implementation files compare identically after excluding includes,
comments and whitespace. This scope check supplements, rather than replaces,
the actual native/contract checks.

The user accepted this implementation on October 26, 2026 after reviewing the
lighting ownership walkthrough and explicitly requested the scoped commit. This
records acceptance, not an additional observed native playthrough. No push was
authorized.
The two reported UI issues remain separate backlog items; no gameplay, physics,
lighting tuning, new debug UX or asset replacement runtime entered this slice.
Generated commands/logs/maps used during verification remain under ignored
`research/evidence/reconstruction/game-grouping/`.

Design consultation retrieved Core SF.11 (`rs-contained`) and performance COPY.7
(`eliminate-hidden-copies`); implementation consultation retrieved Core I.1
(`ri-explicit`) and performance CDSA.32
(`transform-static-data-into-the-consumer-layout`). The actor split keeps explicit
self-contained policy inputs and load-time immutable metadata. Input/developer
reports record their separate full design and implementation consultations.
