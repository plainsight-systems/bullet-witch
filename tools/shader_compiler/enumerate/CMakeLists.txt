# Configure-time enumerator, a standalone project that cmake/ShaderArtifacts.cmake
# configures, builds and runs with execute_process before the main build
# exists, so CMake can declare one command per artifact. It compiles the
# engine shader module through that module's own CMakeLists.txt and
# enumeration.cpp, nothing else: no toolchain, no third-party code.
#
# Input: BW_SOURCE_ROOT, the repository root. Output: bin/shader_enumerate.
cmake_minimum_required(VERSION 3.25)
project(BulletWitchShaderEnumerate LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT IS_DIRECTORY "${BW_SOURCE_ROOT}/src/engine/rendering/shaders")
  message(FATAL_ERROR "BW_SOURCE_ROOT='${BW_SOURCE_ROOT}' is not the repository root")
endif()

include("${BW_SOURCE_ROOT}/cmake/Modules.cmake")
# The main build attaches its warning policy to bw_contracts; this build only
# runs the registry, and the main build compiles the same sources under that
# policy and clang-tidy.
add_library(bw_contracts INTERFACE)
bw_header_interface(bw_memory_headers "${BW_SOURCE_ROOT}/src/engine/core/memory")
# The registry names the fixture draw blocks of the geometry module
# (src/engine/rendering/shaders/permutation.cpp), so the shader module's
# private header dependency is declared here as the main build declares it.
bw_header_interface(bw_geometry_headers "${BW_SOURCE_ROOT}/src/engine/rendering/geometry")
add_subdirectory("${BW_SOURCE_ROOT}/src/engine/rendering/shaders" engine_shaders)

add_executable(shader_enumerate main.cpp "${BW_SOURCE_ROOT}/tools/shader_compiler/enumeration.cpp")
target_include_directories(shader_enumerate PRIVATE "${BW_SOURCE_ROOT}/tools/shader_compiler/public")
target_link_libraries(shader_enumerate PRIVATE bw_shaders)
set_target_properties(shader_enumerate PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin")
