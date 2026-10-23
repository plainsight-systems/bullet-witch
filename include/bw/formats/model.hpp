// Material animation names and ordered per-draw texture-slot snapshots apply
// both-phase R.2 (#rr-use-ptr) bounded archive views and MEM.9
// (guidelines/memory/ MEM.9-embedded-allocate-at-init.md) construction-time
// metadata. TANM updates one slot and TRST clears it; each draw retains its
// state before later resets. Decode original PC model definitions,
// independently of device construction. Input archive ownership moves into the
// result; all spans borrow that result until destruction/mutation. Invalid
// framing, associations and draw ranges throw resources::Error; unknown
// rendering semantics are retained, never declared renderable. Caller
// serializes mutation. See world-renderer-reconstruction.md and
// research/engine/subsystems/stage-content-decoding.md. Algorithm: retain
// archive; index buffer records; decode TLST names; traverse bounded
// CNKH/nested records in original order; snapshot active bindings and material
// state for each DIP; validate every referenced vertex/index range. Cost:
// O(encoded metadata + referenced indices), owned O(metadata); buffer bytes are
// retained once. Shape streams/offsets remain separate from ordinary streams;
// skeleton construction derives bind transforms without enabling pose sampling.
// Current phase I.11 (#ri-raw) keeps metadata ownership separate from borrowed
// archive bytes; MEM.6 injects the construction domain.
// Optimization: no vertex/index conversion or payload copy.
// Design: cpp-guidelines SL.con.3 (#rsl-bounds) bounds every section; R.2
// (#rr-use-ptr) uses spans for borrowed arrays. cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// propagates the injected memory domain to all nested metadata. Implementation
// consultation and exact supported-record evidence accompany the source file.
// Matrix-reference additions apply both-phase C.32 (#rc-dtor-ptr) to owned
// material metadata and MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) to load-time palette binding
// construction.
#pragma once

#include "bw/formats/animation_definition.hpp"
#include "bw/formats/resource_archive.hpp"

namespace bw::formats {
struct ModelVertexElement {
  std::uint16_t stream{}, offset{};
  std::uint8_t type{}, method{}, usage{}, usage_index{};
};
struct ModelDeclaration {
  explicit ModelDeclaration(const memory::Allocator<std::byte>& allocator)
      : elements(allocator) {}
  memory::Vector<ModelVertexElement> elements;
};
struct ModelBuffer {
  std::size_t archive_record{};
  std::uint32_t index_element_bytes{};  // Zero for a vertex buffer.
};
// Preserve ordinary and shape streams independently. Deformation execution
// belongs to a later pose owner, not the decoder or static material fallback.
struct ModelStream {
  std::uint32_t stream{}, buffer{}, offset{}, stride{};
};
struct ModelTextureReference {
  std::array<std::uint32_t, 2> semantic{};
  std::uint32_t texture{}, secondary{};
};
struct ModelConstant {
  // semantic[0] is the FourCC; semantic[1] is the original register-patch
  // slot, which native construction rewrites. The following two words remain
  // opaque source metadata: binding does not mean resolved GPU registers.
  std::array<std::uint32_t, 2> semantic{}, binding{};
  std::array<float, 4> value{};
};
// Original state entry: semantic FourCC, native resolver patch word, value,
// auxiliary word. Interpretation belongs to the ordered material/pass owner.
struct ModelStateEntry {
  std::uint32_t semantic{}, patch{}, value{}, auxiliary{};
};
// Preserve effective material bindings and each local record range. Native
// continuation commands inherit prior bindings; new semantic writes replace
// their previous value. Decoding remains independent of shader translation.
struct ModelMatrixReference {
  std::uint32_t semantic{}, source_matrix{}, destination_matrix{};
};
struct ModelMaterial {
  explicit ModelMaterial(const memory::Allocator<std::byte>& allocator)
      : textures(allocator),
        constants(allocator),
        matrices(allocator),
        render_states(allocator),
        sampler_states(allocator) {}
  std::array<std::uint32_t, 2> shader_selector{};
  resources::Range records{};
  memory::Vector<ModelTextureReference> textures;
  memory::Vector<ModelConstant> constants;
  // Original matrix semantic and source-to-shader-palette slot mapping.
  memory::Vector<ModelMatrixReference> matrices;
  memory::Vector<ModelStateEntry> render_states, sampler_states;
};
struct ModelRecord {
  std::uint32_t tag{}, depth{};
  resources::Range payload{};  // Relative to header.bin, including nested data.
};
struct ModelDraw {
  std::uint32_t primitive_type{};
  std::int32_t base_vertex{};
  std::uint32_t minimum_vertex{}, vertex_count{}, start_index{},
      primitive_count{};
  std::size_t declaration{}, material{}, index_buffer{};
  ModelStream stream;
  std::uint32_t chunk{}, group{}, volume{};
  std::optional<ModelStream> shape_stream;
  // Effective ordered TANM/TRST bindings at this draw, before later resets.
  std::array<std::optional<std::uint32_t>, 2> texture_animation_slots;
};
class ModelDefinition {
 public:
  static ModelDefinition parse(resources::ResourceArchive archive,
                               std::shared_ptr<memory::Domain> domain);
  const auto& declarations() const { return declarations_; }
  const auto& vertex_buffers() const { return vertex_buffers_; }
  const auto& index_buffers() const { return index_buffers_; }
  const auto& shape_buffers() const { return shape_buffers_; }
  const auto& skeleton() const { return skeleton_; }
  const auto& shape_offsets() const { return shape_offsets_; }
  const auto& materials() const { return materials_; }
  const auto& draws() const { return draws_; }
  const auto& records() const { return records_; }
  const auto& texture_names() const { return texture_names_; }
  // Original model animation-slot names; clips bind these by label, not index.
  const auto& material_animation_names() const {
    return material_animation_names_;
  }
  std::span<const std::uint8_t> header() const;
  std::span<const std::uint8_t> vertex_bytes(std::size_t buffer) const;
  std::span<const std::uint8_t> index_bytes(std::size_t buffer) const;
  std::span<const std::uint8_t> shape_bytes(std::size_t buffer) const;
  std::span<const std::uint8_t> shape_offset_bytes(std::size_t buffer) const;

 private:
  ModelDefinition(resources::ResourceArchive archive,
                  const memory::Allocator<std::byte>& allocator);
  void decode();
  struct State;
  void records(resources::Range range, State& state, std::uint32_t depth);
  void validate_draw(const ModelDraw& draw) const;
  resources::ResourceArchive archive_;
  memory::Allocator<std::byte> allocator_;
  std::size_t header_record_{};
  memory::Vector<ModelDeclaration> declarations_;
  memory::Vector<ModelBuffer> vertex_buffers_, index_buffers_, shape_buffers_,
      shape_offsets_;
  std::optional<SkeletonDefinition> skeleton_;
  memory::Vector<ModelMaterial> materials_;
  memory::Vector<ModelDraw> draws_;
  memory::Vector<ModelRecord> records_;
  memory::Vector<memory::String> texture_names_;
  memory::Vector<memory::String> material_animation_names_;
};
}  // namespace bw::formats
