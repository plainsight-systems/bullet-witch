// Integration test: the RHI on this Mac's Metal device (docs/architecture/rhi.md).
// Each scenario creates a Device through device.hpp with the Metal backend,
// drives it through the public headers only, and asserts an invariant a
// header states; the comment above each scenario names the lines. The shader
// artifacts are the ones the build produced for the fixture shaders
// (fullscreen_triangle permutation 0, fullscreen_triangle_color permutation 0).
//
// Arguments after the scenario name: the Metal artifact directory,
// ${CMAKE_BINARY_DIR}/shaders/metal.
//
// Every assertion holds whatever the GPU's timing: a state that is only
// observable while a frame is still in flight (PollOnly NotReady, a Submitted
// fence, EndFrameResult::waited true) is not asserted.
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
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
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

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

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

// Loads permutation 0 of a registry shader with the expectations the registry
// gives (artifact.hpp, load_artifact). The artifact views the bytes, so the
// result stays on the heap where the bytes do not move.
std::unique_ptr<LoadedArtifact> load_fixture(const std::filesystem::path& directory,
                                             std::string_view shader_name) {
  const bw::shaders::ShaderDescription* shader =
      bw::shaders::find_shader(bw::shaders::shader_registry(), shader_name);
  require(shader != nullptr, "the registry holds the fixture shader");
  const std::filesystem::path path =
      directory /
      bw::shaders::artifact_file_name(shader->name, bw::shaders::PermutationId{0}, shader->stage);
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
  list.set_viewport(Viewport{8.0F, 8.0F, 32.0F, 32.0F, 0.0F, 1.0F});
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

// device.hpp:35-41 (create checks the two required capabilities and creates
// the backend); backend.hpp:33-38 (the required entries are true) and
// :111-131 (the device's name; frames in flight, descriptor capacities and
// the transient budget are the budgets the backend was created with,
// :126-127).
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

// device.hpp:35-37 (create validates the description first and fails by
// name, creating no device). One probe of the wiring; the order of
// validate_device_description's codes is its unit test's.
void device_create_rejects_bad_description(const std::filesystem::path& /*artifacts*/) {
  DeviceDescription capacity{};
  capacity.descriptors.storage = 1;
  const DeviceResult bad_capacity = Device::create(capacity);
  require(!bad_capacity.ok() && bad_capacity.error() == DeviceError::DescriptorCapacityInvalid,
          "a storage capacity other than the root layout's is DescriptorCapacityInvalid");
}

// pipeline_states.hpp:490-492 (Clear writes the target texture's
// clear_value); command_list.hpp:62-64 (a transition is declared before the
// copy); synchronization.hpp:35-40 (Block waits on the fence first and
// reports that it waited; a second lock before unlock is AlreadyLocked) and
// :41-50 with :151-192 (the texture lands in the staging layout).
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

// device.hpp:50-52 and :191-192 and pipeline_states.hpp:29-42 (a pipeline
// from the fixture artifacts with an empty vertex layout);
// binding_structure.hpp:18-34 (the 160-byte top-level argument buffer: the
// draw block's address at 24 and the tables at 0, 8, 16, against a live
// draw that reads the draw block, the Texture2D array and the sampler
// array); command_list.hpp:48-61 (set_draw_block binds the block for the
// draw). Coverage: the viewport and scissor are (8, 8, 32, 32) and the
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

// device.hpp:60-66 (end_frame destroys the delete queue's entries at or
// below the completed frame and reports how many) and :67-68 (wait_idle
// runs the queue for every submitted frame); lifetime.hpp:21-30 (a release
// at zero enqueues; the entry is destroyed only once its frame has
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

// device.hpp:60-63 and lifetime.hpp:37-41 (end_frame blocks on the oldest
// frame when frames_in_flight frames are queued, and only then;
// EndFrameResult, :124-138). With three in flight the first two end_frames
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

// synchronization.hpp:21-28 (a written fence is Pending until its frame is
// submitted; poll is false before the GPU signals; wait returns after the
// signal; then the state is Signaled) and :32-34 (reset returns the fence to
// Unwritten); device.hpp:216 (wait_fence).
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

// synchronization.hpp:29-31 and :119-121 with device.hpp:214-216
// (wait_fence checks fence_wait_error first and rejects a fence whose
// write's frame is not submitted, without blocking). One probe of the
// wiring; fence_wait_error's mapping is its unit test's.
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

// queries.hpp:21-24 (an occlusion result is the number of samples that
// passed, not a boolean) and :31-38 (Block before the ending frame is
// submitted returns NotSubmitted without blocking; the value once that
// frame has completed; NotEnded for a query not ended); :89 and device.hpp
// query_result (IndexOutOfRange). The triangle covers 32 x 32 one-sample
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

// queries.hpp:17-20 (AbsoluteTime only with timestamp_queries, else
// KindUnsupported, the device's check) and :101-113 (validate_query_pool,
// probed once for its wiring with ZeroCount).
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

// command_list.hpp:67-72 and :93-98 (replay returns the first failure; the
// ring has the device's budget and never grows: TransientBudgetExhausted when
// the frame's ring is full, DrawBlockTooLarge for one block above the ring);
// backend.hpp:199-203. The blocks are set inside a pass with the fixture
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

// resources.hpp:48-50 (only an Upload buffer locks: validate_lock's wiring,
// probed with NotUploadBuffer) and :520-528 with device.hpp:180-181 (a
// second lock is AlreadyLocked; unlock without lock is NotLocked; the lock
// is a span of the range).
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

// resources.hpp:259-260 (a null Ref passed to a view creation is
// NullResource).
void view_of_null_resource_is_named(const std::filesystem::path& /*artifacts*/) {
  const std::unique_ptr<Device> device = make_device();
  const ShaderResourceViewResult view =
      device->create_shader_resource_view(Ref<Texture>{}, TextureViewDescription{});
  require(!view.ok() && view.error() == ResourceError::NullResource,
          "a view of a null texture is NullResource");
}

// device.hpp:50-56 (with PipelineMissPolicy::Error a miss returns CacheMiss;
// precompile creates the description, after which the lookup returns it).
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

// device.hpp:53-55 (precompile creates in order and stops at the first
// failure with its code and the description's debug name).
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

// resources.hpp:44-47 (a buffer created with initial data holds it when
// creation returns) read back through synchronization.hpp:35-40
// (copy_to_staging, the fence, the lock).
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

// pipeline_states.hpp:36-42 (the Metal backend creates pipelines with an
// empty vertex layout only and returns VertexLayoutUnsupported for a
// non-empty one).
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

// pipeline_states.hpp:399-401 and :408-413 (the backend returns
// ShaderLibraryRejected when the API rejects the shader library and
// EntryPointNotFound when the function is not in it). The faulty artifacts
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

#if BW_RHI_DEBUG
// backend.hpp:342-350 (the validation layer reports a failure once with the
// code, the resource's debug name and the command's name) and
// transitions.hpp:38-40 and :221-232 (a copy source must be in CopySource at
// use; AccessMissingAtUse). The probe that Device::create wires the layer.
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

using ScenarioFunction = void (*)(const std::filesystem::path&);
struct Scenario {
  std::string_view name;
  ScenarioFunction run;
};

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
#if BW_RHI_DEBUG
    Scenario{"validation_rejects_undeclared_transition_by_name",
             &validation_rejects_undeclared_transition_by_name},
#endif
};

} // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "Usage: rhi_metal_tests <scenario> <metal artifact directory>\n";
    return 2;
  }
  const std::string_view requested = argv[1];
  for (const Scenario& scenario : kScenarios) {
    if (scenario.name == requested) {
      try {
        scenario.run(std::filesystem::path{argv[2]});
        return 0;
      } catch (const std::exception& error) {
        std::cerr << scenario.name << ": " << error.what() << '\n';
        return 1;
      }
    }
  }
  std::cerr << "Unknown scenario\n";
  return 2;
}
