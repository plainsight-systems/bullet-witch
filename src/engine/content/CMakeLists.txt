# Content: asset ids, the cooked asset frame, the layers and the loader, the
# install search and its VDF parser. See docs/architecture/content.md and
# public/bw/content/*.hpp. The header interface carries the constexpr frame
# parse and the id rules; the module library adds the loader, the layers' OS
# probe, the VDF parser, the install search and the BLAKE3 hash. BLAKE3 is
# private: no public header names it.
bw_header_interface(bw_content_headers "${CMAKE_CURRENT_SOURCE_DIR}")
add_library(bw_content
  asset_id.cpp
  cooked_asset.cpp
  layers.cpp
  loader.cpp
  vdf.cpp
  install.cpp)
target_link_libraries(bw_content PUBLIC bw_content_headers PRIVATE bw_blake3)
