# Geometry: vertex pulling with no input layout, the VertexStreams block and
# the presentation slice's fixture cube. See docs/architecture/shaders.md,
# invariant 5, and public/bw/rendering/geometry/vertex_streams.hpp. The header
# interface carries the constexpr layout checks and the block declarations;
# the module library adds the fixture cube's out-of-line generator.
bw_header_interface(bw_geometry_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_geometry_headers INTERFACE bw_shaders_headers)
add_library(bw_geometry pulled_mesh_fixture.cpp)
target_link_libraries(bw_geometry PUBLIC bw_geometry_headers)
