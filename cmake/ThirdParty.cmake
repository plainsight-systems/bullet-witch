# Pinned third-party dependencies of the Metal RHI and of the shader toolchain.
# Pins, licenses and the DXC recipe: third_party/README.md. Architecture:
# docs/architecture/rhi.md and docs/architecture/shaders.md, "Dependencies".

# Runtime dependencies of the Metal RHI, Apple only. Independent of the
# BW_SHADER_TOOLCHAIN option: the RHI links them whether or not the shader
# toolchain is built.
if(APPLE)
  # metal-cpp: headers only, submodule third_party/metal-cpp. Linking the
  # target links the three frameworks its headers call into. Its
  # implementation macros are defined in one translation unit of the Metal
  # RHI, never here.
  set(bw_metal_cpp_root "${PROJECT_SOURCE_DIR}/third_party/metal-cpp")
  if(NOT EXISTS "${bw_metal_cpp_root}/Metal/Metal.hpp")
    message(FATAL_ERROR
      "third_party/metal-cpp has no Metal/Metal.hpp: the metal-cpp submodule "
      "is not initialized. Run `git submodule update --init "
      "--recommend-shallow third_party/metal-cpp`.")
  endif()
  add_library(bw_metal_cpp INTERFACE)
  target_include_directories(bw_metal_cpp SYSTEM INTERFACE "${bw_metal_cpp_root}")
  target_link_libraries(bw_metal_cpp INTERFACE
    "-framework Foundation" "-framework Metal" "-framework QuartzCore")

  # Vendored converter runtime shim: headers only, used by the Metal RHI.
  add_library(bw_metal_irconverter_runtime INTERFACE)
  target_include_directories(bw_metal_irconverter_runtime SYSTEM INTERFACE
    "${PROJECT_SOURCE_DIR}/third_party/metal_irconverter_runtime")

  # Build-time proof that metal-cpp and the shim compile together under
  # IR_RUNTIME_METALCPP and the project's warning flags. Compiled as part of
  # ALL, under the global CMAKE_CXX_CLANG_TIDY like every file in
  # third_party/checks/.
  add_library(bw_metal_cpp_include_check OBJECT
    "${PROJECT_SOURCE_DIR}/third_party/checks/include_check_metal_cpp.cpp")
  target_link_libraries(bw_metal_cpp_include_check PRIVATE bw_contracts
    bw_metal_cpp bw_metal_irconverter_runtime)
endif()

# SPIRV-Reflect and BLAKE3 are C libraries; the project itself is C++ only.
enable_language(C)

set(bw_blake3_root "${PROJECT_SOURCE_DIR}/third_party/blake3/c")
add_library(bw_blake3 STATIC
  "${bw_blake3_root}/blake3.c"
  "${bw_blake3_root}/blake3_dispatch.c"
  "${bw_blake3_root}/blake3_portable.c")
target_include_directories(bw_blake3 SYSTEM PUBLIC "${bw_blake3_root}")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64|ARM64)$")
  target_sources(bw_blake3 PRIVATE "${bw_blake3_root}/blake3_neon.c")
  target_compile_definitions(bw_blake3 PRIVATE BLAKE3_USE_NEON=1)
else()
  # Portable path only: blake3_dispatch.c references the x86 SIMD
  # implementations unless each is disabled, and none of their sources are
  # compiled here.
  target_compile_definitions(bw_blake3 PRIVATE
    BLAKE3_USE_NEON=0 BLAKE3_NO_SSE2 BLAKE3_NO_SSE41 BLAKE3_NO_AVX2
    BLAKE3_NO_AVX512)
endif()

set_target_properties(bw_blake3 PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  C_CLANG_TIDY ""
  CXX_CLANG_TIDY "")

# The content converter's vendored block encoders and decoders and its PNG
# decoder: copies of named files at a commit, unmodified, compiled only into
# the converter tool, so they sit above the shader toolchain switch. Like the
# other pinned code they keep their own warning expectations: they do not link
# bw_contracts, clang-tidy is off, and their include directories are system
# directories so consumers' warnings-as-errors flags do not reach them.
set(bw_bc7enc_root "${PROJECT_SOURCE_DIR}/third_party/bc7enc_rdo")
add_library(bw_bc7enc STATIC
  "${bw_bc7enc_root}/rgbcx.cpp"
  "${bw_bc7enc_root}/bc7enc.cpp"
  "${bw_bc7enc_root}/bc7decomp.cpp")
target_include_directories(bw_bc7enc SYSTEM PUBLIC "${bw_bc7enc_root}")
# Floating-point contraction off: the encoders' least-squares and rounding
# steps feed integer decisions, and a fused multiply-add on one host but not
# another would change the encoded bytes (texture_reader.hpp states the
# determinism condition). The upstream build compiles without strict
# aliasing because the encoders cast pixel bytes to structs.
target_compile_options(bw_bc7enc PRIVATE -ffp-contract=off -fno-strict-aliasing)

set(bw_lodepng_root "${PROJECT_SOURCE_DIR}/third_party/lodepng")
add_library(bw_lodepng STATIC "${bw_lodepng_root}/lodepng.cpp")
target_include_directories(bw_lodepng SYSTEM PUBLIC "${bw_lodepng_root}")
# Decoder only, through the C entry points: no encoder, no file access, no
# C++ wrappers, and no ancillary-chunk parsing. Without the ancillary section
# gAMA, cHRM, iCCP, sRGB and the other informational chunks are skipped as
# unknown chunks, so none of them changes a decoded pixel; tRNS is still read,
# as it carries alpha. PUBLIC so every includer of lodepng.h sees the same
# declarations as the compiled file. The decoder's CRC and Adler-32 checks
# stay on (their defaults; nothing here changes them); the CRC is checked on
# the chunks the decoder reads, and a skipped chunk reaches no pixel.
target_compile_definitions(bw_lodepng PUBLIC
  LODEPNG_NO_COMPILE_ENCODER LODEPNG_NO_COMPILE_DISK
  LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS LODEPNG_NO_COMPILE_CPP)

set_target_properties(bw_bc7enc bw_lodepng PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  CXX_CLANG_TIDY "")

# Shader toolchain. Two prerequisites are found through cache variables and are
# never searched for: the DXC install prefix that third_party/build-dxc.sh
# produces, and, on Apple platforms, the Metal Shader Converter install. A
# missing one stops configure; there is no fallback.
option(BW_SHADER_TOOLCHAIN "Build the shader compiler and its integration tests" ON)
if(NOT BW_SHADER_TOOLCHAIN)
  return()
endif()

set(BW_DXC_PREFIX "" CACHE PATH
  "Install prefix of the pinned DXC build (third_party/build-dxc.sh <prefix>)")
if(APPLE)
  set(bw_dxcompiler_name libdxcompiler.dylib)
else()
  set(bw_dxcompiler_name libdxcompiler.so)
endif()
foreach(required IN ITEMS "lib/${bw_dxcompiler_name}" include/dxc/dxcapi.h
    lib/libSPIRV-Tools.a lib/libSPIRV-Tools-opt.a
    include/spirv-tools/optimizer.hpp include/directx/d3d12shader.h)
  if(BW_DXC_PREFIX STREQUAL "")
    set(bw_dxc_problem "BW_DXC_PREFIX is not set")
  elseif(NOT EXISTS "${BW_DXC_PREFIX}/${required}")
    set(bw_dxc_problem "BW_DXC_PREFIX='${BW_DXC_PREFIX}' has no ${required}")
  else()
    continue()
  endif()
  message(FATAL_ERROR
    "BW_SHADER_TOOLCHAIN is ON and ${bw_dxc_problem}. Build the pinned DXC "
    "with `third_party/build-dxc.sh <prefix>` and configure with "
    "-DBW_DXC_PREFIX=<prefix>, or configure with -DBW_SHADER_TOOLCHAIN=OFF.")
endforeach()

# Imported include directories are system directories for consumers, so the
# project's warnings-as-errors flags do not apply to DXC's and SPIRV-Tools'
# headers. wsl/stubs supplies rpc.h and rpcndr.h, which directx/d3dcommon.h
# includes by bare name; DXC's own build adds the same directory
# (third_party/dxc/CMakeLists.txt, include_directories(AFTER ...)).
add_library(bw_dxcompiler SHARED IMPORTED GLOBAL)
set_target_properties(bw_dxcompiler PROPERTIES
  IMPORTED_LOCATION "${BW_DXC_PREFIX}/lib/${bw_dxcompiler_name}"
  INTERFACE_INCLUDE_DIRECTORIES
    "${BW_DXC_PREFIX}/include;${BW_DXC_PREFIX}/include/wsl/stubs")
if(APPLE)
  # The installed library's install name is @rpath/libdxcompiler.dylib.
  set_target_properties(bw_dxcompiler PROPERTIES
    IMPORTED_SONAME "@rpath/${bw_dxcompiler_name}")
endif()

add_library(bw_spirv_tools STATIC IMPORTED GLOBAL)
set_target_properties(bw_spirv_tools PROPERTIES
  IMPORTED_LOCATION "${BW_DXC_PREFIX}/lib/libSPIRV-Tools.a"
  INTERFACE_INCLUDE_DIRECTORIES "${BW_DXC_PREFIX}/include")
add_library(bw_spirv_tools_opt STATIC IMPORTED GLOBAL)
set_target_properties(bw_spirv_tools_opt PROPERTIES
  IMPORTED_LOCATION "${BW_DXC_PREFIX}/lib/libSPIRV-Tools-opt.a"
  INTERFACE_INCLUDE_DIRECTORIES "${BW_DXC_PREFIX}/include"
  INTERFACE_LINK_LIBRARIES bw_spirv_tools)
if(APPLE)
  # Documented prerequisite location of the Metal Shader Converter installer.
  set(BW_METAL_SHADER_CONVERTER_ROOT "/usr/local" CACHE PATH
    "Install root of the Metal Shader Converter (prerequisite, not in the tree)")
  foreach(required IN ITEMS lib/libmetalirconverter.dylib
      include/metal_irconverter/metal_irconverter.h)
    if(NOT EXISTS "${BW_METAL_SHADER_CONVERTER_ROOT}/${required}")
      message(FATAL_ERROR
        "BW_SHADER_TOOLCHAIN is ON and BW_METAL_SHADER_CONVERTER_ROOT="
        "'${BW_METAL_SHADER_CONVERTER_ROOT}' has no ${required}. Install the "
        "Metal Shader Converter (the prerequisite in third_party/README.md) "
        "or point BW_METAL_SHADER_CONVERTER_ROOT at its install root.")
    endif()
  endforeach()
  add_library(bw_metal_irconverter SHARED IMPORTED GLOBAL)
  set_target_properties(bw_metal_irconverter PROPERTIES
    IMPORTED_LOCATION
      "${BW_METAL_SHADER_CONVERTER_ROOT}/lib/libmetalirconverter.dylib"
    INTERFACE_INCLUDE_DIRECTORIES "${BW_METAL_SHADER_CONVERTER_ROOT}/include")
  # Apple clang searches /usr/local/include implicitly, so CMake drops it from
  # the compile line; the LLVM clang-tidy the build runs does not search it, and
  # would not find the converter's headers. State the directory explicitly when
  # it is one of the compiler's implicit ones.
  if("${BW_METAL_SHADER_CONVERTER_ROOT}/include" IN_LIST
      CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES)
    set_target_properties(bw_metal_irconverter PROPERTIES
      INTERFACE_COMPILE_OPTIONS
        "SHELL:-isystem ${BW_METAL_SHADER_CONVERTER_ROOT}/include")
  endif()
endif()

# Compiled third-party libraries. Pinned upstream code keeps its own warning
# expectations: these targets do not link bw_contracts, and clang-tidy is off
# for them, the same exception the workflow makes for pinned Lua. Their include
# directories are system directories for the same reason as the imports.
set(bw_spirv_reflect_root "${PROJECT_SOURCE_DIR}/third_party/spirv-reflect")
# spirv_reflect.h includes its bundled include/spirv/unified1/spirv.h (the
# library default, SPIRV_REFLECT_USE_SYSTEM_SPIRV_H undefined).
add_library(bw_spirv_reflect STATIC "${bw_spirv_reflect_root}/spirv_reflect.c")
target_include_directories(bw_spirv_reflect SYSTEM PUBLIC
  "${bw_spirv_reflect_root}")

set_target_properties(bw_spirv_reflect PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  C_CLANG_TIDY ""
  CXX_CLANG_TIDY "")

# Build-time proof that every target above carries the include directories its
# headers need, under the project's own warning flags. Compiled as part of ALL;
# a missing or broken header is a build failure. Every file under
# third_party/checks/ is first-party and runs under clang-tidy; the submodule
# sources above do not. The .cpp files get the global CMAKE_CXX_CLANG_TIDY; the
# Objective-C++ file gets the same command through OBJCXX_CLANG_TIDY below, so
# the analyzer policy stays in cmake/Quality.cmake.
add_library(bw_third_party_include_check OBJECT
  "${PROJECT_SOURCE_DIR}/third_party/checks/include_check.cpp")
target_link_libraries(bw_third_party_include_check PRIVATE bw_contracts
  bw_dxcompiler bw_spirv_tools_opt bw_spirv_reflect bw_blake3)
if(APPLE)
  # The converter's C API is checked from C++, as the tool includes it.
  # metal_irconverter_runtime.h requires Metal.h (Objective-C) or metal-cpp's
  # Metal.hpp to be included first. The shim is checked under both: after
  # Metal.h from the Objective-C++ translation unit here, after metal-cpp from
  # bw_metal_cpp_include_check above.
  enable_language(OBJCXX)
  target_sources(bw_third_party_include_check PRIVATE
    "${PROJECT_SOURCE_DIR}/third_party/checks/include_check_apple.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/checks/include_check_metal.mm")
  target_link_libraries(bw_third_party_include_check PRIVATE
    bw_metal_irconverter bw_metal_irconverter_runtime)
  set_source_files_properties(
    "${PROJECT_SOURCE_DIR}/third_party/checks/include_check_metal.mm"
    PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
  set_target_properties(bw_third_party_include_check PROPERTIES
    OBJCXX_CLANG_TIDY "${CMAKE_CXX_CLANG_TIDY}")
endif()
