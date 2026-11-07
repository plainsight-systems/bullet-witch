# Geometry: vertex pulling with no input layout, the VertexStreams block, the
# fixture draws and the mesh and morph target payloads. See
# docs/architecture/shaders.md, invariant 5, and
# public/bw/rendering/geometry/vertex_streams.hpp and mesh_asset.hpp. The
# header interface carries the constexpr layout checks and the block
# declarations; the module library adds the fixture cube's out-of-line
# generator and the payload decoders. The
# mesh decoder validates its skeleton id with the content module's asset id
# rule (a constexpr header function), so the library links the content
# headers privately. mesh_resources.hpp names RHI resources, so the
# interface links the RHI headers; the upload itself is a library of its
# own, bw_mesh_resources, so that a client of the payload alone (the
# converter) does not link the RHI.
bw_header_interface(bw_geometry_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_geometry_headers INTERFACE bw_shaders_headers bw_rhi_headers)
add_library(bw_geometry
  pulled_mesh_fixture.cpp
  mesh_asset.cpp
  morph_targets_asset.cpp)
target_link_libraries(bw_geometry PUBLIC bw_geometry_headers PRIVATE bw_content_headers)
add_library(bw_mesh_resources mesh_resources.cpp)
target_link_libraries(bw_mesh_resources PUBLIC bw_geometry bw_rhi)
