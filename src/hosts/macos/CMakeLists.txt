add_executable(engine_host engine_host.cpp)
# Global operators remain executable-local, never a transitive library choice.
target_sources(engine_host PRIVATE ../../engine/core/memory/global_allocation.cpp)
target_link_libraries(engine_host PRIVATE bw_runtime bw_allocator)

# game_host's command line as rows of the option table
# (public/bw/hosts/option_table.hpp, game_host_options.hpp), a library of its
# own so the unit tests link the parse without the host. Its headers name
# only the standard library, so it is defined on every platform and the
# header checks compile them everywhere. The --asset row checks its value
# with the content module's id rule (a constexpr header function), so the
# library links the content headers privately.
bw_header_interface(bw_game_host_options_headers "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(bw_game_host_options game_host_options.cpp)
target_link_libraries(bw_game_host_options PUBLIC bw_game_host_options_headers
  PRIVATE bw_content_headers)

# The game executable's first stage: the AppKit window (macos/window.hpp), the
# Metal device and viewport, and the fixture cube drawn with pulled vertices,
# or with --cache and --asset a model loaded and drawn by the model module
# (bw/rendering/model_resources.hpp, model_draw.hpp), seen
# through a view of the scene module.
# Apple hosts only, because the window adapter and the Metal backend exist
# only there. Its default shader directory is <executable directory>/shaders/metal,
# where the build writes the artifacts when the shader toolchain is configured.
if(APPLE)
  # The shell (game_host.cpp) and its host-private parts: the scene it calls
  # (host_scene.hpp), the cube and model scenes, what they share and the
  # failure line. No test includes these headers.
  add_executable(game_host game_host.cpp
    host_scene.hpp host_failure.hpp scene_support.hpp scene_support.cpp
    cube_scene.hpp cube_scene.cpp model_scene.hpp model_scene.cpp)
  target_sources(game_host PRIVATE ../../engine/core/memory/global_allocation.cpp)
  target_link_libraries(game_host PRIVATE bw_game_host_options bw_window bw_allocator
    bw_platform_time bw_platform_process bw_model bw_scene)
  if(BW_SHADER_TOOLCHAIN)
    add_dependencies(game_host shader_artifacts)
  endif()
endif()
