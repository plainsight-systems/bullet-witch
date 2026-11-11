#ifndef BW_RENDERING_GRAPH_GRAPH_STATE_HPP
#define BW_RENDERING_GRAPH_GRAPH_STATE_HPP

#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"

#include <cstdint>
#include <vector>

// Responsibility: what one FrameGraph holds for a frame, shared by the
// declaration side (builder.cpp) and compile (compile.cpp). Private to the
// module: no test or other module includes it.
namespace bw::graph {

// One texture of the frame, frame-only or imported, indexed by its handle.
struct TextureRecord {
  bool imported{};
  FrameTextureDescription description{};
  ImportedTexture import{};
  // The frame-only texture between acquire and give-back (builder.hpp,
  // contract 4); empty otherwise.
  rhi::Ref<rhi::Texture> acquired{};
  bool presented{};
  bool history{};
};

struct BufferRecord {
  bool imported{};
  FrameBufferDescription description{};
  ImportedBuffer import{};
  rhi::Ref<rhi::Buffer> acquired{};
  bool history{};
};

// A pass's uses in the flat use arrays.
struct PassRange {
  std::uint32_t first_texture{};
  std::uint32_t texture_count{};
  std::uint32_t first_buffer{};
  std::uint32_t buffer_count{};
};

struct FrameGraph::State {
  // The number of the frame being declared, stamped on every handle the
  // builder returns; reset() advances it (builder.hpp, contract 3).
  std::uint16_t frame{};
  std::vector<TextureRecord> textures{};
  std::vector<BufferRecord> buffers{};
  // The passes as added; their use spans view texture_uses and buffer_uses
  // and are re-pointed whenever those arrays move.
  std::vector<PassDeclaration> passes{};
  std::vector<PassRange> ranges{};
  std::vector<TextureUse> texture_uses{};
  std::vector<BufferUse> buffer_uses{};
  // One entry per resource, indexed by handle; a null staging pointer means
  // the resource is not read back.
  std::vector<ReadBack> texture_readbacks{};
  std::vector<ReadBack> buffer_readbacks{};
  // The first declaration rule broken this frame, reported by compile
  // (builder.hpp, contract 1).
  CompileResult first_error{};
};

} // namespace bw::graph

#endif
