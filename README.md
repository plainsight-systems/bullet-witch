# Bullet Witch

A remaster of Bullet Witch, written as new source, with Unreal Engine's
architecture as the guide. Purchased original game data is required and is
never committed. There is no runnable game yet: the tree holds the headless
engine loop and the memory and math foundations.

Start with [what we are building](docs/architecture/overview.md), then the
[source layout](docs/architecture/source-layout.md).

## Build

CMake 3.25 or newer, a C++20 compiler, and `clang-format` / `clang-tidy`
21.1.8 on PATH or in the Homebrew LLVM location. Configuration rejects other
tool versions and downloads nothing.

```sh
cmake -S . -B build-game-loop -DCMAKE_BUILD_TYPE=Debug
cmake --build build-game-loop --parallel
ctest --test-dir build-game-loop --output-on-failure
cmake --build build-game-loop --target format-check
./build-game-loop/engine_host --seconds 0.1
```

The host runs the engine lifecycle with no world, window or game data;
`--seconds` bounds the run.

## Licensing

The [split license](LICENSE), [code license](LICENSE-CODE),
[content license](LICENSE-CONTENT) and [NOTICE](NOTICE) are preserved
verbatim. The NOTICE describes the prior project's dependencies, not this
tree's. Unreal is a reference, not bundled source or a linked dependency.
