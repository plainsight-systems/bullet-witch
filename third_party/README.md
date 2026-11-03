# Third-party dependencies

The shader toolchain's dependencies (docs/architecture/shaders.md,
"Dependencies") and the Metal RHI's (docs/architecture/rhi.md). Upstream source
enters as git submodules pinned to a tag; the Metal Shader Converter runtime
shim is a vendored copy because it ships in an installer, not a repository.
Nothing here is modified.

| Name | Upstream | Tag | Commit | License | Enters the tree as |
|---|---|---|---|---|---|
| DirectXShaderCompiler | https://github.com/microsoft/DirectXShaderCompiler | `v1.9.2609` | `01b62ad47db7dee3dd33f90e7339cf9e860b3f93` | University of Illinois/NCSA (LLVM release license), `LICENSE.TXT`; `ThirdPartyNotices.txt` | submodule `third_party/dxc`, shallow; built by `build-dxc.sh` |
| SPIRV-Tools | https://github.com/KhronosGroup/SPIRV-Tools | DXC's pin | `ef96ed763b43b59b33b31b362f09a02b729fa1c9` | Apache 2.0, `LICENSE` | DXC's nested submodule `external/SPIRV-Tools` |
| SPIRV-Headers | https://github.com/KhronosGroup/SPIRV-Headers | DXC's pin | `496543121ce6419f23d6fa5d7194ba66c36212d2` | MIT-style Khronos license, `LICENSE` (`LICENSES/`) | DXC's nested submodule `external/SPIRV-Headers` |
| DirectX-Headers | https://github.com/microsoft/DirectX-Headers | DXC's pin | `980971e835876dc0cde415e8f9bc646e64667bf7` | MIT, `LICENSE` | DXC's nested submodule `external/DirectX-Headers` |
| SPIRV-Reflect | https://github.com/KhronosGroup/SPIRV-Reflect | `vulkan-sdk-1.4.363.0` | `f3e261fda74ca81b44d1941931c6b7b5fe236d0e` | Apache 2.0, `LICENSE` | submodule `third_party/spirv-reflect`; `spirv_reflect.c` compiled as `bw_spirv_reflect` |
| BLAKE3 | https://github.com/BLAKE3-team/BLAKE3 | `1.8.7` | `f3149ec5bb5449af877ba20377a11008ff499fa2` | CC0 1.0, or Apache 2.0, or Apache 2.0 with LLVM exceptions (`LICENSE_CC0`, `LICENSE_A2`, `LICENSE_A2LLVM`) | submodule `third_party/blake3`; the C implementation compiled as `bw_blake3` |
| metal-cpp | https://github.com/apple/metal-cpp | `release/metal-cpp_macOS26_iOS26` | `f567ed836e4cbb85788c42115a2682bbe68097ee` | Apache 2.0, `LICENSE.txt` | submodule `third_party/metal-cpp`, shallow; headers only, consumed by the Metal RHI |
| Metal Shader Converter runtime shim | Metal Shader Converter installer, `include/metal_irconverter_runtime/` | converter 4.0.3 | none (installer) | Apache 2.0, `metal_irconverter_runtime/LICENSE.txt` | vendored copy `third_party/metal_irconverter_runtime/`: `metal_irconverter_runtime.h`, `ir_tessellator_tables.h`, `ir_raytracing.h`, `LICENSE.txt` |

SPIRV-Reflect's nested `third_party/googletest` stays uninitialized. DXC's
`.gitmodules` still names `external/googletest`, but the tag records no
gitlink for it, so there is nothing to initialize. Nothing here builds
upstream tests.

metal-cpp's tag is the largest release not past the SDK the project builds
with (Xcode 26.2, SDK macOS 26.2). Later tags target macOS 26.4 and 27.

The shim's version is the converter's `include/metal_irconverter/ir_version.h`
(4.0.3). `checks/include_check_apple.cpp` fails the build when the installed
converter is a different release.

## Prerequisite outside the tree

The Metal Shader Converter itself, `libmetalirconverter.dylib`,
`metal_irconverter.h` and the `metal-shaderconverter` binary, is installed
from Apple's installer under `/usr/local` and never enters the tree. Apple
platforms only.

## Checking out

```sh
git submodule update --init third_party/dxc third_party/spirv-reflect third_party/blake3 third_party/metal-cpp
git -C third_party/dxc submodule update --init external/SPIRV-Tools external/SPIRV-Headers external/DirectX-Headers
```

## Building DXC

```sh
third_party/build-dxc.sh <prefix>
```

Configures `third_party/dxc` with its `cmake/caches/PredefinedParams.cmake`,
Release, SPIR-V code generation on, tests, examples and docs off, and source
regeneration off, into `<prefix>/build`; builds `dxc`, `dxcompiler`,
`SPIRV-Tools` and `SPIRV-Tools-opt`; installs `bin/dxc`,
`lib/libdxcompiler.dylib` (`.so` on Linux), `lib/libSPIRV-Tools.a`,
`lib/libSPIRV-Tools-opt.a` and `include/{dxc,spirv-tools,directx,wsl}` into
`<prefix>`. The script refuses to build when the DXC checkout is not at the
recorded pins. Use a prefix of its own, for example a `build-dxc/` directory
in the checkout, which `.gitignore` already covers. The installed
`libdxcompiler.dylib` has the install name `@rpath/libdxcompiler.dylib`.

## CMake

`cmake/ThirdParty.cmake` declares the targets. On Apple platforms
`bw_metal_cpp` and `bw_metal_irconverter_runtime` are declared whatever the
value of `BW_SHADER_TOOLCHAIN`: they are the Metal RHI's runtime dependencies.
Configure stops with a named message when `third_party/metal-cpp` is not
initialized.

With `BW_SHADER_TOOLCHAIN` ON (the default) configure also stops with a named
message when a toolchain prerequisite is missing:

| Cache variable | Default | Must contain |
|---|---|---|
| `BW_DXC_PREFIX` | none | the prefix `build-dxc.sh` installed into |
| `BW_METAL_SHADER_CONVERTER_ROOT` (Apple) | `/usr/local` | `lib/libmetalirconverter.dylib`, `include/metal_irconverter/metal_irconverter.h` |

With `-DBW_SHADER_TOOLCHAIN=OFF` no toolchain target is declared and neither
prerequisite is checked.

| Target | Declared | What it is |
|---|---|---|
| `bw_metal_cpp` | Apple, always | metal-cpp's include directory; links Foundation, Metal and QuartzCore |
| `bw_metal_irconverter_runtime` | Apple, always | the shim's include directory |
| `bw_dxcompiler`, `bw_spirv_tools`, `bw_spirv_tools_opt` | toolchain ON | imported from `BW_DXC_PREFIX` |
| `bw_metal_irconverter` | Apple, toolchain ON | imported from `BW_METAL_SHADER_CONVERTER_ROOT` |
| `bw_spirv_reflect`, `bw_blake3` | toolchain ON | compiled from the submodules |

All their include directories are system directories, so the project's warning
flags do not apply to upstream headers, and the compiled ones are excluded
from clang-tidy. `checks/` holds the build-time include checks that compile
every header through these targets: `include_check_metal_cpp.cpp` builds as
`bw_metal_cpp_include_check` (Apple, always), the rest as
`bw_third_party_include_check` (toolchain ON). The shim is checked twice, after
Metal.h from `include_check_metal.mm` and after metal-cpp under
`IR_RUNTIME_METALCPP`. Every file under `checks/` is
first-party: it runs under clang-tidy (the Objective-C++ file included) and
under the `format-check` gate; the submodule sources do neither.

Including `directx/d3d12shader.h` off Windows needs `dxc/WinAdapter.h` first
and `interface` defined as `struct`, as DXC's own
`include/dxc/Support/D3DReflection.h` does; `checks/include_check.cpp` shows
the order.
