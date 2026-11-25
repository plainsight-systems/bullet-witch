# Camera: the pose, rigs as data and the evaluator (public/bw/camera). The
# header interface links the math its declarations name and the input
# headers, since a rig's input slot reads a player's action.
bw_header_interface(bw_camera_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_camera_headers INTERFACE bw_math bw_input_headers)
# The implementation: the pose's arithmetic and the evaluator, which reads
# the player's action states.
add_library(bw_camera pose.cpp evaluator.cpp)
target_link_libraries(bw_camera PUBLIC bw_camera_headers bw_input bw_math PRIVATE bw_world_headers)
