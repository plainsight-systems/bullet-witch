# Rendering root: the texture asset layout and its reader
# (public/bw/rendering/texture_asset.hpp), which every renderer module
# consumes; the header names RHI formats and subresource data, so the
# interface links the RHI headers. The module library adds the payload
# decoder. Rendering's subsystems (geometry, materials, rhi, shaders) are
# modules of their own under this directory.
bw_header_interface(bw_texture_asset_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_texture_asset_headers INTERFACE bw_rhi_headers)
add_library(bw_texture_asset texture_asset.cpp)
target_link_libraries(bw_texture_asset PUBLIC bw_texture_asset_headers)
# A model as the renderer holds it (public/bw/rendering/model_resources.hpp),
# the cleared pass every draw begins with (target_pass.hpp), and the two
# fixture draws (textured_model_fixture.hpp, pulled_cube_fixture.hpp), which
# leave with the fixture shaders. The modules a model is made of are linked
# publicly because the headers name their types.
add_library(bw_model
  model_resources.cpp
  target_pass.cpp
  textured_model_fixture.cpp
  pulled_cube_fixture.cpp)
target_link_libraries(bw_model PUBLIC bw_mesh_resources bw_texture_asset bw_content bw_materials
  bw_animation bw_math PRIVATE bw_scene)
