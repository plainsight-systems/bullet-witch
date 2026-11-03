#include "bw/rendering/rhi/pipeline_cache.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <utility>

namespace bw::rhi {

namespace {

// FNV-1a 64 over the fields in declaration order, each integer
// little-endian, each float as its bits (pipeline_cache.hpp).
class KeyHasher {
public:
  void byte(std::uint8_t value) noexcept {
    hash_ ^= value;
    hash_ *= kPrime;
  }
  template <class T>
    requires std::is_unsigned_v<T>
  void integer(T value) noexcept {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
      byte(static_cast<std::uint8_t>(value >> (8U * i)));
    }
  }
  template <class E>
    requires std::is_enum_v<E>
  void enumeration(E value) noexcept {
    integer(static_cast<std::make_unsigned_t<std::underlying_type_t<E>>>(value));
  }
  void flag(bool value) noexcept { byte(value ? 1U : 0U); }
  void real(float value) noexcept { integer(std::bit_cast<std::uint32_t>(value)); }
  void bytes(std::span<const std::uint8_t> values) noexcept {
    for (const std::uint8_t value : values) {
      byte(value);
    }
  }
  [[nodiscard]] PipelineKey key() const noexcept { return PipelineKey{hash_}; }

private:
  static constexpr std::uint64_t kOffsetBasis = 0xCBF29CE484222325ULL;
  static constexpr std::uint64_t kPrime = 0x100000001B3ULL;
  std::uint64_t hash_{kOffsetBasis};
};

void hash_rasterizer(KeyHasher& h, const RasterizerDescription& d) noexcept {
  h.enumeration(d.fill);
  h.enumeration(d.cull);
  h.real(d.depth_bias);
  h.real(d.slope_scale_depth_bias);
  h.enumeration(d.depth_clip);
  h.flag(d.allow_multisample);
}

void hash_stencil_face(KeyHasher& h, const StencilFace& face) noexcept {
  h.flag(face.enable);
  h.enumeration(face.test);
  h.enumeration(face.fail);
  h.enumeration(face.depth_fail);
  h.enumeration(face.pass);
}

void hash_depth_stencil(KeyHasher& h, const DepthStencilDescription& d) noexcept {
  h.flag(d.depth_write);
  h.enumeration(d.depth_test);
  hash_stencil_face(h, d.front);
  hash_stencil_face(h, d.back);
  h.integer(d.stencil_read_mask);
  h.integer(d.stencil_write_mask);
}

void hash_blend(KeyHasher& h, const BlendDescription& d) noexcept {
  for (const RenderTargetBlend& target : d.targets) {
    h.enumeration(target.color_op);
    h.enumeration(target.color_source);
    h.enumeration(target.color_destination);
    h.enumeration(target.alpha_op);
    h.enumeration(target.alpha_source);
    h.enumeration(target.alpha_destination);
    h.enumeration(target.write_mask);
  }
  h.flag(d.independent);
  h.flag(d.alpha_to_coverage);
}

// A state object is hashed by its description, never its address; a null
// state (only in a description the device has not validated) hashes as an
// absent marker so it differs from every present one.
template <class State, class Hash>
void hash_state(KeyHasher& h, const State* state, Hash hash_description) noexcept {
  h.flag(state != nullptr);
  if (state != nullptr) {
    hash_description(h, state->description());
  }
}

void hash_vertex_layout(KeyHasher& h, const VertexLayout& layout) noexcept {
  for (const VertexAttribute& attribute : layout.attributes) {
    h.integer(attribute.location);
    h.enumeration(attribute.format);
    h.integer(attribute.buffer_slot);
    h.integer(attribute.offset);
  }
  for (const VertexBufferBinding& buffer : layout.buffers) {
    h.integer(buffer.stride);
    h.enumeration(buffer.rate);
  }
  h.integer(layout.attribute_count);
  h.integer(layout.buffer_count);
}

void hash_targets(KeyHasher& h, const RenderTargetLayout& targets) noexcept {
  for (const Format format : targets.color_formats) {
    h.enumeration(format);
  }
  h.integer(targets.color_count);
  h.enumeration(targets.depth_stencil_format);
  h.integer(targets.sample_count);
}

// The two shader slots hash the artifacts' input hashes. An absent pixel
// shader hashes as 32 zero bytes, the same as GraphicsPipeline::
// pixel_input_hash reports for it, so the key of a description and the key of
// the pipeline created from it agree (a created pipeline does not keep
// whether it had a pixel shader).
struct ShaderHashes {
  shaders::InputHash vertex{};
  shaders::InputHash pixel{};
};

PipelineKey graphics_key(const ShaderHashes& shaders,
                         const GraphicsPipelineDescription& d) noexcept {
  KeyHasher h;
  h.bytes(shaders.vertex);
  h.bytes(shaders.pixel);
  hash_state(h, d.rasterizer, hash_rasterizer);
  hash_state(h, d.depth_stencil, hash_depth_stencil);
  hash_state(h, d.blend, hash_blend);
  hash_vertex_layout(h, d.vertex_layout);
  hash_targets(h, d.targets);
  h.enumeration(d.primitive);
  return h.key();
}

template <class T> [[nodiscard]] Ref<T> find_entry(const auto& entries, PipelineKey key) noexcept {
  const auto it = std::lower_bound(
      entries.begin(), entries.end(), key.value,
      [](const auto& entry, std::uint64_t value) { return entry.key.value < value; });
  if (it == entries.end() || it->key != key) {
    return Ref<T>{};
  }
  return it->pipeline;
}

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:1860-1875: Add
// inserts the state under its key. Difference: the pin check()s that the key
// is absent (:1871), a fatal assertion; here a present key is a returned
// false and the cache keeps the first entry (pipeline_cache.hpp: insert
// replaces nothing).
template <class Entry, class T>
bool insert_entry(std::vector<Entry>& entries, PipelineKey key, Ref<T> pipeline) {
  const auto it = std::lower_bound(
      entries.begin(), entries.end(), key.value,
      [](const Entry& entry, std::uint64_t value) { return entry.key.value < value; });
  if (it != entries.end() && it->key == key) {
    return false;
  }
  entries.insert(it, Entry{key, std::move(pipeline)});
  return true;
}

} // namespace

PipelineKey graphics_pipeline_key(const GraphicsPipelineDescription& d) noexcept {
  ShaderHashes hashes{};
  if (d.vertex_shader != nullptr) {
    hashes.vertex = d.vertex_shader->header.input_hash;
  }
  if (d.pixel_shader != nullptr) {
    hashes.pixel = d.pixel_shader->header.input_hash;
  }
  return graphics_key(hashes, d);
}

PipelineKey compute_pipeline_key(const ComputePipelineDescription& d) noexcept {
  const shaders::InputHash none{};
  KeyHasher h;
  h.bytes(d.compute_shader != nullptr ? d.compute_shader->header.input_hash : none);
  return h.key();
}

PipelineKey graphics_pipeline_key(const GraphicsPipeline& pipeline) noexcept {
  return graphics_key(ShaderHashes{pipeline.vertex_input_hash(), pipeline.pixel_input_hash()},
                      pipeline.description());
}

Ref<GraphicsPipeline> PipelineDescriptionCache::find_graphics(PipelineKey key) const noexcept {
  return find_entry<GraphicsPipeline>(graphics_, key);
}

Ref<ComputePipeline> PipelineDescriptionCache::find_compute(PipelineKey key) const noexcept {
  return find_entry<ComputePipeline>(compute_, key);
}

bool PipelineDescriptionCache::insert(PipelineKey key, Ref<GraphicsPipeline> pipeline) {
  return insert_entry(graphics_, key, std::move(pipeline));
}

bool PipelineDescriptionCache::insert(PipelineKey key, Ref<ComputePipeline> pipeline) {
  return insert_entry(compute_, key, std::move(pipeline));
}

void PipelineDescriptionCache::clear() noexcept {
  graphics_.clear();
  compute_.clear();
}

std::size_t enumerate_precompile_set(std::span<const shaders::ShaderDescription> registry,
                                     std::span<shaders::PermutationId> scratch,
                                     std::span<PrecompileEntry> out) noexcept {
  std::size_t count = 0;
  for (const shaders::ShaderDescription& shader : registry) {
    const std::size_t admitted = shaders::enumerate_permutations(shader, scratch);
    for (std::size_t i = 0; i < admitted; ++i) {
      if (count < out.size()) {
        out[count] = PrecompileEntry{&shader, scratch[i]};
      }
      ++count;
    }
  }
  return count;
}

} // namespace bw::rhi
