# capture: the frame-to-file path the tools share (docs/architecture/
# source-layout.md, tools/capture). The PNG writer
# (public/bw/capture/png.hpp) names only the standard library, so its
# header interface is defined on every platform and the header checks
# compile it everywhere.
bw_header_interface(bw_capture_headers "${CMAKE_CURRENT_SOURCE_DIR}")
# The writer itself: pure functions over bytes, no compression library.
add_library(bw_capture png.cpp)
target_link_libraries(bw_capture PUBLIC bw_capture_headers)
