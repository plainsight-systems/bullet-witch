# Shader system: parameter blocks, permutations, root layout, artifact format
# and the compiler tool's command line. See docs/architecture/shaders.md.
# This file declares the public header interface and the shader module
# library. The shader_compiler tool is added by a later step.
bw_header_interface(bw_shaders_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_shaders_headers INTERFACE bw_memory_headers)
add_library(bw_shaders
  artifact.cpp
  compiler_options.cpp
  parameter_block.cpp
  permutation.cpp
  root_layout.cpp)
target_link_libraries(bw_shaders PUBLIC bw_shaders_headers)
