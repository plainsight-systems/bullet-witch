# Consumer flags come from real targets, including transitive usage requirements.
# These checks exercise the build boundary, not a manually curated include list.
set(boundary_dir "${CMAKE_BINARY_DIR}/contracts/boundaries")
foreach(consumer IN ITEMS engine_host math_register_tests allocator_structure_tests)
  file(GENERATE OUTPUT "${boundary_dir}/${consumer}.cmake" CONTENT
"set(check_includes \"$<TARGET_PROPERTY:${consumer},INCLUDE_DIRECTORIES>\")
set(check_definitions \"$<TARGET_PROPERTY:${consumer},COMPILE_DEFINITIONS>\")
set(check_options \"$<TARGET_PROPERTY:${consumer},COMPILE_OPTIONS>\")
")
endforeach()
file(GENERATE OUTPUT "${boundary_dir}/configuration.cmake" CONTENT
"set(check_compiler \"${CMAKE_CXX_COMPILER}\")
set(check_standard \"${CMAKE_CXX_STANDARD}\")
set(check_architectures \"${CMAKE_OSX_ARCHITECTURES}\")
set(check_global_flags \"${CMAKE_CXX_FLAGS}\")
set(check_config_flags \"$<$<CONFIG:Debug>:${CMAKE_CXX_FLAGS_DEBUG}>$<$<CONFIG:Release>:${CMAKE_CXX_FLAGS_RELEASE}>\")
")
add_custom_target(include_boundary_checks ALL
  COMMAND "${CMAKE_COMMAND}" "-DBOUNDARY_DIR=${boundary_dir}"
    -P "${PROJECT_SOURCE_DIR}/cmake/RunIncludeBoundaries.cmake"
  VERBATIM)
