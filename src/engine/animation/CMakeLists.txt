# Animation: this slice holds the skeleton asset layout and its reader
# (public/bw/animation/skeleton_asset.hpp); clips and bone maps' use arrive
# with the animation slice. The header interface carries the layout; the
# module library adds the payload decoder.
bw_header_interface(bw_animation_headers "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(bw_animation skeleton_asset.cpp)
target_link_libraries(bw_animation PUBLIC bw_animation_headers)
