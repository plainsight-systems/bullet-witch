# Engineering tools

Run from the repository root with Python 3.9+, CMake 3.20+ and a C++20 compiler.
CMake uses the host's default generator; Ninja is not required. BUILD_TESTING
requires Python for the CLI integration tests.

## Formatting and static analysis

Both clang-format and clang-tidy are pinned to **21.1.8**, matching the imported
DoLittle macOS tooling. The formatter uses Google style. Tool discovery checks
`.venv-tools/bin`, versioned/unversioned PATH entries, and Homebrew LLVM prefixes
on macOS. `CLANG_FORMAT` and `CLANG_TIDY` explicitly select executable paths;
a requested tool with the wrong version fails instead of silently substituting.
Provide these executables from a matching LLVM installation. No check downloads
or installs tools. Compiler version and analyzer version are independent.

```sh
python3 scripts/check.py format --fix
python3 scripts/check.py all
```

`all` runs formatting, strict Debug compilation, CTest, clang-tidy and repository
policy checks. Individual actions are `format`, `host`, `tidy`, `policy`.
`--jobs N` bounds build/analyzer concurrency; `--build-dir build/NAME` selects an
ignored build directory. No source formatter or analyzer scans recovered game
code under `research/`. All authored project C++ translation units must appear
in the compilation database or analysis fails. Headers are checked through their
includers. A newly added standalone public header should have a compile consumer.

`tidy` preserves compilation arguments, uses an isolated database, and on macOS
adds the selected Xcode SDK/libc++ headers for the separately installed analyzer.
Every enabled finding is fatal. Per-file logs and a summary stay under ignored
`research/evidence/workflow/clang-tidy/`. LLVM 21.1.8 on native macOS is verified;
other hosts are not established by these results. CUDA analysis is not needed
by this CPU-only foundation and has not been imported as a false capability.

The editor settings use `scripts/clang-format`, which enforces the same version
and supports stdin formatting. A missing formatter reports an error; editor
formatting does not replace `all`.

## Presets, corpus and sanitizers

```sh
cmake --preset host-debug
cmake --build --preset host-debug
ctest --preset host-debug
cmake --preset host-ubsan
cmake --build --preset host-ubsan
ctest --preset host-ubsan
python3 scripts/check.py corpus --game /path/to/read-only/installation
```

`corpus` builds/tests the host inspector, then compares all local RAT/CPR files
with the independent research readers. It needs the local inputs/reference
artifacts documented in `docs/resources.md`; it is intentionally excluded from
`all`, which works without the game. Outputs remain under organized `research/`.

UBSan is a separate preset so ordinary runs do not silently turn sanitizers on
or off in another build. AddressSanitizer is supported through `BW_SANITIZERS`
but currently hangs during runtime startup on the research Mac. Do not describe
that as a passing test. CMake supports MSVC warning flags, but Windows has not
been verified. No hosted CI is configured.

## Repository safeguards

Enable the versioned hooks with `git config core.hooksPath .githooks`. They
check staged content at commit and reachable history at push for forbidden
assets, binary data, links, oversized files and ignored evidence. The full
`policy` action also checks the proposed working-tree snapshot using a temporary
index and object directory; it leaves the real index untouched. Run `all`
before a commit, and keep its validated working tree consistent with the commit.

The runtime build now fetches Lua 5.0.2 from lua.org with a pinned SHA256 into
`build/.../_deps`. Initial configuration needs network access or CMake's
`FETCHCONTENT_SOURCE_DIR_LUA502` pointing to that exact source release. The
tracked compatibility patch validates expected source fragments. No tools or
assets are installed. CMake 3.24+ and C99/C++20 compilers are required. See
[the startup verification commands](../docs/startup-runtime.md).
