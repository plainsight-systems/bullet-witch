# Bullet Witch

![Bullet Witch title screen](https://media.plainsight-systems.com/bullet-witch/title-screen-2026-10-24-0dbf54587d10.png)

Game imagery remains the property of its rights holders and is not covered by
this repository's licenses.

We're rebuilding Bullet Witch because we love it and want to see its vision
fully realized. That means preserving the look, feel, and character that drew
us to the game, while giving its world and systems room to become what the
original hardware couldn't support.

The remaster is new source. How it is built draws on modern engines, talks
and papers; none of them is its specification, and the original game's data
is the evidence for what the game is. Purchased original game data is
required and is never committed.

Start with [what we are building](docs/architecture/overview.md), then the
[source layout](docs/architecture/source-layout.md).

## Build

CMake 3.25 or newer, a C++20 compiler, and `clang-format` / `clang-tidy`
21.1.8 on PATH or in the Homebrew LLVM location. Configuration rejects other
tool versions and downloads nothing. The shader toolchain needs the pinned
submodules, a build of the pinned DXC, and on Apple platforms the Metal
Shader Converter installed under `/usr/local`; see
[third_party/README.md](third_party/README.md). Configuration stops by name
when one is missing.

```sh
git submodule update --init --recommend-shallow
git -C third_party/dxc submodule update --init --recommend-shallow external/SPIRV-Tools external/SPIRV-Headers external/DirectX-Headers
third_party/build-dxc.sh build-dxc
cmake -S . -B build-game-loop -DCMAKE_BUILD_TYPE=Debug -DBW_DXC_PREFIX=build-dxc
cmake --build build-game-loop --parallel
ctest --test-dir build-game-loop --output-on-failure
cmake --build build-game-loop --target format-check
./build-game-loop/engine_host --seconds 0.1
```

`-DBW_SHADER_TOOLCHAIN=OFF` configures without the shader toolchain and its
tests.

The host runs the engine lifecycle with no world, window or game data;
`--seconds` bounds the run.

## Licensing

The [split license](LICENSE), [code license](LICENSE-CODE),
[content license](LICENSE-CONTENT) and [NOTICE](NOTICE) are preserved
verbatim. The NOTICE describes the prior project's dependencies, not this
tree's.
