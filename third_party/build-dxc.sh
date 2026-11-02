#!/bin/sh
# Builds the pinned DirectXShaderCompiler in third_party/dxc and installs the
# compiler, its library, the SPIR-V optimizer libraries and the headers the
# shader compiler tool needs into one prefix:
#
#   <prefix>/bin/dxc
#   <prefix>/lib/libdxcompiler.dylib (libdxcompiler.so on Linux)
#   <prefix>/lib/libSPIRV-Tools.a, <prefix>/lib/libSPIRV-Tools-opt.a
#   <prefix>/include/dxc/**         (DXC API and WinAdapter.h)
#   <prefix>/include/spirv-tools/** (SPIRV-Tools API)
#   <prefix>/include/directx/**, <prefix>/include/wsl/**
#                                   (d3d12shader.h and its stubs, for DXIL
#                                    reflection on macOS and Linux)
#
# The build directory is <prefix>/build. Give the prefix to the project with
# -DBW_DXC_PREFIX=<prefix>. Use a prefix dedicated to this build: files are
# overwritten, never removed, so a stale prefix keeps files from older runs.
set -eu

usage() {
  echo "usage: $0 <install-prefix>" >&2
  exit 64
}
[ "$#" -eq 1 ] || usage
[ -n "$1" ] || usage

third_party=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
repository=$(CDPATH='' cd -- "$third_party/.." && pwd -P)
dxc_source="$third_party/dxc"
submodule_help="run: git submodule update --init third_party/dxc && git -C third_party/dxc submodule update --init external/SPIRV-Tools external/SPIRV-Headers external/DirectX-Headers"

# The checked-out DXC and its three nested dependencies must be the commits the
# superproject records. A leading '+' (other commit), '-' (not initialized) or
# 'U' (conflict) in the status line means the build would not be the pin.
for required in \
  "$dxc_source/CMakeLists.txt" \
  "$dxc_source/external/SPIRV-Tools/CMakeLists.txt" \
  "$dxc_source/external/SPIRV-Headers/CMakeLists.txt" \
  "$dxc_source/external/DirectX-Headers/include/directx/d3d12shader.h"; do
  if [ ! -e "$required" ]; then
    echo "error: $required is missing; $submodule_help" >&2
    exit 1
  fi
done
status=$(git -C "$repository" submodule status --recursive third_party/dxc)
unpinned=$(printf '%s\n' "$status" | grep -v '^ ' || true)
if [ -n "$unpinned" ]; then
  printf 'error: third_party/dxc is not at its recorded pins:\n%s\n%s\n' \
    "$unpinned" "$submodule_help" >&2
  exit 1
fi

mkdir -p -- "$1"
prefix=$(CDPATH='' cd -- "$1" && pwd -P)
build="$prefix/build"
jobs=$(getconf _NPROCESSORS_ONLN)

case "$(uname -s)" in
  Darwin) dxcompiler_library=libdxcompiler.dylib ;;
  *) dxcompiler_library=libdxcompiler.so ;;
esac

# HLSL_DISABLE_SOURCE_GENERATION: build from the generated sources the tag
# checks in. Left on, DXC regenerates them, formats them with whatever
# clang-format is first on PATH and fails when that version formats them
# differently from the one upstream used, so the build would depend on the
# host's clang-format.
cmake -S "$dxc_source" -B "$build" \
  -C "$dxc_source/cmake/caches/PredefinedParams.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX="$prefix" \
  -DENABLE_SPIRV_CODEGEN=ON \
  -DSPIRV_BUILD_TESTS=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DCLANG_INCLUDE_TESTS=OFF \
  -DHLSL_INCLUDE_TESTS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_DOCS=OFF \
  -DHLSL_DISABLE_SOURCE_GENERATION=ON

# SPIRV-Tools is an alias of SPIRV-Tools-static (output name SPIRV-Tools) when
# SPIRV-Tools builds its static library, which is its default; an alias is not
# a build target, so the real target is named here.
cmake --build "$build" --parallel "$jobs" \
  --target dxc dxcompiler SPIRV-Tools-static SPIRV-Tools-opt

# Copies every header (*.h, *.hpp, *.inl, *.inc, *.def) under $1 into $2, keeping the
# relative layout. Build files and IDL sources stay behind.
copy_headers() {
  (
    cd -- "$1"
    # find exits non-zero when any batch of the -exec command fails.
    find . -type f \( -name '*.h' -o -name '*.hpp' -o -name '*.inl' \
      -o -name '*.inc' -o -name '*.def' \) -exec sh -c '
        set -eu
        destination=$1
        shift
        for header do
          mkdir -p -- "$destination/${header%/*}"
          cp -- "$header" "$destination/$header"
        done' sh "$2" {} +
  )
}

# Plain copies: files DXC's install rules do not provide.
# - include/dxc/**: the source tree's headers; DXC installs only five of them.
# - include/spirv-tools/**, lib/libSPIRV-Tools*.a: SPIRV-Tools' own install
#   rules carry no install component, so installing them would install every
#   unscoped LLVM rule as well. The archives land in <build>/lib because DXC
#   sets CMAKE_ARCHIVE_OUTPUT_DIRECTORY there.
# - include/directx/**, include/wsl/**: DirectX-Headers is only an include
#   directory of the DXC build and has no install rules in it.
copy_headers "$dxc_source/include/dxc" "$prefix/include/dxc"
copy_headers "$dxc_source/external/SPIRV-Tools/include/spirv-tools" \
  "$prefix/include/spirv-tools"
copy_headers "$dxc_source/external/DirectX-Headers/include/directx" \
  "$prefix/include/directx"
copy_headers "$dxc_source/external/DirectX-Headers/include/wsl" \
  "$prefix/include/wsl"
mkdir -p -- "$prefix/lib"
cp -- "$build/lib/libSPIRV-Tools.a" "$build/lib/libSPIRV-Tools-opt.a" \
  "$prefix/lib/"

# DXC's install rules, by component:
# - dxc: bin/dxc
# - dxcompiler: lib/libdxcompiler.dylib or .so
# - dxc-headers: include/dxc/{dxcapi.h,dxcerrors.h,dxcisense.h,WinAdapter.h}
#   and the generated include/dxc/config.h, which exists only in the build
#   tree. Runs after the plain copy so the installed public headers win.
for component in dxc dxcompiler dxc-headers; do
  cmake --install "$build" --component "$component" --prefix "$prefix"
done

for installed in \
  "$prefix/bin/dxc" \
  "$prefix/lib/$dxcompiler_library" \
  "$prefix/lib/libSPIRV-Tools.a" \
  "$prefix/lib/libSPIRV-Tools-opt.a" \
  "$prefix/include/dxc/dxcapi.h" \
  "$prefix/include/dxc/WinAdapter.h" \
  "$prefix/include/dxc/config.h" \
  "$prefix/include/spirv-tools/optimizer.hpp" \
  "$prefix/include/directx/d3d12shader.h" \
  "$prefix/include/wsl/stubs/rpc.h"; do
  if [ ! -f "$installed" ]; then
    echo "error: expected $installed after install" >&2
    exit 1
  fi
done
echo "DXC installed into $prefix; configure the project with -DBW_DXC_PREFIX=$prefix"
