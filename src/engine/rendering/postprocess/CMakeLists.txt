# Post-processing: exposure, the bloom chain, the grade and its bake, the
# tone mapping pass and the chain that orders them (public/bw/rendering/
# postprocess; docs/architecture/post-processing.md). The header interface
# links the shader headers its blocks are declared with and the RHI and
# graph headers its passes name.
bw_header_interface(bw_postprocess_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_postprocess_headers INTERFACE bw_shaders_headers bw_rhi_headers
  bw_graph_headers)

# The module library. It holds the pure functions the headers declare: the
# exposure relations, the meter and the adaptation (exposure.cpp), the
# grade's validation (grade.cpp), the bake's reference functions
# (grade_bake.cpp) and the vignette (tone_mapping.cpp); and the work the
# chain declares on the frame graph: the exposure texels and passes
# (exposure_passes.cpp), the bloom chain (bloom.cpp), the bake's table and
# passes (grade_bake_pass.cpp), the tone mapping pass
# (tone_mapping_pass.cpp) and the chain (post_chain.cpp), with what they
# share (post_support.cpp). The passes go through the device and read their
# artifacts, so the library links the RHI, the graph and the shader module.
add_library(bw_postprocess exposure.cpp grade.cpp grade_bake.cpp tone_mapping.cpp
  post_support.cpp exposure_passes.cpp bloom.cpp grade_bake_pass.cpp tone_mapping_pass.cpp
  post_chain.cpp)
target_link_libraries(bw_postprocess PUBLIC bw_postprocess_headers bw_rhi bw_graph bw_shaders)
