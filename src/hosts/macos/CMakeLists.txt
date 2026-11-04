add_executable(engine_host engine_host.cpp)
# Global operators remain executable-local, never a transitive library choice.
target_sources(engine_host PRIVATE ../../engine/core/memory/global_allocation.cpp)
target_link_libraries(engine_host PRIVATE bw_runtime bw_allocator)

# game_host's command line as a pure parse (public/bw/hosts/game_host_options.hpp),
# a library of its own so the unit tests link the parse without the host. It
# names only the standard library, so it is defined on every platform and the
# header checks compile its header everywhere.
bw_header_interface(bw_game_host_options_headers "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(bw_game_host_options game_host_options.cpp)
target_link_libraries(bw_game_host_options PUBLIC bw_game_host_options_headers)

# The game executable's first stage: the AppKit window (macos/window.hpp), the
# Metal device and viewport, and the fixture cube drawn with pulled vertices.
# Apple hosts only, because the window adapter and the Metal backend exist
# only there. Its default shader directory is <executable directory>/shaders/metal,
# where the build writes the artifacts when the shader toolchain is configured.
if(APPLE)
  add_executable(game_host game_host.cpp)
  target_sources(game_host PRIVATE ../../engine/core/memory/global_allocation.cpp)
  target_link_libraries(game_host PRIVATE bw_game_host_options bw_window bw_rhi bw_geometry
    bw_shaders bw_allocator bw_platform_time)
  if(BW_SHADER_TOOLCHAIN)
    add_dependencies(game_host shader_artifacts)
  endif()
endif()
