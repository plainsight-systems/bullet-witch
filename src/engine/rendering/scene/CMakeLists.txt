# Scene: views of the scene and the matrices a draw needs from them
# (public/bw/rendering/scene/view.hpp), the orbit camera a viewer moves
# (orbit.hpp), the fixture light (fixture_light.hpp)
# and the forward pass's lit shaders with the lit shading model
# (forward_pass.hpp, its HLSL under hlsl/). The header interface links the
# math and shader headers its declarations name and the materials headers
# whose blocks the forward pass reads.
bw_header_interface(bw_scene_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_scene_headers INTERFACE bw_math bw_shaders_headers bw_materials_headers)
add_library(bw_scene view.cpp fixture_light.cpp orbit.cpp)
target_link_libraries(bw_scene PUBLIC bw_scene_headers)
