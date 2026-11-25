# Input: keys, key state, modifiers, triggers and a player's actions and
# contexts (public/bw/input). Key state consumes the platform's device
# samples.
bw_header_interface(bw_input_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_input_headers INTERFACE bw_platform_input_headers)
# The implementation: pure functions and tables over the key set, no OS.
add_library(bw_input keys.cpp key_state.cpp modifiers.cpp triggers.cpp player_input.cpp)
target_link_libraries(bw_input PUBLIC bw_input_headers)
