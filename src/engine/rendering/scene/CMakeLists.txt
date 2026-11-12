# Scene: views of the scene and the matrices a draw needs from them
# (public/bw/rendering/scene/view.hpp), the orbit camera a viewer moves
# (orbit.hpp), the fixture light (fixture_light.hpp)
# and the forward pass's lit shaders with the lit shading model
# (forward_pass.hpp, its HLSL under hlsl/). The header interface links the
# math and shader headers its declarations name and the materials headers
# whose blocks the forward pass reads.
bw_header_interface(bw_scene_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_scene_headers INTERFACE bw_math bw_shaders_headers bw_materials_headers
  bw_geometry_headers bw_rhi_headers bw_graph_headers)
# The instance set, the views of a frame, detail level choice, the CPU order
# and the mesh tables (instance_set.hpp, frame_views.hpp, detail_level.hpp,
# cpu_order.hpp, scene_geometry.hpp, draw_records.hpp), and the work the
# scene declares on the frame graph: the culling dispatch, the depth
# hierarchy and the depth pass (culling.hpp, depth_hierarchy.hpp,
# depth_pass.hpp), which read their artifacts. The uploads and the passes go
# through the device, so the library links the RHI, the graph, the shader
# artifact reader and the mesh payload reader.
add_library(bw_scene view.cpp fixture_light.cpp orbit.cpp instance.cpp instance_set.cpp
  frame_views.cpp detail_level.cpp cpu_order.cpp scene_geometry.cpp draw_records.cpp
  culling.cpp depth_hierarchy.cpp depth_pass.cpp)
target_link_libraries(bw_scene PUBLIC bw_scene_headers bw_rhi bw_geometry bw_graph bw_shaders)
