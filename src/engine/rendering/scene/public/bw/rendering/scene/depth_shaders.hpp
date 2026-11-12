#ifndef BW_RENDERING_SCENE_DEPTH_SHADERS_HPP
#define BW_RENDERING_SCENE_DEPTH_SHADERS_HPP

#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/view.hpp"

#include <string_view>

// Responsibility: the lit template's depth-only shaders as the engine
// declares them (docs/architecture/scene-renderer.md, "Passes a template
// provides", the Depth row): their names, their blocks and what each stage
// computes. The pass that draws with them is depth_pass.hpp; this header
// names nothing of the RHI, so the shader registry reads it.
// The depth pass's pixel stage exists for the masked clip alone. The level
// fade's discard is not written in it: the record carries the weight
// (draw_records.hpp), and the discard is written with the first mesh that
// has two levels, since until then no weight is non-zero.
//
// Contract.
//   1. Shaders. kDepthVertexShader (vertex, entry main) has the lit
//      template's domain and predicate (lit_template.hpp), the static block
//      View, the per-draw block RecordDraw and the buffer blocks
//      DrawRecord and InstanceRecord; one artifact exists for every
//      admitted permutation. kDepthPixelShader (pixel, entry main) has the
//      same domain, the same static and per-draw blocks and the buffer
//      blocks InstanceRecord and LitMaterial, reads its instance at the
//      index the vertex stage passes it, and is built for the masked
//      permutations only; a pipeline for any other permutation has no
//      pixel stage.
//   2. Vertex stage. The record is the DrawRecord at the draw's index in
//      the list RecordDraw names, the instance the InstanceRecord it names
//      (draw_records.hpp, contract 2). The vertex is SV_VertexID +
//      streams.base_vertex of the instance's streams. Clip position is the
//      position as a row vector times object_to_world times
//      View.world_to_clip, the expression the forward lit vertex shader
//      uses, so the two passes give a pixel the same depth. Masked
//      permutations also pass texture coordinate set 0.
//   3. Pixel stage, masked permutations: the pixel is discarded when the
//      lit template's opacity mask for it is below 0 (lit_template.hpp,
//      contract 5). Nothing is written but depth.
//
// Read guidance: I.1 (every input of a draw is in a block or a record),
// Con.5.
namespace bw::scene {

inline constexpr std::string_view kDepthVertexShader = "depth_lit_mesh";
inline constexpr std::string_view kDepthPixelShader = "depth_lit";

} // namespace bw::scene

#endif
