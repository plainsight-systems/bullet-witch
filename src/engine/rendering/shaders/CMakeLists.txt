# Shader system: parameter blocks, permutations, root layout, artifact format
# and the compiler tool's command line. See docs/architecture/shaders.md.
# This file declares the public header interface and the shader module
# library. The shader_compiler tool is added by a later step.
bw_header_interface(bw_shaders_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_shaders_headers INTERFACE bw_memory_headers)
add_library(bw_shaders
  artifact.cpp
  artifact_file.cpp
  compiler_options.cpp
  parameter_block.cpp
  permutation.cpp
  root_layout.cpp)
target_link_libraries(bw_shaders PUBLIC bw_shaders_headers)
# The registry names the fixture draw blocks (bw/rendering/geometry/
# pulled_mesh_fixture.hpp and textured_mesh_fixture.hpp). Header interface
# only and private: the geometry header interface links bw_shaders_headers,
# not bw_shaders, so there is no library cycle.
target_link_libraries(bw_shaders PRIVATE bw_geometry_headers)
