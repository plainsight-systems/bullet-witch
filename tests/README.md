# Verification inventory

`resources_test.cpp` is the in-process resource contract suite. Its printed
check count is assertions executed, including a loop over truncated byte lengths;
it is not a count of independent tests. It uses synthetic in-memory bytes.

Integration scenarios (eleven, under the revised startup-slice adoption
exception; the target is five repository-wide):

- Four `resource_cli_test.py` methods exercise executable/file interactions.
- Five `test_repository_policy.py` methods exercise disposable Git repositories,
  index/history semantics and content rejection.
- One `research/tools/verify_resource_foundation.py` corpus workflow compares the
  C++ inspector with independent research evidence over the installed archives.

- One `tests/startup_corpus.cpp` / `research/tools/verify_startup_runtime.py`
  provisioned scenario runs the hash-bound original root through resource/task/Lua
  composition with deterministic screen services and the concrete startup session
  using controlled media observations, original title/menu/costume assets and a
  synthetic renderer. The production route now performs real profile I/O in an
  isolated temporary directory, selects costume 5, validates the original Lua
  new-game handoff and reloads the persisted profile. `bw-startup
  --verify-startup` still exercises only the four-movie natural title route.
  Native manual costume 1 preview/selection and gameplay handoff are recorded
  separately; neither route establishes original-game timing equivalence or
  world execution.

The corpus workflow remains an integration scenario even though it lives under
research and is excluded from the default local check. Do not count CTest's six
registered commands as individual behavioral cases or the reader's assertions as separate tests.
Add no integration scenarios until this inventory is reduced or explicitly
rebudgeted. New parser boundary cases belong in the in-process suite.

`python3 scripts/check.py all` runs the in-process suite, four CLI methods and
five policy methods along with format/build/analysis checks. `corpus --game PATH`
runs the separately provisioned real-file comparison. No fixture copies of
commercial game assets are committed.

The resource contract executable also checks bounded DPK members and startup
USER lifecycle with an in-memory file capability. The existing real-archive
corpus scenario now compares recognized DPK metadata with the independent
Python reader; this extends the same container-validation flow.

`runtime_test.cpp` exercises isolated context and scheduler contracts directly in
process. `startup-corpus` is built by default but only runs when explicitly
provisioned with the purchased game. It is not registered as a unit test. The
new integration scenario is an explicit budget exception for the authorized
startup slice; the five-scenario target and reconciliation debt remain.

`startup_services_test.cpp` exercises in-process session lifecycle, presentation
and gameplay-request contracts.
`startup_playback_test.cpp` exercises generic player timing, final presentation,
continuous PCM/backpressure, late audio failure, long video tails and the thin
game fade adapter. `pcm_buffer_test.cpp` adds three in-process cases for
timestamp-segment wrap, partial reads/descriptor pressure and concurrent SPSC
handoff. Their assertions are not
separate test cases. Decoder reference probes and native run diagnostics stay
under ignored `research/evidence/reconstruction/native-startup/`; they are not
additional unit suites or asset fixtures.

`image-font-tests` adds two in-process contract groups for bounded PNG decoding
and original FONT metadata, including signed bearings and packed channel flags.
`advertise_flow_test.cpp` and `title_flow_test.cpp` exercise recovered gates,
fallback timers, animation and input outcomes. The session suite additionally
checks title readiness and lifetime, and the fade adapter checks zero-duration
snap on its next update. These extend in-process coverage; the integration
scenario inventory remains eleven.

`profile-tests` is an in-process CTest entry. Its tests use synthetic save
buffers and a controlled storage capability to verify exact-size rejection,
unknown-byte preservation, defaults, dirty-file error handling, checkpoint bounds,
and the recovered Steam costume/extra availability refresh. `ProfileLoadFlow`
checks readiness/fades and retry versus explicit continuation without saving.

`menu_flow_test.cpp` adds four in-process behavioral cases to the existing
runtime suite: wrapping/skipping and new-game selection; absent Continue data
and return-to-title; options/costume/campaign-stage navigation; and the exact
Steam availability refresh with retained compatibility IDs. The profile tests
cover persistence errors; the production front-end/session test covers request
validation and ownership after profile changes.
These are behavioral cases, not a count of assertions or separate integration
scenarios. The actual profile-file round trip belongs to the existing provisioned
original-root scenario, not these unit tests.

Current acceptance does not include audible menu music/cues, output gain from
volume settings, a Steam achievement provider, extra-mission launch routing,
world rendering or gameplay. Native costume preview evidence concerns authored
2D sprite textures, not model rendering. Generated logs and snapshots remain in
ignored organized research evidence.

`memory-tests` adds four common in-process contract groups: zeroed/aligned
allocation, rejection/rollback, retained allocator/container ownership, and
concurrent allocation with cross-thread release. On Mac the same executable
also exercises native aligned backing and invalid-request handling without files,
subprocesses or UI. Resource tests add nested allocator propagation, lease survival
after service destruction and bounded rejection. Counts printed by those suites
remain assertions, not individual tests.

The existing provisioned original-root scenario now injects the selected Mac
CPU provider and verifies that resource-domain live payload/backing returns to
zero after teardown. It reports peak requested storage for that migrated domain;
this is not total process residency, a gameplay peak or GPU accounting. Both
debug and UBSan runs cover this path. No additional integration scenario was
introduced by the memory migration.

The memory executable also exercises portable device admission/reconciliation,
rollback and moved/retained tokens. On Mac it checks real Metal texture upload,
reported allocation bytes and completion before retirement without a window.
These are in-process capability checks, not a rendered-startup acceptance run.
`lua_memory_test.cpp` and `task_memory_test.cpp` extend the existing runtime
suite with bounded VM bootstrap/exhaustion/reallocation and retained task
snapshot/failure recovery. Image/font and media cases cover copied/retained output
lifetime and rejected allocation. The provisioned original-root scenario now
covers catalogs, bounded Lua, task and decoded presentation CPU ownership and
reports the 64 KiB root heap separately from aggregate migrated-domain storage.
The CTest entry count remains six; assertions are not individual tests.

Memory concurrency cases use a semaphore-gated provider to pause native acquire
or free while other domain calls progress. They verify provider snapshot access,
payload and backing admission against pending reservations, isolated failure
rollback and budget retention through release. No timing-based throughput claim
is made; these are public correctness contracts within the existing memory suite.
