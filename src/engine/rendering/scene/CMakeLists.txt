# Scene: views of the scene and the matrices a draw needs from them
# (public/bw/rendering/scene/view.hpp). The header interface links the math
# and shader headers its declarations name.
bw_header_interface(bw_scene_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_scene_headers INTERFACE bw_math bw_shaders_headers)
add_library(bw_scene view.cpp)
target_link_libraries(bw_scene PUBLIC bw_scene_headers)
