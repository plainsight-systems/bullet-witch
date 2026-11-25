# World: objects, clocks, tick functions and the tick with its publication
# (public/bw/world). It evaluates the camera, so its headers link the
# camera's.
bw_header_interface(bw_world_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_world_headers INTERFACE bw_math bw_camera_headers)
# The implementation: the tick's order, the camera step and the publication.
add_library(bw_world world.cpp)
target_link_libraries(bw_world PUBLIC bw_world_headers bw_camera)
