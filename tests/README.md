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
  composition with deterministic screen services. No rendering or native-game
  timing equivalence is claimed.

The corpus workflow remains an integration scenario even though it lives under
research and is excluded from the default local check. Do not count CTest's three
registered commands as two tests or the reader's assertions as separate tests.
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
