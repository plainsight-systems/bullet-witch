// Integration test: the RHI on this Mac's Metal device (docs/architecture/rhi.md).
// Each scenario creates a Device through device.hpp with the Metal backend,
// drives it through the public headers only, and asserts an invariant a
// header states; the comment above each scenario names the lines. The shader
// artifacts are the ones the build produced for the fixture shaders
// (fullscreen_triangle permutation 0, fullscreen_triangle_color permutation 0;
// for the vertex pulling scenarios pulled_mesh, vertex_decode_probe one
// permutation per element type, and vertex_color).
//
// Arguments after the scenario name: the Metal artifact directory,
// ${CMAKE_BINARY_DIR}/shaders/metal.
//
// Every assertion holds whatever the GPU's timing: a state that is only
// observable while a frame is still in flight (PollOnly NotReady, a Submitted
// fence, EndFrameResult::waited true) is not asserted.
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_cache.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// The fixture vertex shader's per-draw block, declared as the registry
// declares it (src/engine/rendering/shaders/permutation.cpp); the scenario
// that draws checks its layout hash against the registry's before using it.
struct alignas(bw::shaders::kBlockAlignment) FullscreenTriangleDraw {
  bw::shaders::Float4 color{};
  bw::shaders::Float2 scale{};
  bw::shaders::TextureIndex texture{};
  bw::shaders::SamplerIndex sampler{};
};

} // namespace

BW_PARAMETER_BLOCK_LAYOUT(FullscreenTriangleDraw,
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, color),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, scale),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, texture),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, sampler));

namespace {

using namespace bw::rhi;

using bw::testing::require;

template <class T, class E> T take(Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed with code " +
                             std::to_string(static_cast<int>(result.error())) + ": " +
                             std::string(result.detail()));
  }
  return std::move(result.value());
}

// ---- Shader artifacts ------------------------------------------------------------

struct LoadedArtifact {
  std::vector<std::byte> bytes;
  bw::shaders::Artifact artifact{};
};

// Loads a permutation of a registry shader, 0 unless named, with the
// expectations the registry gives (artifact.hpp, load_artifact). The artifact
// views the bytes, so the result stays on the heap where the bytes do not
// move.
std::unique_ptr<LoadedArtifact> load_fixture(const std::filesystem::path& directory,
                                             std::string_view shader_name,
                                             bw::shaders::PermutationId permutation = {}) {
  const bw::shaders::ShaderDescription* shader =
      bw::shaders::find_shader(bw::shaders::shader_registry(), shader_name);
  require(shader != nullptr, "the registry holds the fixture shader");
  const std::filesystem::path path =
      directory / bw::shaders::artifact_file_name(shader->name, permutation, shader->stage);
  std::ifstream stream(path, std::ios::binary);
  require(static_cast<bool>(stream), "the fixture artifact is readable: " + path.string());
  auto loaded = std::make_unique<LoadedArtifact>();
  const std::vector<char> chars{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
  loaded->bytes.resize(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    loaded->bytes[i] = static_cast<std::byte>(chars[i]);
  }
  std::vector<const bw::shaders::BlockLayout*> blocks;
  if (shader->draw_block != nullptr) {
    blocks.push_back(shader->draw_block);
  }
  const bw::shaders::LoadExpectations expectations{&bw::shaders::engine_root_layout(), blocks,
                                                   shader->permutation_count};
  const bw::shaders::ArtifactLoadResult result =
      bw::shaders::load_artifact(loaded->bytes, expectations);
  require(result.ok(), "the fixture artifact loads: " + path.string());
  loaded->artifact = result.artifact();
  return loaded;
}

struct Fixtures {
  std::unique_ptr<LoadedArtifact> vertex;
  std::unique_ptr<LoadedArtifact> pixel;
};

Fixtures load_fixtures(const std::filesystem::path& directory) {
  return Fixtures{load_fixture(directory, "fullscreen_triangle"),
                  load_fixture(directory, "fullscreen_triangle_color")};
}

// ---- Devices -----------------------------------------------------------------------

#if BW_RHI_DEBUG
// Validation failures a scenario captures instead of aborting, with the names
// copied (the reporter's views live for the call only).
struct CapturedFailure {
  ValidationCode code{ValidationCode::None};
  TransitionError transition{TransitionError::None};
  std::string resource;
  std::string command;
};
void capture_failure(const ValidationFailure& failure, void* user) noexcept {
  auto* failures = static_cast<std::vector<CapturedFailure>*>(user);
  try {
    failures->push_back(CapturedFailure{failure.code, failure.transition,
                                        std::string(failure.resource_name),
                                        std::string(failure.command)});
  } catch (...) {
    std::abort();
  }
}
#else
// No validation layer exists in this configuration (build.hpp), so nothing
// is captured.
struct CapturedFailure {};
#endif

// A device on this Mac. The miss policy is CreateOnMiss so pipelines are
// created on first use in both build configurations; a scenario that tests
// the policy sets its own. With failures non-null, validation failures are
// captured there (debug builds); otherwise the default reporter aborts, so a
// scenario that must be validation-clean fails if it is not.
std::unique_ptr<Device> make_device(DeviceDescription description = {},
                                    std::vector<CapturedFailure>* failures = nullptr) {
  description.backend = BackendKind::Metal;
#if BW_RHI_DEBUG
  if (failures != nullptr) {
    description.reporter = &capture_failure;
    description.reporter_user = failures;
  }
#else
  static_cast<void>(failures);
#endif
  return take(Device::create(description), "Device::create");
}

DeviceDescription create_on_miss() {
  DeviceDescription description{};
  description.miss_policy = PipelineMissPolicy::CreateOnMiss;
  return description;
}

// ---- Shared scene pieces -----------------------------------------------------------

TextureDescription target_description(std::array<float, 4> clear, std::string_view name) {
  TextureDescription description{};
  description.extent = Extent3{64, 64, 1};
  description.format = Format::R8G8B8A8Unorm;
  description.usage = TextureUsage::RenderTarget | TextureUsage::CopySource;
  description.initial_access = Access::RenderTarget;
  description.clear_value.color = clear;
  description.debug_name = name;
  return description;
}

RenderPassDescription clear_pass(Texture& target, std::string_view name) {
  RenderPassDescription pass{};
  pass.colors[0] = ColorTarget{&target, 0, 0, LoadAction::Clear, StoreAction::Store};
  pass.color_count = 1;
  pass.name = name;
  return pass;
}

// The texels of a 64x64 RGBA8 readback in the staging layout.
struct Readback {
  std::vector<std::array<std::uint8_t, 4>> texels;
  [[nodiscard]] const std::array<std::uint8_t, 4>& at(std::uint32_t x, std::uint32_t y) const {
    return texels[static_cast<std::size_t>(y) * 64U + x];
  }
};

Readback decode(std::span<const std::byte> bytes, const StagingLayout& layout) {
  Readback readback;
  readback.texels.resize(static_cast<std::size_t>(layout.width) * layout.height);
  for (std::uint32_t y = 0; y < layout.height; ++y) {
    for (std::uint32_t x = 0; x < layout.width; ++x) {
      const std::size_t offset =
          static_cast<std::size_t>(y) * layout.row_pitch + static_cast<std::size_t>(x) * 4U;
      auto& texel = readback.texels[static_cast<std::size_t>(y) * layout.width + x];
      for (std::size_t c = 0; c < 4; ++c) {
        texel[c] = static_cast<std::uint8_t>(bytes[offset + c]);
      }
    }
  }
  return readback;
}

// The fixture pipeline and the resources its draw block names: a 1x1 white
// texture with a view and a sampler, as the triangle scenarios need.
struct TriangleScene {
  Fixtures fixtures;
  Ref<RasterizerState> rasterizer;
  Ref<DepthStencilState> depth_stencil;
  Ref<BlendState> blend;
  GraphicsPipelineDescription description{};
  Ref<GraphicsPipeline> pipeline;
  Ref<Texture> white;
  Ref<ShaderResourceView> white_view;
  Ref<Sampler> sampler;
};

GraphicsPipelineDescription fixture_pipeline_description(const Fixtures& fixtures,
                                                         const RasterizerState& rasterizer,
                                                         const DepthStencilState& depth_stencil,
                                                         const BlendState& blend) {
  GraphicsPipelineDescription description{};
  description.vertex_shader = &fixtures.vertex->artifact;
  description.pixel_shader = &fixtures.pixel->artifact;
  description.rasterizer = &rasterizer;
  description.depth_stencil = &depth_stencil;
  description.blend = &blend;
  description.targets.color_formats[0] = Format::R8G8B8A8Unorm;
  description.targets.color_count = 1;
  description.targets.sample_count = 1;
  description.primitive = PrimitiveType::TriangleList;
  description.debug_name = "fixture triangle";
  return description;
}

TriangleScene make_triangle_scene(Device& device, const std::filesystem::path& artifacts) {
  TriangleScene scene{};
  scene.fixtures = load_fixtures(artifacts);
  scene.rasterizer = take(device.rasterizer_state(RasterizerDescription{}), "rasterizer_state");
  DepthStencilDescription depth_off{};
  depth_off.depth_write = false;
  depth_off.depth_test = CompareFunction::Always;
  scene.depth_stencil = take(device.depth_stencil_state(depth_off), "depth_stencil_state");
  scene.blend = take(device.blend_state(BlendDescription{}), "blend_state");
  scene.description = fixture_pipeline_description(scene.fixtures, *scene.rasterizer,
                                                   *scene.depth_stencil, *scene.blend);
  scene.pipeline = take(device.graphics_pipeline(scene.description), "graphics_pipeline");

  TextureDescription white{};
  white.extent = Extent3{1, 1, 1};
  white.format = Format::R8G8B8A8Unorm;
  white.usage = TextureUsage::ShaderResource | TextureUsage::CopyDestination;
  white.initial_access = Access::ShaderResourcePixel | Access::ShaderResourceNonPixel;
  white.debug_name = "white texel";
  const std::array<std::byte, 4> texel{std::byte{0xFF}, std::byte{0xFF}, std::byte{0xFF},
                                       std::byte{0xFF}};
  const std::array<SubresourceData, 1> data{SubresourceData{texel, 4, 0}};
  scene.white = take(device.create_texture(white, data), "create the white texture");
  scene.white_view = take(device.create_shader_resource_view(scene.white, TextureViewDescription{}),
                          "create the white texture's view");
  scene.sampler = take(device.sampler(SamplerDescription{}), "sampler");
  return scene;
}

FullscreenTriangleDraw fixture_draw_block(const TriangleScene& scene) {
  FullscreenTriangleDraw block{};
  block.color = bw::shaders::Float4{1.0F, 0.5F, 0.25F, 1.0F};
  block.scale = bw::shaders::Float2{1.0F, 1.0F};
  block.texture = scene.white_view->texture_index();
  block.sampler = scene.sampler->index();
  return block;
}

// Records the triangle into `target` with the viewport and scissor (8, 8, 32,
// 32), optionally inside occlusion query 0 of `pool`.
void record_triangle(CommandList& list, const TriangleScene& scene, Texture& target,
                     QueryPool* pool) {
  RenderPassDescription pass = clear_pass(target, "fixture triangle");
  pass.occlusion_queries = pool;
  list.begin_render_pass(pass);
  list.set_graphics_pipeline(*scene.pipeline);
  list.set_viewport(ViewportRect{8.0F, 8.0F, 32.0F, 32.0F, 0.0F, 1.0F});
  list.set_scissor(ScissorRect{8, 8, 32, 32});
  list.set_draw_block(fixture_draw_block(scene));
  if (pool != nullptr) {
    list.begin_query(*pool, 0);
  }
  list.draw(DrawArguments{3, 1, 0, 0});
  if (pool != nullptr) {
    list.end_query(*pool, 0);
  }
  list.end_render_pass();
}

// Readback of a 64x64 RGBA8 target after the commands already in `list`:
// the transition to CopySource, the copy, the fence, execute, end_frame, and
// a blocking lock.
struct ReadbackResult {
  Readback texels;
  bool waited{};
  bool signaled_before_lock{};
  StagingError second_lock{StagingError::None};
};
ReadbackResult read_back(Device& device, CommandList& list, Texture& target) {
  const StagingLayout layout = staging_layout(target.description(), 0);
  const Ref<StagingBuffer> staging =
      take(device.create_staging_buffer(layout.total_bytes, "readback"), "create_staging_buffer");
  const Ref<GpuFence> fence = take(device.create_fence("readback fence"), "create_fence");
  const Transition to_copy{&target, Access::RenderTarget, Access::CopySource};
  list.transition(TransitionBatch{std::span<const Transition>{&to_copy, 1}});
  list.copy_texture_to_staging(target, 0, 0, *staging, 0);
  list.write_fence(*fence);
  const Status<ExecuteError> executed = device.execute(list);
  require(executed.ok(), "execute succeeds");
  static_cast<void>(device.end_frame());
  ReadbackResult result{};
  result.signaled_before_lock = fence->poll();
  const StagingLock lock =
      take(device.lock_staging(*staging, *fence, WaitPolicy::Block), "lock_staging(Block)");
  result.waited = lock.waited;
  const StagingLockResult again = device.lock_staging(*staging, *fence, WaitPolicy::Block);
  result.second_lock = again.ok() ? StagingError::None : again.error();
  require(lock.bytes.size() == layout.total_bytes, "the lock spans the staging buffer");
  result.texels = decode(lock.bytes, layout);
  require(device.unlock_staging(*staging) == StagingError::None, "unlock_staging");
  return result;
}

// A [0, 1] value as the nearest 8-bit unorm, and whether a channel is within
// one unit of it.
struct Unorm8 {
  long value{};
};
Unorm8 to_unorm8(float value) { return Unorm8{std::lround(value * 255.0F)}; }
bool within_one(std::uint8_t actual, Unorm8 expected) {
  const long difference = static_cast<long>(actual) - expected.value;
  return difference >= -1 && difference <= 1;
}

// ---- Scenarios ------------------------------------------------------------------------

// (device.hpp, Device::create: create checks the two required capabilities
// and creates the backend); (backend.hpp, RequiredCapability: the required
// entries are true) and (backend.hpp, Capabilities: the device's name;
// frames in flight, descriptor capacities and the transient budget are the
// budgets the backend was created with, frames_in_flight, descriptors and
// transient_bytes_per_frame).
void device_creation_and_required_capabilities(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  const Capabilities& capabilities = device->capabilities();
  require(capabilities.backend == BackendKind::Metal, "the backend is Metal");
  require(capabilities.typed_descriptor_arrays, "typed descriptor arrays are reported");
  require(capabilities.shader_floor, "the shader floor is reported");
  require(!capabilities.name().empty(), "the device has a name");
  require(capabilities.frames_in_flight == kDefaultFramesInFlight, "three frames in flight");
  const DeviceDescription defaults{};
  require(capabilities.descriptors.read == defaults.descriptors.read &&
              capabilities.descriptors.sampler == defaults.descriptors.sampler &&
              capabilities.descriptors.storage == defaults.descriptors.storage,
          "the descriptor capacities are the description's");
  require(capabilities.transient_bytes_per_frame == defaults.transient_bytes_per_frame,
          "the transient budget is the description's");
}

// (device.hpp, Device::create: create validates the description first and
// fails by name, creating no device). One probe of the wiring; the order of
// validate_device_description's codes is its unit test's.
void device_create_rejects_bad_description(const std::filesystem::path& /*artifacts*/) {
  DeviceDescription capacity{};
  capacity.descriptors.storage = 1;
  const DeviceResult bad_capacity = Device::create(capacity);
  require(!bad_capacity.ok() && bad_capacity.error() == DeviceError::DescriptorCapacityInvalid,
          "a storage capacity other than the root layout's is DescriptorCapacityInvalid");
}

// (pipeline_states.hpp, LoadAction::Clear: Clear writes the target texture's
// clear_value); (command_list.hpp, CommandList::transition: a transition is
// declared before the copy); (synchronization.hpp, StagingBuffer: Block waits
// on the fence first and reports that it waited; a second lock before unlock
// is AlreadyLocked) and (synchronization.hpp, staging_layout: the texture
// lands in the staging layout).
void clear_offscreen_and_read_back(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  constexpr std::array<float, 4> kClear{0.25F, 0.5F, 0.75F, 1.0F};
  const Ref<Texture> target =
      take(device->create_texture(target_description(kClear, "clear target")), "create_texture");
  CommandList list;
  list.begin_render_pass(clear_pass(*target, "clear"));
  list.end_render_pass();
  const ReadbackResult result = read_back(*device, list, *target);
  if (result.signaled_before_lock) {
    require(!result.waited, "a lock on a fence already signaled reports that it did not wait");
  }
  require(result.second_lock == StagingError::AlreadyLocked,
          "a second lock before unlock is AlreadyLocked");
  for (std::uint32_t y = 0; y < 64; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      const auto& texel = result.texels.at(x, y);
      for (std::size_t c = 0; c < 4; ++c) {
        require(within_one(texel[c], to_unorm8(kClear[c])), "every texel equals the clear value");
      }
    }
  }
}

// (device.hpp, Device::graphics_pipeline) and
// (pipeline_states.hpp, GraphicsPipelineDescription: a pipeline from the
// fixture artifacts with an empty vertex layout);
// (binding_structure.hpp, derive_binding_structure, kEngineArgumentBufferBytes:
// the 160-byte top-level argument buffer: the draw block's address at 24 and
// the tables at 0, 8, 16, against a live draw that reads the draw block, the
// Texture2D array and the sampler array);
// (command_list.hpp, CommandList::set_draw_block: set_draw_block binds the
// block for the draw). Coverage: the viewport and scissor are (8, 8, 32, 32) and the
// triangle covers the viewport, so every texel in [8, 40) on both axes is
// the block's color times the white texel within one unit, and every other
// texel is the clear color.
void draw_fixture_triangle_and_read_back(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const bw::shaders::ShaderDescription* shader =
      bw::shaders::find_shader(bw::shaders::shader_registry(), "fullscreen_triangle");
  require(shader != nullptr && shader->draw_block != nullptr &&
              shader->draw_block->hash ==
                  bw::shaders::BlockLayoutOf<FullscreenTriangleDraw>::value.hash,
          "the test's draw block has the registry's layout");
  const TriangleScene scene = make_triangle_scene(*device, artifacts);
  const Ref<Texture> target =
      take(device->create_texture(target_description({0.0F, 0.0F, 0.0F, 1.0F}, "triangle target")),
           "create_texture");
  CommandList list;
  record_triangle(list, scene, *target, nullptr);
  const ReadbackResult result = read_back(*device, list, *target);
  constexpr std::array<float, 4> kColor{1.0F, 0.5F, 0.25F, 1.0F};
  for (std::uint32_t y = 0; y < 64; ++y) {
    for (std::uint32_t x = 0; x < 64; ++x) {
      const auto& texel = result.texels.at(x, y);
      const bool inside = x >= 8U && x < 40U && y >= 8U && y < 40U;
      for (std::size_t c = 0; c < 4; ++c) {
        const float expected = inside ? kColor[c] : (c == 3U ? 1.0F : 0.0F);
        if (!within_one(texel[c], to_unorm8(expected))) {
          throw std::runtime_error("texel (" + std::to_string(x) + ", " + std::to_string(y) + ") " +
                                   (inside ? "inside" : "outside") + " the triangle is rgba " +
                                   std::to_string(texel[0]) + " " + std::to_string(texel[1]) + " " +
                                   std::to_string(texel[2]) + " " + std::to_string(texel[3]));
        }
      }
    }
  }
}

// (device.hpp, Device::end_frame: end_frame destroys the delete queue's
// entries at or below the completed frame and reports how many) and
// (device.hpp, Device::wait_idle: wait_idle runs the queue for every submitted
// frame); (lifetime.hpp, Resource::release, DeferredDeleteQueue::destroy_completed:
// a release at zero enqueues; the entry is destroyed only once its frame has
// completed). Observed through Device::delete_queue().pending().
void release_mid_frame_destroys_after_completion(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  BufferDescription description{};
  description.size = 4096;
  description.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
  description.initial_access = Access::CopySource;
  description.debug_name = "released mid frame";
  Ref<Buffer> source = take(device->create_buffer(description), "create the source buffer");
  description.initial_access = Access::CopyDestination;
  description.debug_name = "copy destination";
  const Ref<Buffer> destination =
      take(device->create_buffer(description), "create the destination");
  CommandList list;
  list.copy_buffer(*source, *destination, BufferCopy{0, 0, 4096});
  require(device->execute(list).ok(), "execute succeeds");
  source.reset();
  require(device->delete_queue().pending() == 1U,
          "the last release enqueues the buffer and nothing destroys it before end_frame");
  const EndFrameResult frame = device->end_frame();
  require(device->delete_queue().pending() + frame.destroyed == 1U,
          "end_frame destroys the buffer or leaves it queued, never both or neither");
  device->wait_idle();
  require(device->delete_queue().pending() == 0U,
          "the buffer is destroyed once its frame has completed");
}

// (device.hpp, Device::end_frame) and (lifetime.hpp, required_completion:
// end_frame blocks on the oldest frame when frames_in_flight frames are
// queued, and only then); (device.hpp, EndFrameResult). With three in flight
// the first two end_frames
// have no required completion, so neither waits. With one in flight the
// device waits for frame submitted + 1 - 1 whenever it is not complete, so
// every end_frame returns with completed >= submitted.
void frames_in_flight_blocks_on_the_oldest(const std::filesystem::path& /*artifacts*/) {
  const auto run_frames = [](std::uint32_t frames_in_flight, auto&& check) {
    DeviceDescription description{};
    description.frames_in_flight = frames_in_flight;
    const std::unique_ptr<Device> device = make_device(description);
    BufferDescription buffer{};
    buffer.size = 4096;
    buffer.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
    buffer.initial_access = Access::CopySource;
    buffer.debug_name = "frame source";
    const Ref<Buffer> source = take(device->create_buffer(buffer), "create the source");
    buffer.initial_access = Access::CopyDestination;
    buffer.debug_name = "frame destination";
    const Ref<Buffer> destination = take(device->create_buffer(buffer), "create the destination");
    for (int frame = 0; frame < 2; ++frame) {
      CommandList list;
      list.copy_buffer(*source, *destination, BufferCopy{0, 0, 4096});
      require(device->execute(list).ok(), "execute succeeds");
      check(device->end_frame());
    }
    device->wait_idle();
  };
  run_frames(3, [](const EndFrameResult& result) {
    require(!result.waited, "with three frames in flight the first two end_frames do not wait");
  });
  run_frames(1, [](const EndFrameResult& result) {
    require(result.completed >= result.submitted,
            "with one frame in flight end_frame returns with its frame completed");
  });
}

// (synchronization.hpp, GpuFence, FenceState: a written fence is Pending
// until its frame is submitted; poll is false before the GPU signals; wait
// returns after the signal; then the state is Signaled) and
// (synchronization.hpp, GpuFence::reset: reset returns the fence to
// Unwritten); (device.hpp, Device::wait_fence).
void fence_poll_false_then_true_and_wait_returns(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  const Ref<GpuFence> fence = take(device->create_fence("poll fence"), "create_fence");
  CommandList list;
  list.write_fence(*fence);
  require(device->execute(list).ok(), "execute succeeds");
  require(fence->state() == FenceState::Pending, "a replayed write is Pending");
  require(!fence->poll(), "the fence polls false before its frame is submitted");
  static_cast<void>(device->end_frame());
  const Status<StagingError> waited = device->wait_fence(*fence);
  require(waited.ok(), "wait_fence returns after the frame is submitted");
  require(fence->poll(), "the fence polls true after the wait");
  require(fence->state() == FenceState::Signaled, "the fence is Signaled after the wait");
  fence->reset();
  require(fence->state() == FenceState::Unwritten, "reset returns the fence to Unwritten");
}

// (synchronization.hpp, fence_wait_error, StagingError::FenceNotSubmitted)
// with (device.hpp, Device::wait_fence: wait_fence checks fence_wait_error
// first and rejects a fence whose write's frame is not submitted, without
// blocking). One probe of the wiring; fence_wait_error's mapping is its unit
// test's.
void fence_wait_errors_named(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  const Ref<GpuFence> fence = take(device->create_fence("unsubmitted fence"), "create_fence");
  CommandList list;
  list.write_fence(*fence);
  require(device->execute(list).ok(), "execute succeeds");
  const Status<StagingError> waited = device->wait_fence(*fence);
  require(!waited.ok() && waited.error() == StagingError::FenceNotSubmitted,
          "wait_fence before end_frame is FenceNotSubmitted");
  static_cast<void>(device->end_frame());
  device->wait_idle();
}

// (queries.hpp, QueryKind::Occlusion: an occlusion result is the number of
// samples that passed, not a boolean) and
// (queries.hpp, QueryError::NotSubmitted, QueryError::NotEnded: Block before
// the ending frame is submitted returns NotSubmitted without blocking; the
// value once that frame has completed; NotEnded for a query not ended);
// (queries.hpp, QueryError::IndexOutOfRange) and
// (device.hpp, Device::query_result). The triangle covers 32 x 32 one-sample
// pixels.
void occlusion_query_counts_covered_samples(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const TriangleScene scene = make_triangle_scene(*device, artifacts);
  const Ref<QueryPool> pool =
      take(device->create_query_pool(QueryPoolDescription{QueryKind::Occlusion, 2, "occlusion"}),
           "create_query_pool");
  const Ref<Texture> target =
      take(device->create_texture(target_description({0.0F, 0.0F, 0.0F, 1.0F}, "occlusion target")),
           "create_texture");
  CommandList list;
  record_triangle(list, scene, *target, pool.get());
  require(device->execute(list).ok(), "execute succeeds");
  const QueryResult unsubmitted = device->query_result(*pool, 0, WaitPolicy::Block);
  require(!unsubmitted.ok() && unsubmitted.error() == QueryError::NotSubmitted,
          "Block before the ending frame's end_frame is NotSubmitted");
  static_cast<void>(device->end_frame());
  const QueryValue covered = take(device->query_result(*pool, 0, WaitPolicy::Block), "query 0");
  constexpr std::uint64_t kCoveredSamples = 32ULL * 32ULL;
  require(covered.value == kCoveredSamples, "the query counts the 1024 covered samples");
  const QueryResult never_ended = device->query_result(*pool, 1, WaitPolicy::Block);
  require(!never_ended.ok() && never_ended.error() == QueryError::NotEnded,
          "a query never ended is NotEnded");
  const QueryResult outside = device->query_result(*pool, 2, WaitPolicy::Block);
  require(!outside.ok() && outside.error() == QueryError::IndexOutOfRange,
          "an index past the pool is IndexOutOfRange");
}

// (queries.hpp, QueryError::KindUnsupported: AbsoluteTime only with
// timestamp_queries, else KindUnsupported, the device's check) and
// (queries.hpp, validate_query_pool: probed once for its wiring with
// ZeroCount).
void query_pool_errors_named(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  const QueryPoolResult zero =
      device->create_query_pool(QueryPoolDescription{QueryKind::Occlusion, 0, "zero"});
  require(!zero.ok() && zero.error() == QueryError::ZeroCount, "a count of 0 is ZeroCount");
  require(!device->capabilities().timestamp_queries, "timestamp queries are reported absent");
  const QueryPoolResult timestamps =
      device->create_query_pool(QueryPoolDescription{QueryKind::AbsoluteTime, 1, "timestamps"});
  require(!timestamps.ok() && timestamps.error() == QueryError::KindUnsupported,
          "an AbsoluteTime pool without the capability is KindUnsupported");
}

// (command_list.hpp, CommandList::replay, ExecuteError: replay returns the
// first failure; the ring has the device's budget and never grows:
// TransientBudgetExhausted when the frame's ring is full, DrawBlockTooLarge
// for one block above the ring); (backend.hpp, Context::set_draw_block). The
// blocks are set inside a pass with the fixture
// pipeline bound, so the validation layer has nothing to report during the
// replay. The two codes are asserted in separate frames, so no precedence
// between them is implied.
void transient_budget_exhaustion_is_named(const std::filesystem::path& artifacts) {
  DeviceDescription description = create_on_miss();
  description.transient_bytes_per_frame = 1024;
  std::vector<CapturedFailure> failures;
  const std::unique_ptr<Device> device = make_device(description, &failures);
  const TriangleScene scene = make_triangle_scene(*device, artifacts);
  const Ref<Texture> target =
      take(device->create_texture(target_description({0.0F, 0.0F, 0.0F, 1.0F}, "budget target")),
           "create_texture");
  // 33 blocks of 32 bytes are more than a 1024-byte ring holds at any
  // placement alignment, and each alone fits it.
  CommandList exhausting;
  exhausting.begin_render_pass(clear_pass(*target, "budget"));
  exhausting.set_graphics_pipeline(*scene.pipeline);
  for (int i = 0; i < 1024 / 32 + 1; ++i) {
    exhausting.set_draw_block(fixture_draw_block(scene));
  }
  exhausting.end_render_pass();
  const Status<ExecuteError> full = device->execute(exhausting);
  require(!full.ok() && full.error() == ExecuteError::TransientBudgetExhausted,
          "blocks beyond a 1024-byte ring are TransientBudgetExhausted");
  require(failures.empty(), "the replay up to the failure raises no validation failure");
  static_cast<void>(device->end_frame());

  const std::vector<std::byte> large(2048);
  CommandList too_large;
  too_large.begin_render_pass(clear_pass(*target, "budget"));
  too_large.set_graphics_pipeline(*scene.pipeline);
  too_large.set_draw_block(large, bw::shaders::BlockLayoutOf<FullscreenTriangleDraw>::value.hash);
  too_large.end_render_pass();
  const Status<ExecuteError> oversized = device->execute(too_large);
  require(!oversized.ok() && oversized.error() == ExecuteError::DrawBlockTooLarge,
          "a block larger than the ring is DrawBlockTooLarge in a fresh frame");
  static_cast<void>(device->end_frame());
  device->wait_idle();
}

// (resources.hpp, validate_lock: only an Upload buffer locks: validate_lock's
// wiring, probed with NotUploadBuffer) and (resources.hpp, LockError, LockResult)
// with (device.hpp, Device::lock_buffer, Device::unlock_buffer: a second lock
// is AlreadyLocked; unlock without lock is NotLocked; the lock is a span of
// the range).
void lock_buffer_errors_named(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  BufferDescription plain{};
  plain.size = 256;
  plain.usage = BufferUsage::CopySource;
  plain.initial_access = Access::CopySource;
  plain.debug_name = "default buffer";
  const Ref<Buffer> default_buffer = take(device->create_buffer(plain), "create a default buffer");
  const LockResult not_upload = device->lock_buffer(*default_buffer, BufferRange{0, 64});
  require(!not_upload.ok() && not_upload.error() == LockError::NotUploadBuffer,
          "a default buffer is NotUploadBuffer");
  BufferDescription upload = plain;
  upload.usage = BufferUsage::Upload | BufferUsage::CopySource;
  upload.debug_name = "upload buffer";
  const Ref<Buffer> upload_buffer = take(device->create_buffer(upload), "create an upload buffer");
  require(device->unlock_buffer(*upload_buffer) == LockError::NotLocked,
          "unlock without a lock is NotLocked");
  const std::span<std::byte> mapped =
      take(device->lock_buffer(*upload_buffer, BufferRange{16, 64}), "lock_buffer");
  require(mapped.size() == 64U, "the lock spans the requested range");
  const LockResult again = device->lock_buffer(*upload_buffer, BufferRange{0, 64});
  require(!again.ok() && again.error() == LockError::AlreadyLocked,
          "a second lock is AlreadyLocked");
  require(device->unlock_buffer(*upload_buffer) == LockError::None, "unlock after lock");
}

// (resources.hpp, ResourceError::NullResource: a null Ref passed to a view
// creation is NullResource).
void view_of_null_resource_is_named(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  const ShaderResourceViewResult view =
      device->create_shader_resource_view(Ref<Texture>{}, TextureViewDescription{});
  require(!view.ok() && view.error() == ResourceError::NullResource,
          "a view of a null texture is NullResource");
}

// (device.hpp, PipelineMissPolicy::Error, Device::precompile: with
// PipelineMissPolicy::Error a miss returns CacheMiss; precompile creates the
// description, after which the lookup returns it).
void pipeline_miss_policy_error_returns_cache_miss(const std::filesystem::path& artifacts) {
  DeviceDescription description{};
  description.miss_policy = PipelineMissPolicy::Error;
  const std::unique_ptr<Device> device = make_device(description);
  const Fixtures fixtures = load_fixtures(artifacts);
  const Ref<RasterizerState> rasterizer =
      take(device->rasterizer_state(RasterizerDescription{}), "rasterizer_state");
  const Ref<DepthStencilState> depth_stencil =
      take(device->depth_stencil_state(DepthStencilDescription{}), "depth_stencil_state");
  const Ref<BlendState> blend = take(device->blend_state(BlendDescription{}), "blend_state");
  const GraphicsPipelineDescription pipeline =
      fixture_pipeline_description(fixtures, *rasterizer, *depth_stencil, *blend);
  const GraphicsPipelineResult missed = device->graphics_pipeline(pipeline);
  require(!missed.ok() && missed.error() == PipelineError::CacheMiss,
          "an unseen description under the Error policy is CacheMiss");
  const Status<PipelineError> precompiled = device->precompile(std::span{&pipeline, 1}, {});
  require(precompiled.ok(), "precompile creates the description");
  const Ref<GraphicsPipeline> found =
      take(device->graphics_pipeline(pipeline), "graphics_pipeline");
  require(found == device->pipeline_cache().find_graphics(graphics_pipeline_key(pipeline)),
          "the lookup returns the precompiled pipeline");
}

// (device.hpp, Device::precompile: precompile creates in order and stops at
// the first failure with its code and the description's debug name).
void precompile_stops_at_first_failure(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device();
  const Fixtures fixtures = load_fixtures(artifacts);
  const Ref<RasterizerState> rasterizer =
      take(device->rasterizer_state(RasterizerDescription{}), "rasterizer_state");
  const Ref<DepthStencilState> depth_stencil =
      take(device->depth_stencil_state(DepthStencilDescription{}), "depth_stencil_state");
  const Ref<BlendState> blend = take(device->blend_state(BlendDescription{}), "blend_state");
  std::array<GraphicsPipelineDescription, 3> descriptions{};
  descriptions[0] = fixture_pipeline_description(fixtures, *rasterizer, *depth_stencil, *blend);
  descriptions[0].debug_name = "first valid";
  descriptions[1] = descriptions[0];
  descriptions[1].vertex_shader = nullptr;
  descriptions[1].debug_name = "missing vertex shader";
  descriptions[2] = descriptions[0];
  descriptions[2].primitive = PrimitiveType::TriangleStrip;
  descriptions[2].debug_name = "third valid";
  const Status<PipelineError> result = device->precompile(descriptions, {});
  require(!result.ok() && result.error() == PipelineError::MissingVertexShader,
          "precompile returns the second description's failure");
  require(result.detail() == "missing vertex shader", "the detail names the failing description");
  require(static_cast<bool>(
              device->pipeline_cache().find_graphics(graphics_pipeline_key(descriptions[0]))),
          "the first description was created");
  require(!device->pipeline_cache().find_graphics(graphics_pipeline_key(descriptions[2])),
          "the third description was not created");
}

// (resources.hpp, Buffer: a buffer created with initial data holds it when
// creation returns) read back through (synchronization.hpp, StagingBuffer:
// copy_to_staging, the fence, the lock).
void buffer_initial_data_is_uploaded(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  std::vector<std::byte> bytes(256);
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    bytes[i] = static_cast<std::byte>((i * 7U + 3U) & 0xFFU);
  }
  BufferDescription description{};
  description.size = bytes.size();
  description.usage = BufferUsage::CopySource;
  description.initial_access = Access::CopySource;
  description.debug_name = "initial data";
  const Ref<Buffer> buffer = take(device->create_buffer(description, bytes), "create_buffer");
  const Ref<StagingBuffer> staging =
      take(device->create_staging_buffer(bytes.size(), "initial data readback"),
           "create_staging_buffer");
  const Ref<GpuFence> fence = take(device->create_fence("initial data fence"), "create_fence");
  CommandList list;
  list.copy_buffer_to_staging(*buffer, *staging, BufferCopy{0, 0, bytes.size()});
  list.write_fence(*fence);
  require(device->execute(list).ok(), "execute succeeds");
  static_cast<void>(device->end_frame());
  const StagingLock lock =
      take(device->lock_staging(*staging, *fence, WaitPolicy::Block), "lock_staging(Block)");
  require(lock.bytes.size() == bytes.size(), "the lock spans the staging buffer");
  for (std::size_t i = 0; i < bytes.size(); ++i) {
    require(lock.bytes[i] == bytes[i], "every byte read back equals the initial data");
  }
  require(device->unlock_staging(*staging) == StagingError::None, "unlock_staging");
}

// (pipeline_states.hpp, PipelineError::VertexLayoutUnsupported: the Metal
// backend creates pipelines with an empty vertex layout only and returns
// VertexLayoutUnsupported for a non-empty one).
void vertex_layout_is_unsupported_on_metal(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const Fixtures fixtures = load_fixtures(artifacts);
  const Ref<RasterizerState> rasterizer =
      take(device->rasterizer_state(RasterizerDescription{}), "rasterizer_state");
  const Ref<DepthStencilState> depth_stencil =
      take(device->depth_stencil_state(DepthStencilDescription{}), "depth_stencil_state");
  const Ref<BlendState> blend = take(device->blend_state(BlendDescription{}), "blend_state");
  GraphicsPipelineDescription description =
      fixture_pipeline_description(fixtures, *rasterizer, *depth_stencil, *blend);
  description.vertex_layout.attributes[0] = VertexAttribute{0, Format::R32G32B32Float, 0, 0};
  description.vertex_layout.attribute_count = 1;
  description.vertex_layout.buffers[0] = VertexBufferBinding{12, VertexInputRate::PerVertex};
  description.vertex_layout.buffer_count = 1;
  const GraphicsPipelineResult result = device->graphics_pipeline(description);
  require(!result.ok() && result.error() == PipelineError::VertexLayoutUnsupported,
          "a non-empty vertex layout is VertexLayoutUnsupported");
}

// (pipeline_states.hpp, PipelineError::ShaderLibraryRejected) and
// (pipeline_states.hpp, PipelineError::EntryPointNotFound): the backend
// returns ShaderLibraryRejected when the API rejects the shader library and
// EntryPointNotFound when the function is not in it. The faulty artifacts
// keep the fixture's input hash, so this device creates no valid pipeline
// first: the description cache would otherwise answer from its entry.
void pipeline_library_errors_named(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const Fixtures fixtures = load_fixtures(artifacts);
  const Ref<RasterizerState> rasterizer =
      take(device->rasterizer_state(RasterizerDescription{}), "rasterizer_state");
  const Ref<DepthStencilState> depth_stencil =
      take(device->depth_stencil_state(DepthStencilDescription{}), "depth_stencil_state");
  const Ref<BlendState> blend = take(device->blend_state(BlendDescription{}), "blend_state");

  std::vector<std::byte> corrupted(fixtures.vertex->artifact.code.begin(),
                                   fixtures.vertex->artifact.code.end());
  for (std::size_t i = 0; i < 16 && i < corrupted.size(); ++i) {
    corrupted[i] = std::byte{0};
  }
  bw::shaders::Artifact bad_library = fixtures.vertex->artifact;
  bad_library.code = corrupted;
  GraphicsPipelineDescription description =
      fixture_pipeline_description(fixtures, *rasterizer, *depth_stencil, *blend);
  description.vertex_shader = &bad_library;
  const GraphicsPipelineResult rejected = device->graphics_pipeline(description);
  require(!rejected.ok() && rejected.error() == PipelineError::ShaderLibraryRejected,
          "corrupted library bytes are ShaderLibraryRejected");

  bw::shaders::Artifact missing_entry = fixtures.vertex->artifact;
  missing_entry.header.entry_point = "no_such_entry_point";
  description.vertex_shader = &missing_entry;
  const GraphicsPipelineResult missing = device->graphics_pipeline(description);
  require(!missing.ok() && missing.error() == PipelineError::EntryPointNotFound,
          "an entry point the library lacks is EntryPointNotFound");
}

// ---- Viewports ------------------------------------------------------------------------
//
// The viewport scenarios open a real window in this process; locals declared
// window, device, viewport tear down in reverse. Like every slice scenario
// they use the default reporter, so any validation failure aborts the run.

// A small window, so no screen clamps its pixel size, pumped until shown.
std::unique_ptr<bw::platform::Window> open_window(std::string_view title) {
  bw::platform::WindowDescription description{};
  description.title = title;
  description.width = 320;
  description.height = 200;
  bw::platform::WindowError error = bw::platform::WindowError::SystemRefused;
  std::unique_ptr<bw::platform::Window> window =
      bw::platform::macos::create_window(description, error);
  require(error == bw::platform::WindowError::None && window != nullptr, "create_window succeeds");
  for (int i = 0; i < 3; ++i) {
    window->pump();
    bw::platform::WindowEvent event{};
    while (window->poll_event(event)) {
    }
  }
  return window;
}

ViewportDescription window_viewport(const bw::platform::Window& window, std::string_view name) {
  ViewportDescription description{};
  description.window = window.native_handle();
  description.pixel_size = Extent2{window.pixel_size().width, window.pixel_size().height};
  description.debug_name = name;
  return description;
}

bool extent_is(const Texture& texture, Extent2 size) {
  const Extent3 extent = texture.description().extent;
  return extent.width == size.width && extent.height == size.height && extent.depth == 1U;
}

void transition(CommandList& list, Texture& texture, Access before, Access after) {
  const Transition one{&texture, before, after};
  list.transition(TransitionBatch{std::span<const Transition>{&one, 1}});
}

void require_executes(Device& device, CommandList& list) {
  const Status<ExecuteError> executed = device.execute(list);
  require(executed.ok(), "execute succeeds: code " +
                             std::to_string(static_cast<int>(executed.error())) + " " +
                             std::string(executed.detail()));
}

// The back buffer from Present to RenderTarget, a clear pass, and back.
void record_clear_frame(CommandList& list, Texture& back_buffer) {
  transition(list, back_buffer, Access::Present, Access::RenderTarget);
  list.begin_render_pass(clear_pass(back_buffer, "viewport clear"));
  list.end_render_pass();
  transition(list, back_buffer, Access::RenderTarget, Access::Present);
}

// One presented clear frame; the back buffer is reread, as resize replaces it.
void present_clear_frame(Device& device, Viewport& viewport, bw::platform::Window& window) {
  CommandList list;
  record_clear_frame(list, viewport.back_buffer());
  require_executes(device, list);
  static_cast<void>(device.end_frame());
  window.pump();
  bw::platform::WindowEvent event{};
  while (window.poll_event(event)) {
  }
}

// (device.hpp, create_viewport, resize_viewport; viewport.hpp, invariants 2,
// 3 and 5; resources.hpp, Viewable::tracked_access): at creation and after a
// resize the back buffer is a texture of the viewport's size tracked in
// Present, and frames transitioned back to Present before end_frame present
// with no validation failure.
void viewport_present_resize_present(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<bw::platform::Window> window = open_window("viewport present resize");
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const ViewportDescription description = window_viewport(*window, "present viewport");
  const Ref<Viewport> viewport = take(device->create_viewport(description), "create_viewport");
  const Extent2 resized{description.pixel_size.width / 2U, description.pixel_size.height / 2U};
  for (const Extent2 size : {description.pixel_size, resized}) {
    if (size == resized) {
      require(device->resize_viewport(*viewport, resized).ok(),
              "resize_viewport between frames succeeds");
    }
    require(viewport->size() == size, "the viewport has the window's or the new size");
    require(extent_is(viewport->back_buffer(), size), "the back buffer has the viewport's size");
    require(viewport->back_buffer().tracked_access().access == Access::Present,
            "the back buffer is tracked in Present at creation and after the resize");
    for (int frame = 0; frame < 3; ++frame) {
      present_clear_frame(*device, *viewport, *window);
    }
  }
  require(extent_is(viewport->back_buffer(), resized),
          "the back buffer keeps the new size across frames");
}

// The device's viewport wiring, one probe each (the check order is
// validate_viewport's unit test). (device.hpp, Viewports): another backend's
// surface kind is SurfaceKindUnsupported, named by the description's debug
// name. (device.hpp, resize_viewport; viewport.hpp, invariant 5): a resize
// while the back buffer is acquired in the frame is ResizeDuringFrame, named
// by the viewport's debug name, and changes nothing.
void viewport_errors_named(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<bw::platform::Window> window = open_window("viewport errors");
  const std::unique_ptr<Device> device = make_device(create_on_miss());

  // The kind after MetalLayer stands for a later backend's surface; the
  // enumeration has a fixed underlying type, so the value is well defined.
  ViewportDescription other_kind = window_viewport(*window, "other kind");
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  other_kind.window.kind = static_cast<bw::platform::NativeSurfaceKind>(
      static_cast<std::uint8_t>(bw::platform::NativeSurfaceKind::MetalLayer) + 1U);
  const ViewportResult kind = device->create_viewport(other_kind);
  require(!kind.ok() && kind.error() == ViewportError::SurfaceKindUnsupported,
          "a surface kind other than the backend's is SurfaceKindUnsupported");
  require(kind.detail() == other_kind.debug_name,
          "the refusal carries the description's debug name");

  const Ref<Viewport> viewport = take(
      device->create_viewport(window_viewport(*window, "resize during frame")), "create_viewport");
  const Extent2 size = viewport->size();
  Texture* const back_buffer = &viewport->back_buffer();
  CommandList list;
  record_clear_frame(list, *back_buffer);
  require_executes(*device, list);
  const Status<ViewportError> resize =
      device->resize_viewport(*viewport, Extent2{size.width / 2U, size.height / 2U});
  require(resize.error() == ViewportError::ResizeDuringFrame,
          "a resize while the back buffer is acquired in the frame is ResizeDuringFrame");
  require(resize.detail() == viewport->description().debug_name,
          "the refusal carries the viewport's debug name");
  require(viewport->size() == size && &viewport->back_buffer() == back_buffer,
          "the refused resize changes nothing");
  static_cast<void>(device->end_frame());
}

// ---- Shared draw and readback pieces for the slice's scenarios -----------------------

// Little-endian loads and stores, the order every target's raw buffer load reads.
void put_u16(std::vector<std::byte>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::byte>(value & 0xFFU);
  bytes[offset + 1U] = static_cast<std::byte>(value >> 8U);
}
void put_f32(std::vector<std::byte>& bytes, std::size_t offset, float value) {
  const auto bits = std::bit_cast<std::uint32_t>(value);
  put_u16(bytes, offset, static_cast<std::uint16_t>(bits & 0xFFFFU));
  put_u16(bytes, offset + 2U, static_cast<std::uint16_t>(bits >> 16U));
}
std::uint32_t get_u32(std::span<const std::byte> bytes, std::size_t offset) {
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (8U * i);
  }
  return value;
}

// A render target that is also a copy source, cleared to `clear`.
Ref<Texture> make_target(Device& device, Extent3 extent, Format format, std::array<float, 4> clear,
                         std::string_view name) {
  TextureDescription description = target_description(clear, name);
  description.extent = extent;
  description.format = format;
  return take(device.create_texture(description), "create_texture");
}

// A pipeline of `fixtures` into one `format` target, depth off, culling
// `cull`; the pipeline holds its state objects.
Ref<GraphicsPipeline> make_pipeline(Device& device, const Fixtures& fixtures, Format format,
                                    CullMode cull) {
  RasterizerDescription rasterizer{};
  rasterizer.cull = cull;
  DepthStencilDescription depth_off{};
  depth_off.depth_write = false;
  depth_off.depth_test = CompareFunction::Always;
  const Ref<RasterizerState> rasterizer_state =
      take(device.rasterizer_state(rasterizer), "rasterizer_state");
  const Ref<DepthStencilState> depth_stencil =
      take(device.depth_stencil_state(depth_off), "depth_stencil_state");
  const Ref<BlendState> blend = take(device.blend_state(BlendDescription{}), "blend_state");
  GraphicsPipelineDescription description =
      fixture_pipeline_description(fixtures, *rasterizer_state, *depth_stencil, *blend);
  description.targets.color_formats[0] = format;
  return take(device.graphics_pipeline(description), "graphics_pipeline");
}

// Opens a clear pass on `target` with `pipeline`, viewport and scissor `rect`
// and `block`; the caller draws and ends the pass.
template <class Block>
void begin_draw(CommandList& list, Texture& target, GraphicsPipeline& pipeline, ScissorRect rect,
                const Block& block) {
  list.begin_render_pass(clear_pass(target, "draw"));
  list.set_graphics_pipeline(pipeline);
  list.set_viewport(ViewportRect{static_cast<float>(rect.x), static_cast<float>(rect.y),
                                 static_cast<float>(rect.width), static_cast<float>(rect.height),
                                 0.0F, 1.0F});
  list.set_scissor(rect);
  list.set_draw_block(block);
}

// Mip 0 of `target` staged after the commands in `list`: RenderTarget to
// CopySource, the copy and a fence, then a transition to `after` unless it is
// CopySource; executed, ended, locked blocking and copied out before unlock.
struct StagedTexture {
  std::vector<std::byte> bytes;
  StagingLayout layout{};
};
StagedTexture stage_texture(Device& device, CommandList& list, Texture& target,
                            Access after = Access::CopySource) {
  StagedTexture staged{};
  staged.layout = staging_layout(target.description(), 0);
  const Ref<StagingBuffer> staging =
      take(device.create_staging_buffer(staged.layout.total_bytes, "readback"), "staging buffer");
  const Ref<GpuFence> fence = take(device.create_fence("readback fence"), "create_fence");
  transition(list, target, Access::RenderTarget, Access::CopySource);
  list.copy_texture_to_staging(target, 0, 0, *staging, 0);
  list.write_fence(*fence);
  if (after != Access::CopySource) {
    transition(list, target, Access::CopySource, after);
  }
  require_executes(device, list);
  static_cast<void>(device.end_frame());
  const StagingLock lock =
      take(device.lock_staging(*staging, *fence, WaitPolicy::Block), "lock_staging(Block)");
  require(lock.bytes.size() == staged.layout.total_bytes, "the lock spans the staging buffer");
  staged.bytes.assign(lock.bytes.begin(), lock.bytes.end());
  require(device.unlock_staging(*staging) == StagingError::None, "unlock_staging");
  return staged;
}

// Texel (x, y) of a staged RGBA8-sized target is `expected` within one unit.
void require_texel(const StagedTexture& staged, std::uint32_t x, std::uint32_t y,
                   const std::array<long, 4>& expected, std::string_view what) {
  const std::size_t offset = std::size_t{y} * staged.layout.row_pitch + std::size_t{x} * 4U;
  for (std::size_t c = 0; c < 4; ++c) {
    const auto actual = static_cast<long>(staged.bytes[offset + c]);
    if (actual - expected[c] < -1 || actual - expected[c] > 1) {
      throw std::runtime_error("texel (" + std::to_string(x) + ", " + std::to_string(y) +
                               ") channel " + std::to_string(c) + " is " + std::to_string(actual) +
                               ", not " + std::string(what) + " " + std::to_string(expected[c]));
    }
  }
}

// A linear [0, 1] value as the 8-bit sRGB encoding an sRGB target stores.
long srgb8(float linear) {
  const float encoded =
      linear <= 0.0031308F ? linear * 12.92F : 1.055F * std::pow(linear, 1.0F / 2.4F) - 0.055F;
  return std::lround(encoded * 255.0F);
}

// (viewport.hpp, invariant 2): the back buffer is a copy source. The fixture
// triangle drawn into (8, 8, 32, 32), copied to staging between CopySource
// and Present: inside it the fixture color, outside the back buffer's clear
// value, opaque black; B8G8R8A8UnormSrgb stores blue, green, red, alpha with
// color sRGB-encoded.
void back_buffer_copies_to_staging(const std::filesystem::path& artifacts) {
  const std::unique_ptr<bw::platform::Window> window = open_window("back buffer copy");
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const TriangleScene scene = make_triangle_scene(*device, artifacts);
  const Ref<GraphicsPipeline> pipeline =
      make_pipeline(*device, scene.fixtures, Format::B8G8R8A8UnormSrgb, CullMode::None);
  const Ref<Viewport> viewport =
      take(device->create_viewport(window_viewport(*window, "copy viewport")), "create_viewport");
  Texture& back_buffer = viewport->back_buffer();
  require(back_buffer.description().extent.width > 64U &&
              back_buffer.description().extent.height > 64U,
          "the back buffer is larger than the probed texels");
  CommandList list;
  transition(list, back_buffer, Access::Present, Access::RenderTarget);
  begin_draw(list, back_buffer, *pipeline, ScissorRect{8, 8, 32, 32}, fixture_draw_block(scene));
  list.draw(DrawArguments{3, 1, 0, 0});
  list.end_render_pass();
  const StagedTexture staged = stage_texture(*device, list, back_buffer, Access::Present);
  require_texel(staged, 20, 20, {srgb8(0.25F), srgb8(0.5F), srgb8(1.0F), 255},
                "the fixture color in BGRA sRGB");
  require_texel(staged, 60, 60, {0, 0, 0, 255}, "opaque black");
}

// ---- Vertex pulling ------------------------------------------------------------------
//
// The decode probe writes one attribute of one vertex into one pixel of a
// one-row float4 target; the cube draws into an RGBA8 target. Every draw
// validates its layout and range first, as vertex_streams.hpp requires.

using bw::geometry::VertexAttribute;
using bw::geometry::VertexAttributeDescription;
using bw::geometry::VertexElementType;
using bw::geometry::VertexStreamError;
using bw::geometry::VertexStreamLayout;

// A pulled-vertex draw on a fresh device: `shader` and vertex_color into a
// width x height `format` target; `vertices` read from `base_vertex`, `count`
// drawn from first vertex 0, through the 16-bit `indices` when there are any.
struct PulledDraw {
  std::string_view shader;
  bw::shaders::PermutationId permutation{};
  Format format{};
  CullMode cull{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::array<float, 4> clear{};
  VertexStreamLayout layout{};
  std::array<std::vector<std::byte>, bw::geometry::kMaxVertexStreams> streams{};
  std::uint32_t base_vertex{};
  std::uint32_t vertices{};
  std::uint32_t count{};
  std::vector<std::uint16_t> indices;
};

// Validates the layout and the vertex range, records `block` with its
// VertexStreams filled, and returns the staged target.
template <class Block>
StagedTexture draw_pulled(const std::filesystem::path& artifacts, const PulledDraw& draw,
                          Block block) {
  require(bw::geometry::validate_vertex_stream_layout(draw.layout) == VertexStreamError::None,
          "the draw's layout validates");
  const std::array<std::uint64_t, 2> stream_bytes{draw.streams[0].size(), draw.streams[1].size()};
  require(bw::geometry::validate_vertex_range(
              draw.layout, std::span(stream_bytes).first(draw.layout.stream_count),
              draw.base_vertex, draw.vertices) == VertexStreamError::None,
          "the draw's vertex range is within the stream buffers");
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const Fixtures fixtures{load_fixture(artifacts, draw.shader, draw.permutation),
                          load_fixture(artifacts, bw::geometry::kVertexColorShader)};
  const Ref<GraphicsPipeline> pipeline = make_pipeline(*device, fixtures, draw.format, draw.cull);
  // Each stream a buffer with initial data reached through a raw view.
  std::array<Ref<Buffer>, bw::geometry::kMaxVertexStreams> buffers{};
  std::array<Ref<ShaderResourceView>, bw::geometry::kMaxVertexStreams> views{};
  std::array<bw::geometry::VertexStreamBinding, bw::geometry::kMaxVertexStreams> bindings{};
  for (std::size_t s = 0; s < draw.layout.stream_count; ++s) {
    BufferDescription description{};
    description.size = draw.streams[s].size();
    description.usage = BufferUsage::ShaderResource;
    description.initial_access = Access::ShaderResourceNonPixel;
    description.debug_name = s == 0U ? "stream 0" : "stream 1";
    buffers[s] = take(device->create_buffer(description, draw.streams[s]), "create a stream");
    views[s] = take(device->create_shader_resource_view(buffers[s], BufferViewDescription{}),
                    "create a stream's view");
    bindings[s] = bw::geometry::VertexStreamBinding{
        views[s]->buffer_index(),
        static_cast<std::uint32_t>(draw.streams[s].size() / draw.layout.streams[s].stride)};
  }
  block.streams = bw::geometry::make_vertex_streams(
      draw.layout, std::span(bindings).first(draw.layout.stream_count), draw.base_vertex);
  const Ref<Texture> target = make_target(*device, Extent3{draw.width, draw.height, 1}, draw.format,
                                          draw.clear, "pulled draw target");
  CommandList list;
  begin_draw(list, *target, *pipeline, ScissorRect{0, 0, draw.width, draw.height}, block);
  Ref<Buffer> indices;
  if (draw.indices.empty()) {
    list.draw(DrawArguments{draw.count, 1, 0, 0});
  } else {
    BufferDescription description{};
    description.size = draw.indices.size() * sizeof(std::uint16_t);
    description.stride = sizeof(std::uint16_t);
    description.usage = BufferUsage::Index;
    description.initial_access = Access::VertexOrIndexBuffer;
    description.debug_name = "indices";
    indices = take(device->create_buffer(description, std::as_bytes(std::span(draw.indices))),
                   "create the index buffer");
    list.set_index_buffer(*indices, IndexFormat::Uint16, 0);
    list.draw_indexed(DrawIndexedArguments{draw.count, 1, 0, 0, 0});
  }
  list.end_render_pass();
  return stage_texture(*device, list, *target);
}

// A decode probe draw and its block: the attribute slot and the probe count.
struct DecodeDraw {
  PulledDraw pulled;
  VertexDecodeProbeDraw block{};
};

using DecodeType = bw::shaders::EnumDimension<"BW_DECODE_TYPE", VertexElementType,
                                              bw::geometry::kVertexElementTypeCount>;

// kProbeCount probes of `attribute` as `type` at byte 4 of stream 0, four 0xA5
// filler bytes either side so a wrong offset or stride decodes a rejected
// value; `vertices` vertices written by `write(bytes, offset, vertex)`; the
// target cleared to -9, which no probe decodes; 6 * kProbeCount drawn.
constexpr std::uint32_t kProbeCount = 12;
constexpr std::uint32_t kProbeOffset = 4;
template <class Write>
DecodeDraw single_stream_draw(VertexAttribute attribute, VertexElementType type,
                              std::uint32_t vertices, Write&& write) {
  bw::shaders::PermutationDomain<DecodeType> domain{};
  domain.set<DecodeType>(type);
  const std::uint32_t stride = kProbeOffset + bw::geometry::element_bytes(type) + 4U;
  constexpr std::array<float, 4> kUncovered{-9.0F, -9.0F, -9.0F, -9.0F};
  DecodeDraw draw{PulledDraw{bw::geometry::kVertexDecodeProbeShader, domain.id(),
                             Format::R32G32B32A32Float, CullMode::None, kProbeCount, 1, kUncovered,
                             VertexStreamLayout{}, std::array<std::vector<std::byte>, 2>{}, 0,
                             kProbeCount, 6U * kProbeCount, std::vector<std::uint16_t>{}},
                  VertexDecodeProbeDraw{}};
  draw.pulled.layout.attributes[0] = VertexAttributeDescription{attribute, type, 0, kProbeOffset};
  draw.pulled.layout.attribute_count = 1;
  draw.pulled.layout.streams[0].stride = stride;
  draw.pulled.layout.stream_count = 1;
  draw.pulled.streams[0].assign(std::size_t{vertices} * stride, std::byte{0xA5});
  draw.block.attribute = static_cast<std::uint32_t>(attribute);
  draw.block.width = kProbeCount;
  for (std::uint32_t i = 0; i < vertices; ++i) {
    write(draw.pulled.streams[0], std::size_t{i} * stride + kProbeOffset, i);
  }
  return draw;
}

// Probe p decodes `expected(p)` within `tolerance` (0 is exact) in all lanes.
template <class Expected>
void require_decoded(const std::filesystem::path& artifacts, const DecodeDraw& draw,
                     Expected&& expected, float tolerance, std::string_view what) {
  const StagedTexture staged = draw_pulled(artifacts, draw.pulled, draw.block);
  for (std::uint32_t p = 0; p < draw.block.width; ++p) {
    const std::array<float, 4> want = expected(p);
    for (std::size_t lane = 0; lane < 4; ++lane) {
      const auto actual =
          std::bit_cast<float>(get_u32(staged.bytes, std::size_t{p} * 16U + lane * 4U));
      if (!(std::fabs(actual - want[lane]) <= tolerance)) {
        throw std::runtime_error("probe " + std::to_string(p) + " lane " + std::to_string(lane) +
                                 " is " + std::to_string(actual) + ", not " + std::string(what) +
                                 " " + std::to_string(want[lane]));
      }
    }
  }
}

// Vertex i's float3 (w 0): negative, large and fractional, never zero or subnormal.
std::array<float, 4> float3_value(std::uint32_t i) {
  const auto f = static_cast<float>(i);
  return {-3.25F + 1.5F * f, 1000.125F * (f + 1.0F), 0.1F * (f + 1.0F) - 0.75F, 0.0F};
}
void write_float3(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t i) {
  for (std::size_t c = 0; c < 3; ++c) {
    put_f32(bytes, at + c * 4U, float3_value(i)[c]);
  }
}

// The short4n lanes cycle through the boundary values -32768, -32767, 0 and 32767.
constexpr std::array<std::int16_t, 8> kShortValues{-32768, -32767, -16384, -1, 0, 1, 16384, 32767};
std::int16_t short_lane(std::uint32_t i, std::size_t lane) {
  return kShortValues[(i + lane * 3U) % kShortValues.size()];
}
float snorm16(std::int16_t value) { return std::max(-1.0F, static_cast<float>(value) / 32767.0F); }
void write_short4n(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t i) {
  for (std::size_t lane = 0; lane < 4; ++lane) {
    put_u16(bytes, at + lane * 2U, static_cast<std::uint16_t>(short_lane(i, lane)));
  }
}
std::array<float, 4> short4n_value(std::uint32_t i) {
  return {snorm16(short_lane(i, 0)), snorm16(short_lane(i, 1)), snorm16(short_lane(i, 2)),
          snorm16(short_lane(i, 3))};
}

// Halves exactly representable as floats, the extremes included: bits, values.
constexpr std::array<std::uint16_t, 8> kHalfBits{0x0000, 0x3C00, 0xC100, 0x3555,
                                                 0x7BFF, 0x0400, 0x63D1, 0xC7C0};
constexpr std::array<float, 8> kHalfValues{
    0.0F, 1.0F, -2.5F, 0.333251953125F, 65504.0F, 6.103515625e-05F, 1000.5F, -7.75F};
std::size_t half_lane(std::uint32_t i, std::size_t lane) { return (i + lane * 5U) % 8U; }

// The ubyte lanes cycle through 0, 1, 127, 128 and 255 among others.
constexpr std::array<std::uint8_t, 8> kByteValues{0, 1, 127, 128, 255, 2, 64, 200};
std::uint8_t byte_lane(std::uint32_t i, std::size_t lane) {
  return kByteValues[(i + lane * 3U) % kByteValues.size()];
}
void write_bytes4(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t i) {
  for (std::size_t lane = 0; lane < 4; ++lane) {
    bytes[at + lane] = static_cast<std::byte>(byte_lane(i, lane));
  }
}
std::array<float, 4> bytes4_value(std::uint32_t i, float scale) {
  return {static_cast<float>(byte_lane(i, 0)) * scale, static_cast<float>(byte_lane(i, 1)) * scale,
          static_cast<float>(byte_lane(i, 2)) * scale, static_cast<float>(byte_lane(i, 3)) * scale};
}

// (vertex_fetch.hlsli, element decode: float3, three floats as stored)
// through (pulled_mesh_fixture.hpp, VertexDecodeProbeDraw: the lanes the
// element type does not fill are 0): probe p is vertex p's float3, exactly, w 0.
void decode_float3(const std::filesystem::path& artifacts) {
  require_decoded(artifacts,
                  single_stream_draw(VertexAttribute::Position, VertexElementType::Float3,
                                     kProbeCount, write_float3),
                  float3_value, 0.0F, "the stored float3 with w 0");
}

// (vertex_fetch.hlsli, element decode: short4n, each max(-1, c / 32767))
// through (pulled_mesh_fixture.hpp, VertexDecodeProbeDraw): within 1/32767.
void decode_short4n(const std::filesystem::path& artifacts) {
  require_decoded(artifacts,
                  single_stream_draw(VertexAttribute::Normal, VertexElementType::Short4Norm,
                                     kProbeCount, write_short4n),
                  short4n_value, 1.0F / 32767.0F, "max(-1, c / 32767)");
}

// (vertex_fetch.hlsli, element decode: half2, low half first) through
// (pulled_mesh_fixture.hpp, VertexDecodeProbeDraw: a half2 in xy, zw 0): exact.
void decode_half2(const std::filesystem::path& artifacts) {
  const auto write = [](std::vector<std::byte>& bytes, std::size_t at, std::uint32_t i) {
    put_u16(bytes, at, kHalfBits[half_lane(i, 0)]);
    put_u16(bytes, at + 2U, kHalfBits[half_lane(i, 1)]);
  };
  require_decoded(
      artifacts,
      single_stream_draw(VertexAttribute::TexCoord0, VertexElementType::Half2, kProbeCount, write),
      [](std::uint32_t p) {
        return std::array<float, 4>{kHalfValues[half_lane(p, 0)], kHalfValues[half_lane(p, 1)],
                                    0.0F, 0.0F};
      },
      0.0F, "the stored halves exactly with zw 0");
}

// (vertex_fetch.hlsli, element decode: ubyte4, low byte first) through
// (pulled_mesh_fixture.hpp, VertexDecodeProbeDraw: a ubyte4 as float(uint)): exact.
void decode_ubyte4(const std::filesystem::path& artifacts) {
  require_decoded(
      artifacts,
      single_stream_draw(VertexAttribute::BoneSlots, VertexElementType::UByte4, kProbeCount,
                         write_bytes4),
      [](std::uint32_t p) { return bytes4_value(p, 1.0F); }, 0.0F, "the stored bytes as integers");
}

// (vertex_fetch.hlsli, element decode: ubyte4n, each c / 255) through
// (pulled_mesh_fixture.hpp, VertexDecodeProbeDraw): within half of 1/255.
void decode_ubyte4n(const std::filesystem::path& artifacts) {
  require_decoded(
      artifacts,
      single_stream_draw(VertexAttribute::Color0, VertexElementType::UByte4Norm, kProbeCount,
                         write_bytes4),
      [](std::uint32_t p) { return bytes4_value(p, 1.0F / 255.0F); }, 0.5F / 255.0F, "c / 255");
}

// (vertex_streams.hpp, VertexStreams buffer1, stride1; attribute_word: the
// stream in the high 16 bits): the short4n moves to stream 1 (byte 4, stride
// 16); stream 0 holds float3 positions at stride 12; the probe reads stream 1.
void decode_from_second_stream(const std::filesystem::path& artifacts) {
  DecodeDraw draw = single_stream_draw(VertexAttribute::Tangent, VertexElementType::Short4Norm,
                                       kProbeCount, write_short4n);
  VertexStreamLayout& layout = draw.pulled.layout;
  layout.attributes[0].stream = 1;
  layout.attributes[1] =
      VertexAttributeDescription{VertexAttribute::Position, VertexElementType::Float3, 0, 0};
  layout.attribute_count = 2;
  layout.streams[1] = layout.streams[0];
  layout.streams[0].stride = 12;
  layout.stream_count = 2;
  draw.pulled.streams[1] = std::move(draw.pulled.streams[0]);
  draw.pulled.streams[0].assign(std::size_t{kProbeCount} * 12U, std::byte{});
  for (std::uint32_t i = 0; i < kProbeCount; ++i) {
    write_float3(draw.pulled.streams[0], std::size_t{i} * 12U, i);
  }
  require_decoded(artifacts, draw, short4n_value, 1.0F / 32767.0F, "max(-1, c / 32767)");
}

// (vertex_streams.hpp, Vertex index: the API gets base vertex 0, the block the
// draw's base, the shader reads SV_VertexID + base_vertex): with block base
// kBase and first vertex 0, probe p reads vertex p + kBase (SV_VertexID from 0).
void decode_with_base_vertex(const std::filesystem::path& artifacts) {
  constexpr std::uint32_t kBase = 5;
  DecodeDraw draw = single_stream_draw(VertexAttribute::Position, VertexElementType::Float3,
                                       kBase + kProbeCount, write_float3);
  draw.pulled.base_vertex = kBase;
  require_decoded(
      artifacts, draw, [](std::uint32_t p) { return float3_value(p + kBase); }, 0.0F,
      "vertex p + base's float3");
}

// ---- The fixture cube against a software reference ------------------------------------

using Vec4 = std::array<float, 4>;
using Mat4 = std::array<Vec4, 4>;
using Vec3 = std::array<float, 3>;
using Point = std::array<float, 2>;

// Row vectors, as the shader's mul(float4(p, 1), world_to_clip): out = v * m.
Vec4 transform(const Vec4& v, const Mat4& m) {
  Vec4 out{};
  for (std::size_t j = 0; j < 4; ++j) {
    out[j] = v[0] * m[0][j] + v[1] * m[1][j] + v[2] * m[2][j] + v[3] * m[3][j];
  }
  return out;
}
float dot3(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
Vec3 normalized(const Vec3& v) {
  const float length = std::sqrt(dot3(v, v));
  return {v[0] / length, v[1] / length, v[2] / length};
}

// A right-handed view from (3.0, 2.4, 4.2) at the origin with +y up, times a
// perspective of 40 degrees, aspect 1, near 0.1, far 100, depth in [0, 1]; row
// r of view * projection is (v0 f, v1 f, v2 a + v3 b, -v2) for view row v.
// The camera sees the +x, +y and +z faces.
Mat4 cube_world_to_clip() {
  const Vec3 eye{3.0F, 2.4F, 4.2F};
  const Vec3 z = normalized(eye);
  const Vec3 x = normalized({z[2], 0.0F, -z[0]});
  const Vec3 y{z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]};
  const float f = 1.0F / std::tan(40.0F * 3.14159265358979F / 360.0F);
  const float a = 100.0F / (0.1F - 100.0F);
  const float b = 0.1F * 100.0F / (0.1F - 100.0F);
  Mat4 m{};
  for (std::size_t r = 0; r < 4; ++r) {
    const Vec4 v = r < 3U ? Vec4{x[r], y[r], z[r], 0.0F}
                          : Vec4{-dot3(x, eye), -dot3(y, eye), -dot3(z, eye), 1.0F};
    m[r] = {v[0] * f, v[1] * f, v[2] * a + v[3] * b, -v[2]};
  }
  return m;
}

// Distance from p to the segment ab, in pixels.
float segment_distance(const Point& p, const Point& a, const Point& b) {
  const Point ab{b[0] - a[0], b[1] - a[1]};
  const float length2 = ab[0] * ab[0] + ab[1] * ab[1];
  const float t =
      length2 > 0.0F
          ? std::clamp(((p[0] - a[0]) * ab[0] + (p[1] - a[1]) * ab[1]) / length2, 0.0F, 1.0F)
          : 0.0F;
  return std::hypot(p[0] - (a[0] + t * ab[0]), p[1] - (a[1] + t * ab[1]));
}

// (pulled_mesh_fixture.hpp, make_fixture_cube, PulledMeshDraw: clip position
// is the row vector (p, 1) times world_to_clip; COLOR0 = float4(color.rgb *
// shade, 1) with the header's shade and light, the normal in object space).
// Drawn indexed into a 128 x 128 RGBA8 target cleared to opaque black, culling
// clockwise, without depth, against a software reference of the 12 triangles:
// front-facing is counter-clockwise in device coordinates (x right, y up), the
// faces CullMode::Clockwise keeps (pipeline_states.hpp, CullMode); window x =
// (x + 1) / 2 * 128, y = (1 - y) / 2 * 128, texel centers at +0.5; a center in
// a front-facing triangle takes its shaded color, others the clear color,
// rounded to 8 bits. Texels within one texel of a projected edge are skipped,
// so the fill rule does not matter; the rest match within one unit, and each
// of the three visible faces covers at least 200 compared texels.
void cube_offscreen_matches_reference(const std::filesystem::path& artifacts) {
  constexpr std::uint32_t kSize = 128;
  const bw::geometry::FixtureCube cube = bw::geometry::make_fixture_cube();
  const Mat4 world_to_clip = cube_world_to_clip();
  PulledMeshDraw block{};
  for (std::size_t r = 0; r < 4; ++r) {
    block.world_to_clip.rows[r] = bw::shaders::Float4{world_to_clip[r][0], world_to_clip[r][1],
                                                      world_to_clip[r][2], world_to_clip[r][3]};
  }
  constexpr std::array<float, 4> kBlack{0.0F, 0.0F, 0.0F, 1.0F};
  const StagedTexture staged =
      draw_pulled(artifacts,
                  PulledDraw{bw::geometry::kPulledMeshShader, bw::shaders::PermutationId{},
                             Format::R8G8B8A8Unorm, CullMode::Clockwise, kSize, kSize, kBlack,
                             cube.layout, std::array{cube.stream0, cube.stream1}, 0,
                             cube.vertex_count, cube.index_count, cube.indices},
                  block);

  // The reference triangles: window corners, signed window-space area (y down,
  // so negative is counter-clockwise in device coordinates, front-facing),
  // shaded color and face normal.
  struct Triangle {
    std::array<Point, 3> window{};
    float area{};
    std::array<long, 4> rgba{};
    Vec3 normal{};
  };
  const Vec3 light = normalized({0.4F, 0.8F, 0.45F});
  std::array<Triangle, 12> triangles{};
  for (std::size_t t = 0; t < triangles.size(); ++t) {
    std::array<Point, 3>& w = triangles[t].window;
    for (std::size_t k = 0; k < 3; ++k) {
      const std::size_t v = cube.indices[t * 3U + k];
      const Vec4 clip = transform({std::bit_cast<float>(get_u32(cube.stream1, v * 12U)),
                                   std::bit_cast<float>(get_u32(cube.stream1, v * 12U + 4U)),
                                   std::bit_cast<float>(get_u32(cube.stream1, v * 12U + 8U)), 1.0F},
                                  world_to_clip);
      require(clip[3] > 0.0F && std::fabs(clip[0]) < clip[3] && std::fabs(clip[1]) < clip[3] &&
                  clip[2] > 0.0F && clip[2] < clip[3],
              "every cube vertex is inside the view volume, so nothing is clipped");
      w[k] = {(clip[0] / clip[3] + 1.0F) * 0.5F * static_cast<float>(kSize),
              (1.0F - clip[1] / clip[3]) * 0.5F * static_cast<float>(kSize)};
    }
    triangles[t].area =
        (w[1][0] - w[0][0]) * (w[2][1] - w[0][1]) - (w[2][0] - w[0][0]) * (w[1][1] - w[0][1]);
    const std::size_t first = std::size_t{cube.indices[t * 3U]} * 24U;
    for (std::size_t c = 0; c < 3; ++c) {
      triangles[t].normal[c] =
          snorm16(static_cast<std::int16_t>(get_u32(cube.stream0, first + c * 2U) & 0xFFFFU));
    }
    const float shade = 0.25F + 0.75F * std::clamp(dot3(triangles[t].normal, light), 0.0F, 1.0F);
    for (std::size_t c = 0; c < 3; ++c) {
      const float color = static_cast<float>(cube.stream0[first + 12U + c]) / 255.0F;
      triangles[t].rgba[c] = std::lround(color * shade * 255.0F);
    }
    triangles[t].rgba[3] = 255;
  }

  // A center is inside a triangle when it is on the same side of all three
  // edges, whatever the triangle's orientation in y-down window space.
  std::array<std::uint32_t, 12> compared{};
  for (std::uint32_t j = 0; j < kSize; ++j) {
    for (std::uint32_t i = 0; i < kSize; ++i) {
      const Point p{static_cast<float>(i) + 0.5F, static_cast<float>(j) + 0.5F};
      bool near_edge = false;
      std::array<bool, 12> inside{};
      for (std::size_t t = 0; t < triangles.size(); ++t) {
        std::array<float, 3> e{};
        for (std::size_t k = 0; k < 3; ++k) {
          const Point& a = triangles[t].window[k];
          const Point& b = triangles[t].window[(k + 1U) % 3U];
          e[k] = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]);
          near_edge = near_edge || segment_distance(p, a, b) <= 1.0F;
        }
        inside[t] = (e[0] > 0.0F && e[1] > 0.0F && e[2] > 0.0F) ||
                    (e[0] < 0.0F && e[1] < 0.0F && e[2] < 0.0F);
      }
      if (near_edge) {
        continue;
      }
      std::array<long, 4> expected{0, 0, 0, 255};
      for (std::size_t t = 0; t < triangles.size(); ++t) {
        if (triangles[t].area < 0.0F && inside[t]) {
          expected = triangles[t].rgba;
          ++compared[t];
        }
      }
      require_texel(staged, i, j, expected, "the reference");
    }
  }
  // A face is the two triangles sharing a normal, so three faces of at least
  // 200 compared texels count six triangles.
  std::uint32_t triangles_of_seen_faces = 0;
  for (const Triangle& t : triangles) {
    std::uint32_t face_texels = 0;
    for (std::size_t u = 0; u < triangles.size(); ++u) {
      face_texels += triangles[u].normal == t.normal ? compared[u] : 0U;
    }
    triangles_of_seen_faces += face_texels >= 200U ? 1U : 0U;
  }
  require(triangles_of_seen_faces == 6U, "three faces each cover at least 200 compared texels");
}

// ---- Indirect work ------------------------------------------------------------------

// The indirect fixtures (permutation.cpp, IndirectQuads and IndirectStore):
// indirect_quads draws quad q, 16 pixels wide at x 16 q, from indices
// [6 q, 6 q + 6) of kQuadIndices, colored ((i + 1) / 4, 1, 0, 1) with i the
// draw's index (bindless.hlsli, BW_DRAW_INDEX);
// indirect_store stores root constant 2 at byte root constant 1 + 4 g of the
// storage buffer root constant 0 names, once per thread group g.
constexpr std::array<std::uint16_t, 24> kQuadIndices{0, 1, 2,  2,  1, 3,  4,  5,  6,  6,  5,  7,
                                                     8, 9, 10, 10, 9, 11, 12, 13, 14, 14, 13, 15};
constexpr std::array<float, 4> kOpaqueBlack{0.0F, 0.0F, 0.0F, 1.0F};

struct IndirectScene {
  Fixtures quads;
  std::unique_ptr<LoadedArtifact> store;
  Ref<GraphicsPipeline> draw;
  Ref<ComputePipeline> compute;
  Ref<Buffer> indices;
  Ref<Texture> target;
};

IndirectScene make_indirect_scene(Device& device, const std::filesystem::path& artifacts) {
  IndirectScene scene{};
  scene.quads = Fixtures{load_fixture(artifacts, "indirect_quads"),
                         load_fixture(artifacts, bw::geometry::kVertexColorShader)};
  scene.store = load_fixture(artifacts, "indirect_store");
  scene.draw = make_pipeline(device, scene.quads, Format::R8G8B8A8Unorm, CullMode::None);
  scene.compute = take(
      device.compute_pipeline(ComputePipelineDescription{&scene.store->artifact, "indirect store"}),
      "compute_pipeline");
  BufferDescription description{};
  description.size = sizeof(kQuadIndices);
  description.stride = 2;
  description.usage = BufferUsage::Index;
  description.initial_access = Access::VertexOrIndexBuffer;
  description.debug_name = "quad indices";
  scene.indices = take(device.create_buffer(description, std::as_bytes(std::span(kQuadIndices))),
                       "create the quad indices");
  scene.target = make_target(device, Extent3{64, 64, 1}, Format::R8G8B8A8Unorm, kOpaqueBlack,
                             "indirect target");
  return scene;
}

// A buffer with `usage` in `access`, holding `words`.
Ref<Buffer> make_words(Device& device, std::span<const std::uint32_t> words, BufferUsage usage,
                       Access access, std::string_view name) {
  BufferDescription description{};
  description.size = words.size_bytes();
  description.usage = usage;
  description.initial_access = access;
  description.debug_name = name;
  return take(device.create_buffer(description, std::as_bytes(words)), "create_buffer");
}

// Records indirect_store over the storage buffer `view` names: words 0 to 2
// of the root constants as above.
void bind_store(CommandList& list, const IndirectScene& scene, const UnorderedAccessView& view,
                std::uint32_t offset, std::uint32_t value) {
  list.set_compute_pipeline(*scene.compute);
  const std::array<std::uint32_t, 3> constants{view.storage_buffer_index().value, offset, value};
  list.set_root_constants(0, constants);
}

void transition_buffer(CommandList& list, Buffer& buffer, Access before, Access after) {
  const Transition one{&buffer, before, after};
  list.transition(TransitionBatch{std::span<const Transition>{&one, 1}});
}

// Quad q of the staged target holds the color of the draw with index
// `draw_index`, or the clear color for kNotDrawn, at the texel in the middle
// of its column.
constexpr std::int32_t kNotDrawn = -1;
void require_quad(const StagedTexture& staged, std::uint32_t quad, std::int32_t draw_index) {
  const bool drawn = draw_index != kNotDrawn;
  const std::array<long, 4> color{
      drawn ? to_unorm8(static_cast<float>(draw_index + 1) * 0.25F).value : 0L, drawn ? 255L : 0L,
      0L, 255L};
  require_texel(staged, quad * 16U + 8U, 32U, color,
                "quad " + std::to_string(quad) +
                    (drawn ? " drawn with index " + std::to_string(draw_index) : " not drawn"));
}

// (command_list.hpp, Indirect work: draw_indexed_indirect_count draws
// min(n, maximum) times, draw i with record i, and reads nothing of a record
// at or past that number; the CPU never reads the count). Four records over
// a maximum of 4, the count buffer created holding 4 and overwritten with 2
// by a dispatch earlier in the same frame: records 0 and 1 (quads 2 and 0)
// are drawn, records 2 and 3 (quads 1 and 3) are not, so the count the GPU
// wrote was read and nothing past it was drawn. (command_list.hpp, Indirect
// work: a shader reads the index of its draw through the bindless include):
// quad 2 holds index 0's color and quad 0 index 1's, so the index reached
// the shader per draw.
void multi_draw_draws_count_records(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const IndirectScene scene = make_indirect_scene(*device, artifacts);
  constexpr std::array<std::uint32_t, 4> kRecordQuads{2, 0, 1, 3};
  std::array<IndirectDrawRecord, 4> records{};
  for (std::uint32_t i = 0; i < records.size(); ++i) {
    records[i] = IndirectDrawRecord{i, DrawIndexedArguments{6, 1, 6U * kRecordQuads[i], 0, 0}};
  }
  BufferDescription description{};
  description.size = sizeof(records);
  description.usage = BufferUsage::IndirectArguments;
  description.initial_access = Access::IndirectArguments;
  description.debug_name = "records";
  const Ref<Buffer> record_buffer =
      take(device->create_buffer(description, std::as_bytes(std::span(records))), "records");
  const std::array<std::uint32_t, 1> initial_count{4};
  const Ref<Buffer> count = make_words(
      *device, initial_count, BufferUsage::UnorderedAccess | BufferUsage::IndirectArguments,
      Access::UnorderedAccessCompute, "count");
  const Ref<UnorderedAccessView> count_view =
      take(device->create_unordered_access_view(count, BufferViewDescription{}), "count view");

  CommandList list;
  bind_store(list, scene, *count_view, 0, 2);
  list.dispatch(DispatchArguments{1, 1, 1});
  transition_buffer(list, *count, Access::UnorderedAccessCompute, Access::IndirectArguments);
  list.begin_render_pass(clear_pass(*scene.target, "multi-draw"));
  list.set_graphics_pipeline(*scene.draw);
  list.set_index_buffer(*scene.indices, IndexFormat::Uint16, 0);
  list.draw_indexed_indirect_count(*record_buffer, *count,
                                   MultiDrawArguments{0, kIndirectDrawRecordBytes, 0, 4});
  list.end_render_pass();
  const StagedTexture staged = stage_texture(*device, list, *scene.target);
  require_quad(staged, 2, 0);
  require_quad(staged, 0, 1);
  require_quad(staged, 1, kNotDrawn);
  require_quad(staged, 3, kNotDrawn);
  device->wait_idle();
}

// (command_list.hpp, Indirect work: draw_indexed_indirect draws once with the
// DrawIndexedArguments at `offset`; dispatch_indirect dispatches once with
// the DispatchArguments there). The arguments sit at offset 16 of buffers
// the CPU filled, so a backend that drew or dispatched with direct
// arguments, or read at offset 0, is caught: the draw covers quad 1 alone,
// and three thread groups store 7 into words 0 to 2 of a buffer whose word 3
// keeps its 0.
void indirect_single_draw_and_dispatch(const std::filesystem::path& artifacts) {
  const std::unique_ptr<Device> device = make_device(create_on_miss());
  const IndirectScene scene = make_indirect_scene(*device, artifacts);
  const std::array<std::uint32_t, 9> draw_words{0, 0, 0, 0, 6, 1, 6, 0, 0};
  const Ref<Buffer> draw_arguments = make_words(*device, draw_words, BufferUsage::IndirectArguments,
                                                Access::IndirectArguments, "draw args");
  const std::array<std::uint32_t, 7> dispatch_words{0, 0, 0, 0, 3, 1, 1};
  const Ref<Buffer> dispatch_arguments =
      make_words(*device, dispatch_words, BufferUsage::IndirectArguments, Access::IndirectArguments,
                 "dispatch args");
  const std::array<std::uint32_t, 4> zeros{};
  const Ref<Buffer> stored =
      make_words(*device, zeros, BufferUsage::UnorderedAccess | BufferUsage::CopySource,
                 Access::UnorderedAccessCompute, "stored");
  const Ref<UnorderedAccessView> stored_view =
      take(device->create_unordered_access_view(stored, BufferViewDescription{}), "stored view");
  const Ref<StagingBuffer> staging =
      take(device->create_staging_buffer(sizeof(zeros), "stored readback"), "staging");
  const Ref<GpuFence> fence = take(device->create_fence("stored fence"), "create_fence");

  CommandList list;
  bind_store(list, scene, *stored_view, 0, 7);
  list.dispatch_indirect(*dispatch_arguments, 16);
  transition_buffer(list, *stored, Access::UnorderedAccessCompute, Access::CopySource);
  list.copy_buffer_to_staging(*stored, *staging, BufferCopy{0, 0, sizeof(zeros)});
  list.write_fence(*fence);
  list.begin_render_pass(clear_pass(*scene.target, "indirect draw"));
  list.set_graphics_pipeline(*scene.draw);
  list.set_index_buffer(*scene.indices, IndexFormat::Uint16, 0);
  list.draw_indexed_indirect(*draw_arguments, 16);
  list.end_render_pass();
  const StagedTexture staged = stage_texture(*device, list, *scene.target);
  for (std::uint32_t quad = 0; quad < 4; ++quad) {
    require_quad(staged, quad, quad == 1U ? 0 : kNotDrawn);
  }
  const StagingLock lock =
      take(device->lock_staging(*staging, *fence, WaitPolicy::Block), "lock_staging(Block)");
  for (std::size_t word = 0; word < 4; ++word) {
    require(get_u32(lock.bytes, word * 4U) == (word < 3U ? 7U : 0U),
            "word " + std::to_string(word) + " holds what three groups store");
  }
  require(device->unlock_staging(*staging) == StagingError::None, "unlock_staging");
  device->wait_idle();
}

#if BW_RHI_DEBUG
// (backend.hpp, wrap_with_validation: the validation layer reports a failure
// once with the code, the resource's debug name and the command's name) and
// (transitions.hpp, validate_use: a copy source must be in CopySource at use;
// AccessMissingAtUse). The probe that Device::create wires the layer.
void validation_rejects_undeclared_transition_by_name(const std::filesystem::path& /*artifacts*/) {
  std::vector<CapturedFailure> failures;
  const std::unique_ptr<Device> device = make_device(DeviceDescription{}, &failures);
  BufferDescription description{};
  description.size = 256;
  description.usage = BufferUsage::CopySource | BufferUsage::CopyDestination;
  description.initial_access = Access::CopyDestination;
  description.debug_name = "undeclared source";
  const Ref<Buffer> source = take(device->create_buffer(description), "create the source");
  description.debug_name = "copy destination";
  const Ref<Buffer> destination =
      take(device->create_buffer(description), "create the destination");
  CommandList list;
  list.copy_buffer(*source, *destination, BufferCopy{0, 0, 256});
  require(device->execute(list).ok(), "execute succeeds");
  require(failures.size() == 1U, "exactly one validation failure is reported");
  const CapturedFailure& failure = failures.front();
  require(failure.code == ValidationCode::Transition, "the failure is a transition failure");
  require(failure.transition == TransitionError::AccessMissingAtUse,
          "the transition error is AccessMissingAtUse");
  require(failure.resource == "undeclared source", "the failure names the resource");
  require(failure.command == "copy_buffer", "the failure names the command");
  static_cast<void>(device->end_frame());
  device->wait_idle();
}
#endif

using Scenario = bw::testing::Scenario<std::filesystem::path>;

constexpr std::array kScenarios{
    Scenario{"device_creation_and_required_capabilities",
             &device_creation_and_required_capabilities},
    Scenario{"device_create_rejects_bad_description", &device_create_rejects_bad_description},
    Scenario{"clear_offscreen_and_read_back", &clear_offscreen_and_read_back},
    Scenario{"draw_fixture_triangle_and_read_back", &draw_fixture_triangle_and_read_back},
    Scenario{"release_mid_frame_destroys_after_completion",
             &release_mid_frame_destroys_after_completion},
    Scenario{"frames_in_flight_blocks_on_the_oldest", &frames_in_flight_blocks_on_the_oldest},
    Scenario{"fence_poll_false_then_true_and_wait_returns",
             &fence_poll_false_then_true_and_wait_returns},
    Scenario{"fence_wait_errors_named", &fence_wait_errors_named},
    Scenario{"occlusion_query_counts_covered_samples", &occlusion_query_counts_covered_samples},
    Scenario{"query_pool_errors_named", &query_pool_errors_named},
    Scenario{"transient_budget_exhaustion_is_named", &transient_budget_exhaustion_is_named},
    Scenario{"lock_buffer_errors_named", &lock_buffer_errors_named},
    Scenario{"view_of_null_resource_is_named", &view_of_null_resource_is_named},
    Scenario{"pipeline_miss_policy_error_returns_cache_miss",
             &pipeline_miss_policy_error_returns_cache_miss},
    Scenario{"precompile_stops_at_first_failure", &precompile_stops_at_first_failure},
    Scenario{"buffer_initial_data_is_uploaded", &buffer_initial_data_is_uploaded},
    Scenario{"vertex_layout_is_unsupported_on_metal", &vertex_layout_is_unsupported_on_metal},
    Scenario{"pipeline_library_errors_named", &pipeline_library_errors_named},
    Scenario{"viewport_present_resize_present", &viewport_present_resize_present},
    Scenario{"viewport_errors_named", &viewport_errors_named},
    Scenario{"back_buffer_copies_to_staging", &back_buffer_copies_to_staging},
    Scenario{"decode_float3", &decode_float3},
    Scenario{"decode_short4n", &decode_short4n},
    Scenario{"decode_half2", &decode_half2},
    Scenario{"decode_ubyte4", &decode_ubyte4},
    Scenario{"decode_ubyte4n", &decode_ubyte4n},
    Scenario{"decode_from_second_stream", &decode_from_second_stream},
    Scenario{"decode_with_base_vertex", &decode_with_base_vertex},
    Scenario{"cube_offscreen_matches_reference", &cube_offscreen_matches_reference},
    Scenario{"multi_draw.draws_count_records", &multi_draw_draws_count_records},
    Scenario{"indirect.single_draw_and_dispatch", &indirect_single_draw_and_dispatch},
#if BW_RHI_DEBUG
    Scenario{"validation_rejects_undeclared_transition_by_name",
             &validation_rejects_undeclared_transition_by_name},
#endif
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(argc, argv, kScenarios, "<metal artifact directory>",
                                   [](std::span<char* const> rest, std::filesystem::path& out) {
                                     out = rest.size() == 1U ? std::filesystem::path(rest[0])
                                                             : std::filesystem::path();
                                     return rest.size() == 1U;
                                   });
}
