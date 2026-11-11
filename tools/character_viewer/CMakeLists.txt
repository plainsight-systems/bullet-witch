# character_viewer: the windowed composition that draws a converted model
# under an orbit camera and dumps the frame (docs/architecture/
# source-layout.md, tools/character_viewer). Its command line
# (public/bw/character_viewer/viewer_options.hpp) is a table of the host
# option module, so the header interface links that module's headers and
# is defined on every platform; the executable is Apple-only like game_host.
# The input script (public/bw/character_viewer/script.hpp) names the
# window's event type, so the interface links the window contract's headers.
bw_header_interface(bw_character_viewer_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_character_viewer_headers INTERFACE bw_game_host_options_headers
  bw_platform_window_headers)

# The parse as a library of its own, so the unit tests link it without the
# executable; like game_host's, it names only the standard library and is
# defined on every platform. The --asset row checks its value with the
# content module's id rule, so it links the content headers privately.
add_library(bw_viewer_options viewer_options.cpp)
target_link_libraries(bw_viewer_options PUBLIC bw_character_viewer_headers
  PRIVATE bw_content_headers)

# The input script's parse as a library of its own, for the same reason: a
# pure function of the text over the window contract's event constructors,
# defined on every platform.
add_library(bw_viewer_script script.cpp)
target_link_libraries(bw_viewer_script PUBLIC bw_character_viewer_headers)

# The viewer: game_host's window, device, viewport and frame loop around the
# engine's model scene (bw/rendering/model_scene.hpp), one scene per --asset,
# seen through the orbit camera (bw/rendering/scene/orbit.hpp), with the
# frame read back through the render graph and written by the capture
# library, and the commands of --script applied one per frame. Apple hosts
# only, because the window adapter and the Metal backend exist only there.
# Its default shader directory is <executable directory>/shaders/metal, as
# game_host's.
if(APPLE)
  add_executable(character_viewer main.cpp)
  # Global operators remain executable-local, never a transitive library choice.
  target_sources(character_viewer PRIVATE
    ${PROJECT_SOURCE_DIR}/src/engine/core/memory/global_allocation.cpp)
  target_link_libraries(character_viewer PRIVATE bw_viewer_options bw_viewer_script bw_window
    bw_allocator bw_platform_time bw_platform_process bw_model bw_graph bw_scene bw_capture)
  if(BW_SHADER_TOOLCHAIN)
    add_dependencies(character_viewer shader_artifacts)
  endif()
endif()
