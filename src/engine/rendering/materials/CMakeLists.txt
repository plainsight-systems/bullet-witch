# Materials (docs/architecture/materials.md): the material asset layout and
# its reader (public/bw/rendering/materials/material_asset.hpp), the shading
# and blend enumerations with the attribute table (shading.hpp), material
# templates, materials and instances (material.hpp), the lit template
# (lit_template.hpp, its HLSL under templates/) and the material buffer
# (material_buffer.hpp). The header interface carries the layouts and the
# constexpr tables; it links the content headers for the asset id rule, the
# shader and geometry headers for parameter blocks, permutation domains and
# vertex attributes, and the RHI headers the material buffer names.
bw_header_interface(bw_materials_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_materials_headers INTERFACE bw_content_headers bw_shaders_headers
  bw_geometry_headers bw_rhi_headers)
# The module library: the payload decoder, the template registry, material
# creation and instances. It touches no GPU object, so a client of the
# payload alone (the converter) does not link the RHI.
add_library(bw_materials material_asset.cpp material.cpp lit_template.cpp)
target_link_libraries(bw_materials PUBLIC bw_materials_headers)
# The material buffer: one GPU buffer of material blocks, a library of its
# own because it links the RHI.
add_library(bw_material_buffer material_buffer.cpp)
target_link_libraries(bw_material_buffer PUBLIC bw_materials bw_rhi)
