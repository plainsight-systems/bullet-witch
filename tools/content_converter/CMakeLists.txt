# content_converter: the only reader of the original formats and the writer
# of the converted-originals cache (docs/architecture/content.md). Its public
# headers (public/bw/content_converter/*.hpp) name the engine's content,
# geometry, animation, texture and material asset headers and the standard
# library; the header checks compile each alone through this target.
bw_header_interface(bw_content_converter_headers "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(bw_content_converter_headers INTERFACE
  bw_content_headers bw_geometry_headers bw_animation_headers
  bw_texture_asset_headers bw_materials_headers)

# The readers, the payload encoders and the texture pipeline. The payload
# encoders run each layout's own decoder over what they write, so the library
# links the consuming modules; the block encoders and the PNG decoder are the
# vendored targets, reached only from here and never from the runtime.
add_library(bw_content_converter
  cpr_container.cpp
  rat_index.cpp
  asset_data.cpp
  mesh_reader.cpp
  skeleton_reader.cpp
  texture_reader.cpp
  material_mapping.cpp
  tangent_handedness.cpp
  clusters.cpp)
target_link_libraries(bw_content_converter
  PUBLIC bw_content_converter_headers bw_content bw_geometry bw_texture_asset bw_materials
    bw_animation
  PRIVATE bw_bc7enc bw_lodepng)
# The mip filter's float reductions are promised the same bytes on every
# host (texture_reader.hpp); contraction into fused multiply-adds is a
# per-target compiler choice, so it is off for the whole library.
target_compile_options(bw_content_converter PRIVATE -ffp-contract=off)

# The conversion driver over an install (conversion.hpp) as its own library,
# so the readers' library above keeps its sources; workers are std::thread.
add_library(bw_conversion conversion.cpp)
target_link_libraries(bw_conversion PUBLIC bw_content_converter PRIVATE Threads::Threads)

# The command line tool: content_converter convert ... (conversion.hpp).
add_executable(content_converter main.cpp)
target_link_libraries(content_converter PRIVATE bw_conversion)
