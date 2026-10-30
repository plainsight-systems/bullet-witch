bw_header_interface(bw_math_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_math_headers INTERFACE bw_platform_math_headers bw_diagnostics_headers)
add_library(bw_math
  vector/vector3.cpp
  rotation/quaternion.cpp
  ../diagnostics/quaternion_diagnostic.cpp
  ../diagnostics/math_diagnostic.cpp
  ${PROJECT_SOURCE_DIR}/src/platform/os/macos/diagnostics/diagnostic_output.cpp)
target_link_libraries(bw_math PUBLIC bw_math_headers PRIVATE bw_platform_diagnostics_headers)
# One public configuration for all inline vector consumers; unchanged semantics.
target_compile_definitions(bw_math PUBLIC
  BW_ENABLE_MATH_DIAGNOSTICS=$<IF:$<CONFIG:Debug>,1,0>)
