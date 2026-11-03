#ifndef BW_TESTS_UNIT_RENDERING_RHI_PIPELINE_OBJECTS_HPP
#define BW_TESTS_UNIT_RENDERING_RHI_PIPELINE_OBJECTS_HPP
// Test-local state objects and pipelines, derived from the public types the
// way a backend derives them (pipeline_states.hpp gives them protected
// constructors).
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/shaders/artifact.hpp"

#include <cstdint>

namespace bw_rhi_test {

class TestRasterizerState final : public bw::rhi::RasterizerState {
public:
  TestRasterizerState(bw::rhi::DeferredDeleteQueue& queue,
                      const bw::rhi::RasterizerDescription& description) noexcept
      : RasterizerState(queue, description) {}

private:
  ~TestRasterizerState() override = default;
};

class TestDepthStencilState final : public bw::rhi::DepthStencilState {
public:
  TestDepthStencilState(bw::rhi::DeferredDeleteQueue& queue,
                        const bw::rhi::DepthStencilDescription& description) noexcept
      : DepthStencilState(queue, description) {}

private:
  ~TestDepthStencilState() override = default;
};

class TestBlendState final : public bw::rhi::BlendState {
public:
  TestBlendState(bw::rhi::DeferredDeleteQueue& queue,
                 const bw::rhi::BlendDescription& description) noexcept
      : BlendState(queue, description) {}

private:
  ~TestBlendState() override = default;
};

class TestGraphicsPipeline final : public bw::rhi::GraphicsPipeline {
public:
  TestGraphicsPipeline(bw::rhi::DeferredDeleteQueue& queue,
                       const bw::rhi::GraphicsPipelineDescription& description) noexcept
      : GraphicsPipeline(queue, description, bw::rhi::PipelineBinding{}) {}

private:
  ~TestGraphicsPipeline() override = default;
};

class TestComputePipeline final : public bw::rhi::ComputePipeline {
public:
  TestComputePipeline(bw::rhi::DeferredDeleteQueue& queue,
                      const bw::rhi::ComputePipelineDescription& description) noexcept
      : ComputePipeline(queue, description, bw::rhi::PipelineBinding{}) {}

private:
  ~TestComputePipeline() override = default;
};

// An artifact whose header names a stage and an input hash; the RHI's key
// reads nothing else of it.
inline bw::shaders::Artifact artifact(bw::shaders::ShaderStage stage, std::uint8_t hash_seed) {
  bw::shaders::Artifact result{};
  result.header.stage = stage;
  result.header.target = bw::shaders::ShaderTarget::Metal;
  for (std::size_t i = 0; i < result.header.input_hash.size(); ++i) {
    result.header.input_hash[i] = static_cast<std::uint8_t>(hash_seed + i);
  }
  return result;
}

} // namespace bw_rhi_test

#endif
