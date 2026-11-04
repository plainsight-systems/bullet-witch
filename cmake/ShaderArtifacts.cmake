# Shader artifacts: one custom command per artifact the registry enumerates,
# under ${CMAKE_BINARY_DIR}/shaders/<target>/, and the `shader_artifacts`
# target (part of ALL) that builds them. docs/architecture/shaders.md,
# "Build-time flow" and "Artifact"; compiler_options.hpp, enumerate output.
#
# The artifact set is learned at configure time: a standalone enumerator
# (tools/shader_compiler/enumerate) is configured, built and run with
# execute_process, because add_custom_command needs every OUTPUT before the
# build starts and the main build's shader_compiler does not exist yet. Its
# output lines are "<shader>\t<permutation>\t<target>\t<target dir>/<file>".
# Configure re-runs when the registry, the shader module or the HLSL
# directory's entries change (CMAKE_CONFIGURE_DEPENDS). Each command writes a
# Makefile dependency file naming the main source and every include it read,
# so an edited include rebuilds exactly the artifacts that read it. The
# command also depends on shader_compiler, the enumerator's list and the DXC
# and converter libraries, whose identities are inputs of the artifact hash.
#
# Targets: d3d12 and vulkan everywhere (DXIL compiles off Windows); metal on
# Apple platforms, the only hosts with the converter (cmake/ThirdParty.cmake).
set(bw_shader_root "${PROJECT_SOURCE_DIR}/src/engine/rendering/shaders/hlsl")
set(bw_shader_output_root "${CMAKE_BINARY_DIR}/shaders")
set(bw_enumerate_dir "${CMAKE_BINARY_DIR}/shader_enumerate")
set(bw_shader_targets d3d12 vulkan)
if(APPLE)
  list(APPEND bw_shader_targets metal)
endif()
list(JOIN bw_shader_targets "," bw_shader_targets_csv)

file(GLOB bw_shader_module_sources CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/engine/rendering/shaders/*.cpp"
  "${PROJECT_SOURCE_DIR}/src/engine/rendering/shaders/CMakeLists.txt"
  "${PROJECT_SOURCE_DIR}/src/engine/rendering/shaders/public/bw/rendering/shaders/*.hpp"
  "${PROJECT_SOURCE_DIR}/src/engine/rendering/geometry/public/bw/rendering/geometry/*.hpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/engine/rendering/shaders/permutation.cpp"
  ${bw_shader_module_sources}
  "${bw_shader_root}"
  "${PROJECT_SOURCE_DIR}/tools/shader_compiler/enumeration.cpp"
  "${PROJECT_SOURCE_DIR}/tools/shader_compiler/public/bw/shader_compiler/enumeration.hpp"
  "${PROJECT_SOURCE_DIR}/tools/shader_compiler/enumerate/main.cpp"
  "${PROJECT_SOURCE_DIR}/tools/shader_compiler/enumerate/CMakeLists.txt")

set(bw_enumerate_configure_args
  -S "${PROJECT_SOURCE_DIR}/tools/shader_compiler/enumerate"
  -B "${bw_enumerate_dir}"
  -G "${CMAKE_GENERATOR}"
  -DCMAKE_BUILD_TYPE=Release
  "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
  "-DBW_SOURCE_ROOT=${PROJECT_SOURCE_DIR}")
if(CMAKE_MAKE_PROGRAM)
  list(APPEND bw_enumerate_configure_args "-DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM}")
endif()
if(APPLE AND CMAKE_OSX_SYSROOT)
  list(APPEND bw_enumerate_configure_args "-DCMAKE_OSX_SYSROOT=${CMAKE_OSX_SYSROOT}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" ${bw_enumerate_configure_args}
  RESULT_VARIABLE bw_result OUTPUT_VARIABLE bw_output ERROR_VARIABLE bw_output)
if(NOT bw_result EQUAL 0)
  message(FATAL_ERROR "Configuring the shader enumerator failed (${bw_result}):\n${bw_output}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${bw_enumerate_dir}" --config Release
  RESULT_VARIABLE bw_result OUTPUT_VARIABLE bw_output ERROR_VARIABLE bw_output)
if(NOT bw_result EQUAL 0)
  message(FATAL_ERROR "Building the shader enumerator failed (${bw_result}):\n${bw_output}")
endif()
execute_process(
  COMMAND "${bw_enumerate_dir}/bin/shader_enumerate" enumerate
    --targets "${bw_shader_targets_csv}" --output-root "${bw_shader_output_root}"
  RESULT_VARIABLE bw_result OUTPUT_VARIABLE bw_enumeration ERROR_VARIABLE bw_output)
if(NOT bw_result EQUAL 0 OR bw_enumeration STREQUAL "")
  message(FATAL_ERROR
    "The shader enumerator failed (${bw_result}) or listed nothing:\n${bw_output}")
endif()

# The list the commands depend on is rewritten only when it changes, so a
# re-configure with the same artifact set rebuilds nothing.
file(WRITE "${bw_enumerate_dir}/artifacts.txt.new" "${bw_enumeration}")
file(COPY_FILE "${bw_enumerate_dir}/artifacts.txt.new" "${bw_enumerate_dir}/artifacts.txt"
  ONLY_IF_DIFFERENT)
set(bw_artifact_list "${bw_enumerate_dir}/artifacts.txt")

set(bw_toolchain_libraries "$<TARGET_FILE:bw_dxcompiler>")
if(APPLE)
  list(APPEND bw_toolchain_libraries "$<TARGET_FILE:bw_metal_irconverter>")
endif()

string(REPLACE "\n" ";" bw_enumerate_lines "${bw_enumeration}")
set(bw_shader_artifacts "")
foreach(bw_line IN LISTS bw_enumerate_lines)
  if(bw_line STREQUAL "")
    continue()
  endif()
  if(NOT bw_line MATCHES "^([^\t]+)\t([0-9]+)\t([a-z0-9]+)\t([^\t]+)$")
    message(FATAL_ERROR "Unexpected shader enumerator line: '${bw_line}'")
  endif()
  set(bw_shader "${CMAKE_MATCH_1}")
  set(bw_permutation "${CMAKE_MATCH_2}")
  set(bw_target "${CMAKE_MATCH_3}")
  set(bw_artifact "${bw_shader_output_root}/${CMAKE_MATCH_4}")
  set(bw_command "$<TARGET_FILE:shader_compiler>" compile
    --shader "${bw_shader}" --permutation "${bw_permutation}" --target "${bw_target}"
    --shader-root "${bw_shader_root}" --output-root "${bw_shader_output_root}"
    --depfile "${bw_artifact}.d" --dxc-library "$<TARGET_FILE:bw_dxcompiler>")
  if(bw_target STREQUAL "metal")
    list(APPEND bw_command --converter-library "$<TARGET_FILE:bw_metal_irconverter>")
  endif()
  add_custom_command(OUTPUT "${bw_artifact}"
    COMMAND ${bw_command}
    DEPENDS shader_compiler "${bw_artifact_list}" ${bw_toolchain_libraries}
    DEPFILE "${bw_artifact}.d"
    COMMENT "Shader ${bw_shader} permutation ${bw_permutation} for ${bw_target}"
    VERBATIM)
  list(APPEND bw_shader_artifacts "${bw_artifact}")
endforeach()
add_custom_target(shader_artifacts ALL DEPENDS ${bw_shader_artifacts})
