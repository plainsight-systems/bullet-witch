# Lighting redesign verification

## User acceptance — October 25, 2026

The user accepted the current lighting implementation and authorized its commit.
Residual angle-dependent speckling and coarse shadows are accepted baseline
limitations. They may be consistent with limitations of the original renderer,
but no matched original-image correspondence was established. The explicit
native final-normal normalization and pixel-footprint specular filtering remain
documented departures from the recovered equation, not claims of original
behavior.

The latest reproduction references are the user's October 25 screenshots at
7:18:15 a.m. (cave) and 7:19:52 a.m. (outdoor school outfit). They preserve the
viewing contexts for the remaining angle-dependent appearance; the screenshots
and extracted data remain outside Git. No new visual investigation is required
for this accepted slice. Residual visual quality belongs to the later upgrade
pass unless a concrete new implementation defect is demonstrated.

Acceptance closes this represented lighting slice, not the named upstream
mission/event/weather/particle/settings producers or every original-device
evidence limit. The independent implementation review and passing verification
remain distinct from this user decision. Production code is unchanged since
the final passing full gate; this acceptance update is documentation only.

The redesign is uncommitted. This report separates implemented contracts,
original supplied-input comparisons, reconstructed native execution and original
live-frame evidence. None of the first three establishes the fourth.

## Implemented boundaries

- Exact effective material admission and independent normal, directional, local,
  reflection, shadow-reception and alpha dependencies; corrected three-influence
  deformation shared by color/preparation/shadow consumers.
- Terrain, actor and placed receiver ownership; strict five-point versus blended
  trilinear field sampling; independent per-instance caches, local-light overflow
  SH and receiver-position reflection lookup with authored binding precedence.
- Source control-point-zero placed receiver origins, weighted direct world poses
  and the original first-placement-transform fallback for absent controls.
- Region far-plane/clear publication; source selection camera, projected-size and
  owner-bound caster admission; independent body/weapon flags, setup retention,
  cull/bias and integer-center raster correspondence.
- Shared-owner S/T preparation ordering, distinct masked S/T alpha contracts,
  Main/Trans owner-opacity sections and explicit per-instance updates.
- Output clear and history lifetime, tone toggle/reset/scene release distinctions,
  and the original optional final edge filter with its BGRA8 tone intermediate.

The [acceptance ledger](lighting-redesign-acceptance.md) carries all 51 audit rows.
The individual linked reports retain exact source functions, supplied-input
witnesses, native cases and reference-derived limitations.

## Verification evidence

Original executable/bytecode comparisons cover material and vertex equations,
coverage, strict field sampling, shadow selection volumes and callbacks, control
point lookup, owner-opacity publication and the final spatial filter. Input
provenance matters: the 81 strict-field coefficients were compared at three
supplied authored matrix translations, not claimed as recovered live receiver
positions. The separate control-point witness establishes that producer's rules.

The existing native memory scenario exercises retained resource/batch rollback,
receiver and caster instance isolation, reflection precedence, preparation/color
survival and ordering, raster edges, source shadow state, owner-opacity sections,
output temporal lifetimes and final-filter pixels. It uses authored fixtures on
the production Metal paths without opening a game window.

The full original-root corpus executable completed the reconstructed load,
4,100 selected static/sky draws with no omitted selected-world draws, live action
and weapon/costume flows and zero tracked CPU memory after teardown. It remains a
controlled rendering fixture. Its observer now builds a destination index once
at draw publication instead of scanning every unrelated draw for each batch
update. No expected outcome changed. Corpus diagnostics go to stderr so stdout
remains the promised machine-readable JSON result.

The wrapper now accepts an explicit finite positive `--timeout` and records the
bound and elapsed time; the existing 30-second default is preserved. The complete
provisioned scenario is run with `--timeout 300`, without removing actions or
relaxing behavioral assertions. Commands and generated output stay under ignored
`research/evidence/reconstruction/lighting-source-fidelity/`.

The pre-mip hash-bound original-root corpus report **passed**, completing in
120.34 seconds with the explicit 300-second bound. This timing describes the
controlled acceptance corpus, not native game performance. Its recorded binary
SHA256 is `52c17534f74c20b1079f22f57e7d747b64f6b01b9aaf2e0f51ecc730e101dee6`;
the original root chunk remains
`42c4bebad62be29a4b514784fcf6b2584f05949d6b4c905566e147e664486dd4`.

The final material-witness ABI refresh **passes**: 3,160 pixel cases with maximum
error 1.49e-7, 420 T and 600 S/shadow vertex cases with maximum error 2.98e-7,
and 24 exact coverage cases. The new trailing alpha-reference field was appended
to witness inputs; hashes verify all other inputs and expected outputs stayed
unchanged. No tolerance was relaxed.

The pre-mip combined `python3 scripts/check.py all` **passed**: pinned formatting,
strict host compilation, all six standard CTest suites, fatal clang-tidy across
181 translation units, and repository content policy. The policy inspected 647
file versions and admitted only text; ignored evidence and original data remain
outside the proposed tracked snapshot. No diagnostics were suppressed.

## Historical pre-walkthrough boundaries

Before the later accepted walkthrough, reconstructed PW0/PW5 and scene/prop/sky
frames still needed user review. The announced native launch failed before presenting a scene with
the previously recorded fullscreen delegate rejection (`rejected=1 pending=0
entered=0 active=0 key=0`). Its ignored `live-game.log` records the failed launch;
no current scene visual check is claimed from it. No matched original live
pose/material/frame/history capture exists;
original first luminance bytes, actual physical display transfer and original
D3D precision/unsupported address behavior remain explicit evidence limits.

The original launch-enabled mip-generation option is off in both original
defaults and the inspected installed settings. Its enabled full-chain,
base-surface-per-level BOX/ordered-dither path is now implemented across world
and front-end DDS resources; see [texture mip generation](lighting-texture-mip-generation.md). Missing original
settings UI/persistence, mission/entity/event startup, AREA overrides, storm and
particle/effect producers and active optional screen-effect controllers are real
implementation dependencies. They are not replaced by a global ambient term or
closed by a callable renderer interface.

The completed native Main/Trans and final edge algorithms do not create their
missing runtime fade/settings producers. The user authorized graphics mip
completion and independent review without adding gameplay/weather producers.
Those named upstream capabilities remain open; the whole engine audit is not
closed by current-scene acceptance. No commit or push is authorized by this
verification report.

## Graphics mip completion and ownership review

The graphics extension captures `--texture-mips on|off` once, default off, in
mission preparation and the native DDS sprite host. It covers world, sky, actor,
weapon, costume, title, menu, loading and font resources. Source metadata stays
attached through base conversion; raw icons and movie frames use separate
constructors. Every lower level reads the complete original base. Native
allocation/upload performs no filtering, accounts every level/face and publishes
only a fully uploaded candidate. Borrowed input, injected allocator ownership,
move-only native retention and GPU-retirement obligations remain explicit.

Independent review found two production design gaps and one evidence-tool gap.
The renderer's broad capability is now split into resource residency and scene
publication with actual borrower types narrowed and negative compile-time
checks. Front-end DDS construction now follows the same source option and
preserves the active four-bit font and odd-size base conversions. The ignored
numerical driver now rejects missing records or truncated payloads before
comparison. See [the independent review](lighting-independent-review.md).

The original DLL comparison covers 189 bases / 517 generated face-levels and
98,632 components with zero differences. The independent reviewer replayed the
full comparison, plus all 256 base values into four-bit staging and three
nonzero-sentinel padding cases. Native headless tests exercise rectangular
chains, six cube faces, HALF bytes, full accounting, incomplete-payload rejection,
invalid counts, budget rollback, move ownership and failed world publication.
The reviewer independently reran all six CTest suites successfully. The final
default-off corpus passes in 123.00 seconds against binary SHA256
`1bda84e1a4c19559e302417c7983585ad18e6c7a4e9168d8ba73855d32c34185`.
The enabled corpus also passes, in 185.91 seconds with the same binary and
an explicit 600-second bound. Both retain the original scenario, selected
content and teardown assertions; these elapsed times are controlled corpus
execution, not native loading or frame-rate measurements. The wrapper hashes the binary before
launch and rejects any change by completion. An overlapping earlier run was
discarded after a debug rebuild; no result from it is used as final evidence.

The final graphics `python3 scripts/check.py all` **passes**: 334 files checked
with the pinned formatter, strict host compilation, all six CTest suites, all
184 fatal clang-tidy commands and content policy over 653 text file versions.
No check family or diagnostic was disabled. Fixture-only integer-width findings
were corrected; preceding failed logs remain ignored evidence. The final log is
`research/evidence/reconstruction/lighting-source-fidelity/mip-all-accepted.log`.
Here “accepted” identifies the automated gate log, not user visual acceptance.
Final off/on reports are in adjacent `mip-corpus-final-off` and
`mip-corpus-final-on` directories. This was the mip milestone gate; the subsequent surface repair has its own
final gate in `lighting-surface-filtering.md`.

The independent review is **complete with no unresolved actionable findings**
in the entire uncommitted redesign plus the graphics mip path. It covers all
51 audit boundaries, independently replays original numerical/base conversion
evidence, verifies both final corpus manifests against the current source and
binary, and reruns all six suites against that final build. R1/R2/R3 are resolved;
the final report retains visual, original-device and upstream-owner limits.
A final documentation-only content-policy check also passes. Nothing was
committed or pushed, and no visible game launch was performed in this pass.
