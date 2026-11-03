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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/DynamicRHI.cpp:608-674:
// RHIComputeStatePrecachePSOHash hashes the shader hashes and the content of
// the blend, rasterizer and depth-stencil initializers, the primitive type
// and depth bounds; :676-742 RHIComputePrecachePSOHash adds the render
// target formats, depth-stencil format, sample count and the pass fields.
// Followed as the one key. Departure: the pin's runtime cache keys on the
// initializer with state-object pointers
// (Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:112-142, pointers
// hashed at :114-117); one content key here, so the key is the same across
// runs and across the precompile and draw paths. Hash function: FNV-1a 64
// over the fields in declaration order, each integer little-endian, each
// float as its bits; the pin's CityHash64 is not reproduced.
[[nodiscard]] PipelineKey graphics_pipeline_key(const GraphicsPipelineDescription& d) noexcept;
[[nodiscard]] PipelineKey compute_pipeline_key(const ComputePipelineDescription& d) noexcept;
// The same keys from the parts a created pipeline keeps, so a cache can be
// rebuilt from its entries.
[[nodiscard]] PipelineKey graphics_pipeline_key(const GraphicsPipeline& pipeline) noexcept;

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Private/PipelineStateCache.cpp:1718-2474:
// TSharedPipelineStateCache, a map from initializer to state; Find at
// :1808-1858 and Add at :1860-1875. Followed as one map keyed by content.
// Omitted: the per-thread local caches and the backfill map consolidated at
// the frame boundary (:1901-1970), because pipeline lookup here is on the
// one recording path the device serializes; the async compile tasks
// (:3817-4051); the file cache (PipelineFileCache.h:314-458), replaced by
// the permutation enumeration, which is complete by construction.
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

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/PipelineStateCache.h:241-244:
// PrecacheComputePipelineState and PrecacheGraphicsPipelineState take
// complete initializers; Engine/Source/Runtime/RenderCore/Private/ShaderPipelineCache.cpp:1080-1190
// builds them from the bundled cache's descriptors. Followed with the
// permutation enumeration as the source of the shader set (shaders.md,
// invariant 6). Omitted: the bundled and user file caches
// (PipelineFileCache.cpp:2183-2345), their game version (:278, :2432) and
// usage masks (:214-215, :337), and the precache thread pool
// (PipelineStateCache.cpp:561-619).
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
