# SRC-01: resource/data foundation

October 22, 2026. Production implementation: `src/resources.cpp` and
`include/bw/resources.hpp`. Original game installation remains read-only.

## Implementation basis

RAT native witnesses: 0x41b880 / 0x41b820 / 0x41b920 (relocation),
0x41ba60 (dotted lookup), 0x41b7c0 (full name), 0x414990 (priority),
0x415230 (dependencies), 0x414a10 (physical path).
CPR witness: 0x41b4a0. See
[resource dispatch](../engine/subsystems/resource-dispatch.md) for exports and
[implementation plan](../../IMPLEMENTATION.md) for scope.

Preflight over the installed corpus found 8,235 namespace nodes, 12,706
indexed and 856 named dependency references, 2,074 CPRs with a second payload
section, 45 inline references, and 7,985 records with arguments. All 6,625 CPRs
have version 1 and zero compression fields at +0x28/+0x30. All RAT namespace
memberships and parent links were consistent. Raw preflight results remain in
`research/evidence/reconstruction/src-01/format-preflight.json` (ignored).

Implementation uses owned immutable disk bytes and checked ranges; it does not
relocate offsets into host pointers. Equal-priority winning catalog conflicts
are explicit ambiguity because the original qsort comparator leaves tie order
unresolved. Dependency references are reported in original order; local indexes
are converted to names, consistent with the original global request path.

## Implementation and corrections

Built `bw_resources`, `bw-resource`, synthetic C++ tests and Python CLI tests.
The first whole-corpus run rejected 2,609 files because an overly strict auxiliary
stride check assumed every argument area length was divisible by 12. The native
loader uses integer division and accepts trailing padding. Corrected the reader,
added a padding regression case, and validated all complete argument triples,
including those not referenced by a record.

The startup package is named `script.root.dpk`, not the initially assumed
`script.root`. CLI lookup now demonstrates the real catalog/publication identity:
stable resource 12935, `script/root.cpr`, record 0 (`USER`, `root.dpk`), offset 192,
length 4363. JSON output is retained locally as `resolve-script-root.json`.

## Verification

- AppleClang 17.0.0 native Debug build and CTest passed.
- Homebrew Clang 21.1.8 UBSan build: 555 C++ checks and four Python CLI tests
  passed. Tests use synthetic bytes only; truncations, bad ranges/counts,
  namespaces, dependency indices, publication identity, padding, ownership,
  priorities, unknown handlers, exit codes and symlink containment are exercised.
- `verify_resource_foundation.py` runs the native inspector under
  `UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`, then compares against the
  pre-existing input hashes/inventories and Python CPR reader. All 6,625 CPRs /
  29,996 records and 25 RATs / 15,262 resources matched. Counts include 8,235
  namespace nodes, 13,562 dependencies, 26,364 record arguments, 45 inline
  references and 15,254 publication names. All RAT references cover exactly the
  installed CPR corpus. Zero comparison errors.
- Source/binary/reference hashes and final counts are retained in the reviewed
  [verification summary](../evidence/reconstruction/src-01/verification-summary.json).
  Full native JSONL output, stderr and process samples remain ignored under that
  directory. No research output was written into the game installation.

Commands and contracts: [module documentation](../../docs/resources.md).

## Tooling limitation

AddressSanitizer hung before `main` using both AppleClang 17 and Homebrew Clang 21.
Process samples show ASan initialization reentering allocation through dyld cache
inspection and spinning in `StaticSpinMutex`. Both test processes were stopped;
ASan is not counted as passing. Samples are retained as
`asan-startup-sample.txt` and `llvm21-asan-startup-sample.txt`. UBSan and normal
native execution run successfully. CTest now bounds unit-test runtime to 30 seconds.

## Repository checks

Both final native and UBSan builds pass CTest (555 C++ checks and four CLI cases).
The five repository-policy tests also pass after adding the explicit exception
for the small verification summary. Raw inventory, game bytes and process samples
remain ignored; an isolated-index check covers the proposed source/documentation
snapshot without changing the user's real Git index. No commit or push is made.

## Handoff

SRC-01 is complete for container inspection, lookup and metadata decoding.
GPU/USER/CALL runtime handlers, publication/release, request scheduling, DPK member
access and Lua execution remain SRC-02 work. Inline references retain the prior
reader's extraction formula; their precise runtime semantics are still provisional.
No claim of original-engine runtime equivalence, a running game, or Xbox testing.
