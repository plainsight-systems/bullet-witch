// Integration test: a converted model drawn through the public contracts end
// to end, with no host, over the PW0 cache the CTest fixture pw0_cache
// converted from the player's install: the layers (layers.hpp, open_layers)
// and the loader (loader.hpp) feed load_model (model_resources.hpp), which
// loads, decodes and uploads the mesh, its skeleton, its materials and
// their base color textures on this Mac's Metal device; the fixture draw
// (textured_model_fixture.hpp) records every draw into an offscreen target
// seen through a view (scene/view.hpp), which a staging buffer reads back
// (synchronization.hpp).
//
// Arguments after the scenario name: the PW0 cache and the Metal artifact
// directory.
#include "bw/content/asset_id.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/textured_mesh_fixture.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/target_pass.hpp"
#include "bw/rendering/textured_model_fixture.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

namespace content = bw::content;
namespace geometry = bw::geometry;
namespace rendering = bw::rendering;
namespace rhi = bw::rhi;
namespace shaders = bw::shaders;

using bw::testing::require;

struct Arguments {
  std::filesystem::path cache;
  std::filesystem::path shaders;
};

template <class T, class E> T take(rhi::Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed with code " +
                             std::to_string(static_cast<int>(result.error())) + ": " +
                             std::string(result.detail()));
  }
  return std::move(result.value());
}

// (model_resources.hpp, load_model, Contract 1, 3 and 5;
// textured_model_fixture.hpp, create_textured_model and
// record_textured_model; scene/view.hpp, world_to_clip and frame_sphere;
// target_pass.hpp): model.pw0.mesh loaded with its materials and base
// color textures and drawn, no draw shaded by the white fallback, with no
// cull and a depth test into a 64x64 RGBA8 target cleared
// to a known color and framed on the mesh's bounding sphere (no stored
// reference image). The frame must cover a stated share of the target and
// show the texture sampled; the thresholds are stated where they are
// checked. Alone catches the load, the upload or the draw of a model no
// longer producing textured texels. It does not tell a model drawn without
// some of its groups from a whole one: group 0 alone passes both
// thresholds.
void pw0_offscreen_not_clear(const Arguments& args) {
  const content::OpenLayersResult opened =
      content::open_layers(std::nullopt, std::nullopt, args.cache);
  require(opened.ok(), "the converted layer opens: " + opened.detail);
  const content::Loader loader(opened.layers);

  rhi::DeviceDescription device_description{};
  device_description.backend = rhi::BackendKind::Metal;
  device_description.miss_policy = rhi::PipelineMissPolicy::CreateOnMiss;
  const std::unique_ptr<rhi::Device> device =
      take(rhi::Device::create(device_description), "Device::create");

  const content::AssetIdResult id = content::make_asset_id("model.pw0.mesh");
  require(id.ok(), "the id model.pw0.mesh validates");
  rendering::ModelResources model;
  const rendering::ModelLoadFailure loaded = rendering::load_model(
      *device, loader, id.id, rendering::kTexturedModelTextureParameters, model);
  require(loaded.ok(), "model.pw0.mesh loads: error " +
                           std::to_string(static_cast<int>(loaded.error)) + " for " + loaded.id +
                           ": " + loaded.detail);
  require(!model.mesh.draws.empty(), "the model has a draw");

  shaders::ArtifactFile vertex;
  shaders::ArtifactFile pixel;
  require(shaders::read_artifact_file(args.shaders, geometry::kTexturedMeshShader,
                                      shaders::PermutationId{}, vertex)
                  .ok() &&
              shaders::read_artifact_file(args.shaders, geometry::kTexturedColorShader,
                                          shaders::PermutationId{}, pixel)
                  .ok(),
          "the textured pair's artifacts load from " + args.shaders.string());
  constexpr rhi::Format kColorFormat = rhi::Format::R8G8B8A8Unorm;
  rendering::TexturedModel textured;
  const rendering::TexturedModelFailure made = rendering::create_textured_model(
      *device, model, vertex.artifact, pixel.artifact, kColorFormat, textured);
  require(made.ok(), "the fixture pipeline is made: error " +
                         std::to_string(static_cast<int>(made.error)) + " at index " +
                         std::to_string(made.index) + ": " + made.detail);
  require(textured.fallback_draws == 0U,
          std::to_string(textured.fallback_draws) + " draws sample the white fallback");

  constexpr std::array<float, 4> kClear{0.25F, 0.5F, 0.75F, 1.0F};
  rhi::TextureDescription target_description{};
  target_description.extent = rhi::Extent3{64, 64, 1};
  target_description.format = kColorFormat;
  target_description.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource;
  target_description.initial_access = rhi::Access::RenderTarget;
  target_description.clear_value.color = kClear;
  target_description.debug_name = "pw0 target";
  const rhi::Ref<rhi::Texture> target =
      take(device->create_texture(target_description), "create the target");
  rhi::Ref<rhi::Texture> depth;
  require(rendering::resize_depth_target(*device, rhi::Extent2{64, 64},
                                         rendering::kTexturedModelDepthFormat, "pw0 depth", depth)
              .ok(),
          "create the depth target");

  // The bounding sphere framed on a square target.
  const std::array<float, 3>& center = model.mesh.center;
  const std::optional<bw::scene::PerspectiveView> view = bw::scene::frame_sphere(
      bw::math::Vector3f{center[0], center[1], center[2]}, model.mesh.radius, 1.0F);
  if (!view.has_value()) {
    throw std::runtime_error("the mesh's bounding sphere cannot be framed");
  }

  rhi::CommandList list;
  rendering::begin_cleared_pass(list, *target, depth.get(), rhi::Extent2{64, 64},
                                "pw0 textured draw");
  rendering::record_textured_model(list, textured, model, bw::scene::world_to_clip(*view, 1.0F));
  list.end_render_pass();

  const rhi::StagingLayout staging_layout = rhi::staging_layout(target->description(), 0);
  const rhi::Ref<rhi::StagingBuffer> staging =
      take(device->create_staging_buffer(staging_layout.total_bytes, "pw0 readback"),
           "create_staging_buffer");
  const rhi::Ref<rhi::GpuFence> fence = take(device->create_fence("pw0 readback"), "create_fence");
  const rhi::Transition to_copy{target.get(), rhi::Access::RenderTarget, rhi::Access::CopySource};
  list.transition(rhi::TransitionBatch{std::span<const rhi::Transition>{&to_copy, 1}});
  list.copy_texture_to_staging(*target, 0, 0, *staging, 0);
  list.write_fence(*fence);
  require(device->execute(list).ok(), "execute succeeds");
  const rhi::EndFrameResult ended = device->end_frame();
  const rhi::StagingLock lock =
      take(device->lock_staging(*staging, *fence, rhi::WaitPolicy::Block), "lock_staging");
  require(!ended.gpu_fault, "the GPU reports no fault");

  // Three checks over the frame, with thresholds set from a measurement of
  // the model's 46 draws at this framing (343 covered texels, 28 of them at
  // least 8 steps between their largest and smallest channel):
  //   coverage: texels that differ from the clear color by more than one
  //   step in some channel number at least kMinCovered, 1/32 of the target;
  //   variation: the covered texels are not all one color;
  //   base color sampled: at least kMinChromatic covered texels have a
  //   largest and smallest channel kChromaticSteps or more apart. shade
  //   scales the three channels alike, so the 1x1 white fallback, or any
  //   gray base, gives r = g = b to within rounding; only a sampled base
  //   color that is not gray separates them. PW0 is mostly dark cloth
  //   (an sRGB texture read into a linear target), so the thresholds are
  //   low in absolute terms.
  // Not checked: that the texture varies across the surface. The measured
  // spread of r / (r + g + b) over the covered texels was within what 8-bit
  // rounding gives at these levels, so a sampled texture and a constant
  // colored one are not told apart at this size.
  constexpr std::size_t kMinCovered = 128;
  constexpr std::size_t kMinChromatic = 16;
  constexpr int kChromaticSteps = 8;
  std::size_t covered = 0;
  std::size_t chromatic = 0;
  bool varied = false;
  std::array<int, 3> first_covered{};
  for (std::uint32_t y = 0; y < staging_layout.height; ++y) {
    for (std::uint32_t x = 0; x < staging_layout.width; ++x) {
      const std::size_t offset =
          static_cast<std::size_t>(y) * staging_layout.row_pitch + static_cast<std::size_t>(x) * 4U;
      std::array<int, 3> texel{};
      bool differs = false;
      for (std::size_t c = 0; c < 3; ++c) {
        texel[c] = static_cast<int>(lock.bytes[offset + c]);
        const int expected = static_cast<int>(std::lround(kClear[c] * 255.0F));
        differs = differs || texel[c] - expected > 1 || expected - texel[c] > 1;
      }
      if (!differs) {
        continue;
      }
      if (covered == 0U) {
        first_covered = texel;
      } else if (texel != first_covered) {
        varied = true;
      }
      ++covered;
      const auto [smallest, largest] = std::minmax({texel[0], texel[1], texel[2]});
      if (largest - smallest >= kChromaticSteps) {
        ++chromatic;
      }
    }
  }
  require(device->unlock_staging(*staging) == rhi::StagingError::None, "unlock_staging");
  require(covered >= kMinCovered, std::to_string(covered) +
                                      " texels differ from the clear color, fewer than " +
                                      std::to_string(kMinCovered));
  require(varied, "every covered texel has the same color");
  require(chromatic >= kMinChromatic,
          std::to_string(chromatic) + " covered texels have channels " +
              std::to_string(kChromaticSteps) + " or more steps apart, fewer than " +
              std::to_string(kMinChromatic) + ": the base color reads as gray");
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"pw0_offscreen_not_clear", &pw0_offscreen_not_clear},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<PW0 cache> <metal artifact directory>",
                                   [](std::span<char* const> rest, Arguments& out) {
                                     if (rest.size() != 2U) {
                                       return false;
                                     }
                                     out = Arguments{rest[0], rest[1]};
                                     return true;
                                   });
}
