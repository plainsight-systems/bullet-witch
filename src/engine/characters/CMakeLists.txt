# Characters: the player's controller (public/bw/characters), a tick
# function of the world that evaluates the player's input and writes the
# camera's control rotation.
bw_header_interface(bw_characters_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_characters_headers INTERFACE bw_world_headers bw_input_headers
  bw_camera_headers)
# The implementation: possession, the control rotation read from the camera
# and the input's evaluation in the controller's tick.
add_library(bw_characters player_controller.cpp)
target_link_libraries(bw_characters PUBLIC bw_characters_headers bw_world bw_input bw_camera)
