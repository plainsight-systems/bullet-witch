# shader_compiler: the only shader build path (docs/architecture/shaders.md,
# "Build-time flow"; docs/architecture/source-layout.md, tools/). Two
# libraries so the tests link what they assert:
#   bw_shader_compiler_logic      pure decisions (logic.hpp, enumeration.hpp);
#                                 links the engine shader module and BLAKE3.
#   bw_shader_compiler_toolchain  DXC, SPIRV-Tools, SPIRV-Reflect and the Metal
#                                 Shader Converter behind compile_shader
#                                 (toolchain.hpp).
# DXC and the converter are loaded at run time from the paths given on the
# command line, never linked: a missing library is the tool's named
# ToolchainUnavailable failure with the loader's text. Their headers come from
# the imported targets' include directories without linking them.
# The tool's public headers (logic.hpp, enumeration.hpp, toolchain.hpp) name
# only the engine shader headers and the standard library; the header checks
# compile each alone through this target (cmake/HeaderChecks.cmake).
bw_header_interface(bw_shader_compiler_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_shader_compiler_headers INTERFACE bw_shaders_headers)

add_library(bw_shader_compiler_logic logic.cpp enumeration.cpp)
target_link_libraries(bw_shader_compiler_logic
  PUBLIC bw_shader_compiler_headers bw_shaders PRIVATE bw_blake3)

add_library(bw_shader_compiler_toolchain
  toolchain.cpp
  dxc_backend.cpp
  spirv_backend.cpp
  shared_library.cpp)
target_link_libraries(bw_shader_compiler_toolchain
  PUBLIC bw_shader_compiler_logic
  PRIVATE bw_spirv_tools_opt bw_spirv_reflect ${CMAKE_DL_LIBS})
target_include_directories(bw_shader_compiler_toolchain SYSTEM PRIVATE
  "$<TARGET_PROPERTY:bw_dxcompiler,INTERFACE_INCLUDE_DIRECTORIES>")
if(APPLE)
  target_sources(bw_shader_compiler_toolchain PRIVATE metal_backend.cpp)
  target_include_directories(bw_shader_compiler_toolchain SYSTEM PRIVATE
    "$<TARGET_PROPERTY:bw_metal_irconverter,INTERFACE_INCLUDE_DIRECTORIES>")
  target_compile_options(bw_shader_compiler_toolchain PRIVATE
    "$<TARGET_PROPERTY:bw_metal_irconverter,INTERFACE_COMPILE_OPTIONS>")
else()
  # The converter ships for Apple platforms only (third_party/README.md).
  target_sources(bw_shader_compiler_toolchain PRIVATE metal_backend_unavailable.cpp)
endif()

add_executable(shader_compiler main.cpp)
target_link_libraries(shader_compiler PRIVATE bw_shader_compiler_toolchain)

# The configure-time enumerator (enumerate/main.cpp), also built here so the
# project's warnings and clang-tidy cover it; cmake/ShaderArtifacts.cmake
# builds and runs its own copy at configure time from enumerate/CMakeLists.txt.
add_executable(shader_compiler_enumerate enumerate/main.cpp)
target_link_libraries(shader_compiler_enumerate PRIVATE bw_shader_compiler_logic)
