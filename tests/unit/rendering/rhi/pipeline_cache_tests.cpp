// Unit test: public contracts of bw/rendering/rhi/pipeline_cache.hpp, one
// scenario per CTest entry. Each scenario names the header lines it asserts.
#include "bw/rendering/rhi/pipeline_cache.hpp"

#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "pipeline_objects.hpp"
#include "test_main.hpp"
#include "test_objects.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace {
using bw::rhi::DeferredDeleteQueue;
using bw::rhi::GraphicsPipelineDescription;
using bw::rhi::PipelineKey;
using bw::rhi::Ref;
using bw::shaders::Artifact;
using bw::shaders::ShaderStage;
using bw_rhi_test::require;

// Shaders and state objects for one description; two Sets built the same
// way name equal content through different objects.
struct Set {
  explicit Set(DeferredDeleteQueue& queue,
               const bw::rhi::RasterizerDescription& rasterizer_description = {})
      : rasterizer(
            bw_rhi_test::make<bw_rhi_test::TestRasterizerState>(queue, rasterizer_description)),
        depth_stencil(bw_rhi_test::make<bw_rhi_test::TestDepthStencilState>(
            queue, bw::rhi::DepthStencilDescription{})),
        blend(bw_rhi_test::make<bw_rhi_test::TestBlendState>(queue, bw::rhi::BlendDescription{})) {}

  [[nodiscard]] GraphicsPipelineDescription description(const char* name) const {
    GraphicsPipelineDescription d{};
    d.vertex_shader = &vertex;
    d.pixel_shader = &pixel;
    d.rasterizer = rasterizer.get();
    d.depth_stencil = depth_stencil.get();
    d.blend = blend.get();
    d.targets.color_count = 1;
    d.targets.color_formats[0] = bw::rhi::Format::R8G8B8A8Unorm;
    d.debug_name = name;
    return d;
  }

  Artifact vertex = bw_rhi_test::artifact(ShaderStage::Vertex, 1);
  Artifact pixel = bw_rhi_test::artifact(ShaderStage::Pixel, 100);
  Ref<bw_rhi_test::TestRasterizerState> rasterizer;
  Ref<bw_rhi_test::TestDepthStencilState> depth_stencil;
  Ref<bw_rhi_test::TestBlendState> blend;
};

// pipeline_cache.hpp:20-25 and 68-72: the key hashes content, not
// addresses, and not the debug name; a created pipeline gives the key of
// the description it was created from.
void key_stable_under_pointer_identity() {
  DeferredDeleteQueue queue;
  const Set one(queue);
  const Set two(queue);
  require(one.description("A").vertex_shader != two.description("B").vertex_shader &&
              one.description("A").rasterizer != two.description("B").rasterizer,
          "the two descriptions name different objects");
  const PipelineKey key = bw::rhi::graphics_pipeline_key(one.description("A"));
  require(key == bw::rhi::graphics_pipeline_key(two.description("B")),
          "equal content through different pointers, different names: equal keys");
  const Ref<bw_rhi_test::TestGraphicsPipeline> pipeline =
      bw_rhi_test::make<bw_rhi_test::TestGraphicsPipeline>(queue, one.description("A"));
  require(bw::rhi::graphics_pipeline_key(*pipeline) == key,
          "the pipeline's parts give the description's key");

  const Artifact compute_a = bw_rhi_test::artifact(ShaderStage::Compute, 7);
  const Artifact compute_b = bw_rhi_test::artifact(ShaderStage::Compute, 7);
  require(bw::rhi::compute_pipeline_key({&compute_a, "a"}) ==
              bw::rhi::compute_pipeline_key({&compute_b, "b"}),
          "compute keys hash the artifact's content");
}

// pipeline_cache.hpp:20-26: any change of a shader hash or a state field
// changes the key.
void key_changes_on_shader_hash_or_state() {
  DeferredDeleteQueue queue;
  Set base(queue);
  const PipelineKey key = bw::rhi::graphics_pipeline_key(base.description("Base"));
  const auto differs = [&key](const GraphicsPipelineDescription& d) {
    return bw::rhi::graphics_pipeline_key(d) != key;
  };

  Set vertex_changed(queue);
  vertex_changed.vertex.header.input_hash[31] ^= 1U;
  require(differs(vertex_changed.description("V")), "a vertex hash bit changes the key");
  Set pixel_changed(queue);
  pixel_changed.pixel.header.input_hash[0] ^= 1U;
  require(differs(pixel_changed.description("P")), "a pixel hash bit changes the key");
  GraphicsPipelineDescription depth_only = base.description("D");
  depth_only.pixel_shader = nullptr;
  require(differs(depth_only), "dropping the pixel shader changes the key");

  bw::rhi::RasterizerDescription culled{};
  culled.cull = bw::rhi::CullMode::Clockwise;
  require(differs(Set(queue, culled).description("R")), "a rasterizer field changes the key");
  bw::rhi::RasterizerDescription biased{};
  biased.depth_bias = 0.5F;
  require(differs(Set(queue, biased).description("B")), "a float field changes the key");

  GraphicsPipelineDescription d = base.description("T");
  d.targets.color_formats[0] = bw::rhi::Format::R16G16B16A16Float;
  require(differs(d), "a render target format changes the key");
  d = base.description("S");
  d.targets.sample_count = 4;
  require(differs(d), "the sample count changes the key");
  d = base.description("L");
  d.vertex_layout.attribute_count = 1;
  d.vertex_layout.attributes[0].format = bw::rhi::Format::R32G32B32Float;
  require(differs(d), "the vertex layout changes the key");
  d = base.description("M");
  d.primitive = bw::rhi::PrimitiveType::LineList;
  require(differs(d), "the primitive type changes the key");
  d = base.description("N");
  d.rasterizer = nullptr;
  require(differs(d), "a missing state differs from a present one");

  const Artifact compute_a = bw_rhi_test::artifact(ShaderStage::Compute, 7);
  const Artifact compute_b = bw_rhi_test::artifact(ShaderStage::Compute, 8);
  require(bw::rhi::compute_pipeline_key({&compute_a, "a"}) !=
              bw::rhi::compute_pipeline_key({&compute_b, "a"}),
          "a compute hash change changes the key");
}

// pipeline_cache.hpp:27-31 and 85-92: find returns the pipeline inserted
// under the key, or null; insert under a present key replaces nothing and
// returns false; the cache holds a Ref to every pipeline until cleared.
void find_insert_no_replace() {
  DeferredDeleteQueue queue;
  const Set set(queue);
  bw::rhi::PipelineDescriptionCache cache;
  const PipelineKey key{0x1234U};
  require(!cache.find_graphics(key), "an empty cache finds nothing");
  Ref<bw::rhi::GraphicsPipeline> first =
      bw_rhi_test::make<bw_rhi_test::TestGraphicsPipeline>(queue, set.description("First"));
  const Ref<bw::rhi::GraphicsPipeline> second =
      bw_rhi_test::make<bw_rhi_test::TestGraphicsPipeline>(queue, set.description("Second"));
  require(cache.insert(key, first), "the first insert under a key");
  require(cache.find_graphics(key) == first, "find returns it");
  require(!cache.insert(key, second), "a second insert under the key returns false");
  require(cache.find_graphics(key) == first, "and replaces nothing");
  require(cache.graphics_count() == 1U, "one entry");
  require(!cache.find_graphics(PipelineKey{0x1235U}), "another key finds nothing");

  bw::rhi::GraphicsPipeline* raw = first.get();
  first.reset();
  require(queue.pending() == 0U && raw->ref_count() == 1U, "the cache keeps the pipeline alive");

  const Artifact compute_shader = bw_rhi_test::artifact(ShaderStage::Compute, 3);
  const Ref<bw::rhi::ComputePipeline> compute = bw_rhi_test::make<bw_rhi_test::TestComputePipeline>(
      queue, bw::rhi::ComputePipelineDescription{&compute_shader, "Compute"});
  require(cache.insert(key, compute), "compute entries are their own map");
  require(cache.find_compute(key) == compute && cache.compute_count() == 1U, "find_compute");

  cache.clear();
  require(cache.graphics_count() == 0U && cache.compute_count() == 0U, "clear empties the cache");
  require(queue.pending() == 1U, "clearing releases the cache's last reference");
}

// pipeline_cache.hpp:32-35 and 113-120: every (shader, permutation) the
// predicates admit, in registry order then ascending id; the return is the
// number of entries, at most out.size() of them written.
void precompile_set_from_enumeration() {
  const std::array registry{
      bw::shaders::ShaderDescription{
          .name = "first",
          .should_compile = [](bw::shaders::PermutationId id) noexcept { return id.value != 2U; },
          .permutation_count = 4},
      bw::shaders::ShaderDescription{.name = "second", .permutation_count = 2},
  };
  std::array<bw::shaders::PermutationId, 4> scratch{};
  std::array<bw::rhi::PrecompileEntry, 8> out{};
  const std::size_t count = bw::rhi::enumerate_precompile_set(registry, scratch, out);
  require(count == 5U, "three admitted of the first, two of the second");
  const std::array<std::pair<std::size_t, std::uint32_t>, 5> expected{
      {{0, 0}, {0, 1}, {0, 3}, {1, 0}, {1, 1}}};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    require(out[i].shader == &registry[expected[i].first] &&
                out[i].permutation.value == expected[i].second,
            "registry order, then ascending id");
  }
  std::array<bw::rhi::PrecompileEntry, 2> small{};
  require(bw::rhi::enumerate_precompile_set(registry, scratch, small) == 5U,
          "the count is the whole set");
  require(small[1].shader == &registry[0] && small[1].permutation.value == 1U,
          "only out.size() entries are written");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"key_stable_under_pointer_identity", key_stable_under_pointer_identity},
    bw_rhi_test::Scenario{"key_changes_on_shader_hash_or_state",
                          key_changes_on_shader_hash_or_state},
    bw_rhi_test::Scenario{"find_insert_no_replace", find_insert_no_replace},
    bw_rhi_test::Scenario{"precompile_set_from_enumeration", precompile_set_from_enumeration},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
