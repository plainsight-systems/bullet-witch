#ifndef BW_RENDERING_RHI_PIPELINE_CACHE_HPP
#define BW_RENDERING_RHI_PIPELINE_CACHE_HPP
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/shaders/permutation.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

// Responsibility: the pipeline description cache and its content key
// (docs/architecture/rhi.md, "Pipeline states and caches": keyed by the
// content hash of the shader artifact hashes plus the full state
// description; filled by the permutation enumeration at launch; a miss at
// draw time is an error in shipping). The driver pipeline cache (Metal
// binary archive and the others) is the backend's and is not here.
//
// Contract.
//   graphics_pipeline_key and compute_pipeline_key hash content only: the
//   artifacts' input hashes (not their addresses), each state object's
//   description (not its address), the vertex layout, the render target
//   layout and the primitive type; the debug name is not hashed. Two
//   descriptions that name equal content through different pointers have
//   equal keys; any change of a shader hash or a state field changes the
//   key. Both are unit tests.
//   PipelineDescriptionCache: find returns the pipeline inserted under the
//   key or a null Ref; insert under a key that is present replaces nothing
//   and returns false; the cache holds a Ref to every pipeline, so a cached
//   pipeline lives until the cache is cleared or destroyed. Single-threaded:
//   the device serializes.
//   enumerate_precompile_set lists every (shader, permutation) the registry's
//   exclusion predicates admit, in registry order then ascending id; it
//   returns the number of entries, writing at most out.size(); scratch must
//   hold the largest permutation_count in the registry. The render passes
//   and state descriptions that complete each entry into a pipeline
//   description are the renderer's (docs/architecture/materials.md,
//   "Material to pipelines"); the device's precompile takes a span of
//   complete descriptions (device.hpp).
//
// Core guidelines read: I.1 (the key is a pure function of the
// description), E.27 (a miss is a returned absence; the policy on a miss is
// the device's, named), C.32 (the cache owns Refs; keys own nothing).
// Performance guidelines read: GDSA.17 (the content key is what makes the
// precompiled set complete and the cache stable across runs), CACHE.3 (a
// sorted vector keyed by a 64-bit value; lookups are a binary search over
// contiguous storage).
namespace bw::rhi {

struct PipelineKey {
  std::uint64_t value{};
  [[nodiscard]] friend constexpr bool operator==(PipelineKey, PipelineKey) noexcept = default;
};

// One content key, never state-object pointers, so the key is the same
// across runs and across the precompile and draw paths. Hash function:
// FNV-1a 64 over the fields in declaration order, each integer
// little-endian, each float as its bits.
[[nodiscard]] PipelineKey graphics_pipeline_key(const GraphicsPipelineDescription& d) noexcept;
[[nodiscard]] PipelineKey compute_pipeline_key(const ComputePipelineDescription& d) noexcept;
// The same keys from the parts a created pipeline keeps, so a cache can be
// rebuilt from its entries.
[[nodiscard]] PipelineKey graphics_pipeline_key(const GraphicsPipeline& pipeline) noexcept;

// One map keyed by content. There are no per-thread caches: pipeline lookup
// is on the one recording path the device serializes. There is no file
// cache: the permutation enumeration is complete by construction.
class PipelineDescriptionCache {
public:
  [[nodiscard]] Ref<GraphicsPipeline> find_graphics(PipelineKey key) const noexcept;
  [[nodiscard]] Ref<ComputePipeline> find_compute(PipelineKey key) const noexcept;
  // Returns false and inserts nothing when the key is present.
  bool insert(PipelineKey key, Ref<GraphicsPipeline> pipeline);
  bool insert(PipelineKey key, Ref<ComputePipeline> pipeline);
  [[nodiscard]] std::size_t graphics_count() const noexcept { return graphics_.size(); }
  [[nodiscard]] std::size_t compute_count() const noexcept { return compute_.size(); }
  void clear() noexcept;

private:
  template <class T> struct Entry {
    PipelineKey key{};
    Ref<T> pipeline{};
  };
  std::vector<Entry<GraphicsPipeline>> graphics_{};
  std::vector<Entry<ComputePipeline>> compute_{};
};

// The permutation enumeration is the source of the shader set (shaders.md,
// invariant 6); precompile takes complete descriptions built from it.
struct PrecompileEntry {
  const shaders::ShaderDescription* shader{};
  shaders::PermutationId permutation{};
};
[[nodiscard]] std::size_t
enumerate_precompile_set(std::span<const shaders::ShaderDescription> registry,
                         std::span<shaders::PermutationId> scratch,
                         std::span<PrecompileEntry> out) noexcept;

} // namespace bw::rhi
#endif
