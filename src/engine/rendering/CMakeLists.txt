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
# a model drawn with its own materials through the forward pass
# (model_draw.hpp), one model as a scene declared on the frame graph
# (model_scene.hpp), and the fixture cube draw (pulled_cube_fixture.hpp), which
# leaves with the fixture shaders. Both draws record inside a pass the render
# graph opens (bw/rendering/graph/executor.hpp). The modules a model is made of are linked publicly because the
# headers name their types.
add_library(bw_model
  model_resources.cpp
  model_draw.cpp
  model_scene.cpp
  pulled_cube_fixture.cpp)
target_link_libraries(bw_model PUBLIC bw_mesh_resources bw_texture_asset bw_content bw_materials
  bw_material_buffer bw_scene bw_animation bw_math bw_graph)
