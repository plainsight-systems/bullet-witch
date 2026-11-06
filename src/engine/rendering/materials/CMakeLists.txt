# Materials: this slice holds the material asset layout and its reader
# (public/bw/rendering/materials/material_asset.hpp); templates, instances
# and parameter-block fill arrive with the materials slice
# (docs/architecture/materials.md). The header interface carries the layout
# and links the content headers for the asset id rule; the module library
# adds the payload decoder.
bw_header_interface(bw_materials_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_materials_headers INTERFACE bw_content_headers)
add_library(bw_materials material_asset.cpp)
target_link_libraries(bw_materials PUBLIC bw_materials_headers)
