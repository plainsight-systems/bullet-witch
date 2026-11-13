// Integration test: bw/rendering/scene/depth_hierarchy.hpp on this Mac's
// Metal device with the build's reduce artifact. A depth texture of a known
// pattern at an odd size is reduced by one build and every level read back.
//
// Argument after the scenario name: the Metal artifact directory.
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/executor.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/scene/depth_hierarchy.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace graph = bw::graph;
namespace rhi = bw::rhi;
namespace scene = bw::scene;
using bw::testing::require;

struct Arguments {
  fs::path shaders{};
};

template <class T, class E> T take(rhi::Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed: " + std::string(result.detail()));
  }
  return std::move(result.value());
}

// A positive normal float to the nearest 16-bit float, ties to even.
std::uint16_t nearest_half(float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t exponent = ((bits >> 23U) & 0xFFU) - 127U + 15U;
  const std::uint32_t mantissa = bits & 0x7FFFFFU;
  std::uint32_t half = (exponent << 10U) | (mantissa >> 13U);
  const std::uint32_t rest = mantissa & 0x1FFFU;
  if (rest > 0x1000U || (rest == 0x1000U && (half & 1U) != 0U)) {
    ++half;
  }
  return static_cast<std::uint16_t>(half);
}

// (depth_hierarchy.hpp, contracts 2 and 3): a 7 by 5 depth target whose
// texels are not 16-bit floats reduces to a 4 by 4 R16Float texture of 3
// levels; level 0 texel (x, y) is the largest of its two by two below with
// coordinates clamped to the last column and row, stored as the 16-bit
// float nearest it, and each next level the largest of its two by two.
void furthest_depth(const Arguments& args) {
  rhi::DeviceDescription description{};
  description.backend = rhi::BackendKind::Metal;
  description.miss_policy = rhi::PipelineMissPolicy::CreateOnMiss;
  const std::unique_ptr<rhi::Device> device =
      take(rhi::Device::create(description), "Device::create");
  scene::DepthHierarchy hierarchy;
  require(scene::create_depth_hierarchy(*device, args.shaders, hierarchy).ok(),
          "create_depth_hierarchy");
  constexpr rhi::Extent2 kView{7, 5};
  require(scene::depth_hierarchy_extent(kView) == rhi::Extent2{4, 4},
          "extent: half the power of two");
  require(scene::depth_hierarchy_levels(rhi::Extent2{4, 4}) == 3U, "a full chain");
  require(hierarchy.begin_frame(*device, kView).ok(), "begin_frame");
  require(hierarchy.first_build()->description().format == scene::kDepthHierarchyFormat &&
              hierarchy.first_build()->description().mip_levels == 3U &&
              hierarchy.extent() == rhi::Extent2{4, 4},
          "the texture's format, size and levels");

  std::vector<float> depth(std::size_t{kView.width} * kView.height);
  for (std::uint32_t y = 0; y < kView.height; ++y) {
    for (std::uint32_t x = 0; x < kView.width; ++x) {
      depth[(std::size_t{y} * kView.width) + x] = 0.123456F +
                                                  (0.0371F * static_cast<float>((x * 3U) % 7U)) +
                                                  (0.0517F * static_cast<float>((y * 2U) % 5U));
    }
  }
  rhi::TextureDescription target{};
  target.extent = rhi::Extent3{kView.width, kView.height, 1};
  target.format = rhi::Format::D32Float;
  target.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::DepthStencil;
  target.initial_access = rhi::kShaderResourceMask;
  target.debug_name = "pattern depth";
  const std::array<rhi::SubresourceData, 1> initial{
      rhi::SubresourceData{std::as_bytes(std::span<const float>(depth)), kView.width * 4U,
                           kView.width * kView.height * 4U}};
  const rhi::Ref<rhi::Texture> pattern =
      take(device->create_texture(target, initial), "the pattern depth texture");

  graph::FrameGraph frame_graph;
  const scene::DepthHierarchyFrame frame = scene::declare_depth_hierarchy(hierarchy, frame_graph);
  const graph::TextureHandle source = frame_graph.import_texture(
      graph::ImportedTexture{pattern.get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  scene::declare_depth_hierarchy_build(hierarchy, frame_graph, frame, source,
                                       scene::DepthHierarchyBuild::First);
  graph::CompiledFrame compiled;
  require(frame_graph
              .compile(*graph::schedule_policy_for(rhi::BackendKind::Metal), device->capabilities(),
                       compiled)
              .ok(),
          "the graph compiles");
  rhi::CommandList list;
  require(graph::execute(compiled, frame_graph, *device, list).ok(), "the graph executes");
  // Every level of the first build, read back after the graph's passes.
  rhi::Texture& built = *hierarchy.first_build();
  std::array<rhi::StagingLayout, 3> layouts{};
  std::uint64_t total = 0;
  for (std::uint8_t level = 0; level < 3U; ++level) {
    layouts[level] = rhi::staging_layout(built.description(), level);
    total += layouts[level].total_bytes;
  }
  const rhi::Ref<rhi::StagingBuffer> staging =
      take(device->create_staging_buffer(total, "levels"), "staging");
  const rhi::Ref<rhi::GpuFence> fence = take(device->create_fence("levels"), "fence");
  const std::array<rhi::Transition, 1> to_copy{
      rhi::Transition{&built, rhi::kShaderResourceMask, rhi::Access::CopySource}};
  list.transition(rhi::TransitionBatch{to_copy});
  std::uint64_t offset = 0;
  for (std::uint8_t level = 0; level < 3U; ++level) {
    list.copy_texture_to_staging(built, level, 0, *staging, offset);
    offset += layouts[level].total_bytes;
  }
  const std::array<rhi::Transition, 1> back{
      rhi::Transition{&built, rhi::Access::CopySource, rhi::kShaderResourceMask}};
  list.transition(rhi::TransitionBatch{back});
  list.write_fence(*fence);
  require(device->execute(list).ok(), "execute");
  const rhi::EndFrameResult ended = device->end_frame();
  const rhi::StagingLock lock =
      take(device->lock_staging(*staging, *fence, rhi::WaitPolicy::Block), "lock_staging");
  require(!ended.gpu_fault, "no GPU fault");

  // The levels as the contract states them, as halves and as floats.
  std::vector<std::uint16_t> level0(16);
  for (std::uint32_t y = 0; y < 4U; ++y) {
    for (std::uint32_t x = 0; x < 4U; ++x) {
      float largest = 0.0F;
      for (std::uint32_t b = 0; b < 2U; ++b) {
        for (std::uint32_t a = 0; a < 2U; ++a) {
          const std::uint32_t sx = std::min((2U * x) + a, kView.width - 1U);
          const std::uint32_t sy = std::min((2U * y) + b, kView.height - 1U);
          largest = std::max(largest, depth[(std::size_t{sy} * kView.width) + sx]);
        }
      }
      level0[(y * 4U) + x] = nearest_half(largest);
    }
  }
  // Halves of positive values order as their bits do.
  const auto reduce = [](const std::vector<std::uint16_t>& below, std::uint32_t size) {
    std::vector<std::uint16_t> out(std::size_t{size / 2U} * (size / 2U));
    for (std::uint32_t y = 0; y < size / 2U; ++y) {
      for (std::uint32_t x = 0; x < size / 2U; ++x) {
        out[(y * (size / 2U)) + x] =
            std::max({below[(2U * y * size) + (2U * x)], below[(2U * y * size) + (2U * x) + 1U],
                      below[(((2U * y) + 1U) * size) + (2U * x)],
                      below[(((2U * y) + 1U) * size) + (2U * x) + 1U]});
      }
    }
    return out;
  };
  const std::array<std::vector<std::uint16_t>, 3> want{level0, reduce(level0, 4U),
                                                       reduce(reduce(level0, 4U), 2U)};
  offset = 0;
  for (std::uint8_t level = 0; level < 3U; ++level) {
    const std::uint32_t size = 4U >> level;
    for (std::uint32_t y = 0; y < size; ++y) {
      for (std::uint32_t x = 0; x < size; ++x) {
        std::uint16_t got = 0;
        std::memcpy(&got,
                    lock.bytes.data() + offset + (std::size_t{y} * layouts[level].row_pitch) +
                        (std::size_t{x} * 2U),
                    2);
        const std::size_t at = (std::size_t{y} * size) + x;
        require(got == want[level][at],
                (static_cast<std::size_t>(level) * 100U) + (std::size_t{y} * 10U) + x,
                "level texel is the farthest below, nearest 16-bit float (got " +
                    std::to_string(got) + ", want " + std::to_string(want[level][at]) + ")");
      }
    }
    offset += layouts[level].total_bytes;
  }
  require(device->unlock_staging(*staging) == rhi::StagingError::None, "unlock_staging");
}

constexpr std::array<bw::testing::Scenario<Arguments>, 1> kScenarios{{
    {"furthest_depth", &furthest_depth},
}};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<metal artifact directory>",
                                   [](std::span<char* const> rest, Arguments& out) {
                                     if (rest.size() != 1U) {
                                       return false;
                                     }
                                     out = Arguments{rest[0]};
                                     return true;
                                   });
}
