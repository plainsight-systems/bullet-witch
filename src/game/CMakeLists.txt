# The game's own rules. This slice holds the actions and default mappings
# (input/public/bw/game/input) and Alicia's views
# (characters/alicia/public/bw/game/characters/alicia).
bw_header_interface(bw_game_input_headers "${CMAKE_CURRENT_SOURCE_DIR}/input")
target_link_libraries(bw_game_input_headers INTERFACE bw_input_headers)
bw_header_interface(bw_game_alicia_headers "${CMAKE_CURRENT_SOURCE_DIR}/characters/alicia")
target_link_libraries(bw_game_alicia_headers INTERFACE bw_game_input_headers bw_camera_headers
  bw_characters_headers)
# The tables and their load; Alicia's rigs and director.
add_library(bw_game_input input/actions.cpp)
target_link_libraries(bw_game_input PUBLIC bw_game_input_headers bw_input)
add_library(bw_game_alicia characters/alicia/view.cpp)
target_link_libraries(bw_game_alicia PUBLIC bw_game_alicia_headers bw_game_input bw_camera
  bw_characters)
