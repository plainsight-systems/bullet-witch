// Unit test: public contracts of bw/rendering/rhi/command_list.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
// Replay is asserted through a recording Context: Context is the public,
// pure-virtual boundary replay drives (backend.hpp), so recording the calls
// it receives is the test of the replay contract itself.
#include "bw/rendering/rhi/command_list.hpp"

#include "bw/rendering/rhi/backend.hpp"
#include "bw/rendering/rhi/build.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/queries.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "pipeline_objects.hpp"
#include "test_main.hpp"
#include "test_objects.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using bw::rhi::Access;
using bw::rhi::Buffer;
using bw::rhi::CommandList;
using bw::rhi::ExecuteError;
using bw::rhi::Ref;
using bw::rhi::Transition;
using bw::rhi::TransitionBatch;
using bw_rhi_test::require;

// One call the Context received, with copies of the arguments a scenario
// compares.
struct Call {
  std::string name;
  std::vector<Transition> transitions{};
  std::string text{};
  std::vector<std::byte> bytes{};
  std::vector<std::uint32_t> words{};
  std::uint64_t number{};
  std::uint32_t slot{};
  const void* object{};
  bw::rhi::Viewport viewport{};
  bw::rhi::ScissorRect scissor{};
  bw::rhi::DrawArguments draw{};
  bw::rhi::BufferCopy copy{};
  bw::rhi::RenderPassDescription pass{};
  const void* other{};
  bw::rhi::DrawIndexedArguments indexed{};
  bw::rhi::DispatchArguments groups{};
  bw::rhi::TextureCopyRegion texture_region{};
  bw::rhi::BufferTextureRegion buffer_region{};
};

class RecordingContext final : public bw::rhi::Context {
public:
  std::vector<Call> calls;
  // set_draw_block returns failures[i] for its i-th call, None past the end.
  std::vector<ExecuteError> failures;

  void begin_transitions(const TransitionBatch& batch) override {
    calls.push_back(Call{.name = "begin_transitions",
                         .transitions = {batch.transitions.begin(), batch.transitions.end()}});
  }
  void end_transitions(const TransitionBatch& batch) override {
    calls.push_back(Call{.name = "end_transitions",
                         .transitions = {batch.transitions.begin(), batch.transitions.end()}});
  }
  void begin_render_pass(const bw::rhi::RenderPassDescription& pass) override {
    calls.push_back(
        Call{.name = "begin_render_pass", .text = std::string(pass.name), .pass = pass});
  }
  void end_render_pass() override { calls.push_back(Call{.name = "end_render_pass"}); }
  void set_graphics_pipeline(bw::rhi::GraphicsPipeline& pipeline) override {
    calls.push_back(Call{.name = "set_graphics_pipeline", .object = &pipeline});
  }
  void set_compute_pipeline(bw::rhi::ComputePipeline& pipeline) override {
    calls.push_back(Call{.name = "set_compute_pipeline", .object = &pipeline});
  }
  void set_viewport(const bw::rhi::Viewport& viewport) override {
    calls.push_back(Call{.name = "set_viewport", .viewport = viewport});
  }
  void set_scissor(const bw::rhi::ScissorRect& rect) override {
    calls.push_back(Call{.name = "set_scissor", .scissor = rect});
  }
  void set_stencil_reference(std::uint32_t reference) override {
    calls.push_back(Call{.name = "set_stencil_reference", .number = reference});
  }
  void set_vertex_buffer(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) override {
    calls.push_back(
        Call{.name = "set_vertex_buffer", .number = offset, .slot = slot, .object = &buffer});
  }
  void set_index_buffer(Buffer& buffer, bw::rhi::IndexFormat format,
                        std::uint64_t offset) override {
    calls.push_back(Call{.name = "set_index_buffer",
                         .number = offset,
                         .slot = static_cast<std::uint32_t>(format),
                         .object = &buffer});
  }
  void set_static_block(std::uint32_t slot, Buffer& buffer, std::uint64_t offset) override {
    calls.push_back(
        Call{.name = "set_static_block", .number = offset, .slot = slot, .object = &buffer});
  }
  ExecuteError set_draw_block(std::span<const std::byte> bytes,
                              bw::shaders::LayoutHash layout) override {
    const std::size_t index = draw_blocks_++;
    calls.push_back(Call{
        .name = "set_draw_block", .bytes = {bytes.begin(), bytes.end()}, .number = layout.value});
    return index < failures.size() ? failures[index] : ExecuteError::None;
  }
  void set_root_constants(std::uint32_t first_word, std::span<const std::uint32_t> words) override {
    calls.push_back(Call{
        .name = "set_root_constants", .words = {words.begin(), words.end()}, .slot = first_word});
  }
  void draw(const bw::rhi::DrawArguments& arguments) override {
    calls.push_back(Call{.name = "draw", .draw = arguments});
  }
  void draw_indexed(const bw::rhi::DrawIndexedArguments& arguments) override {
    calls.push_back(Call{.name = "draw_indexed", .indexed = arguments});
  }
  void dispatch(const bw::rhi::DispatchArguments& arguments) override {
    calls.push_back(Call{.name = "dispatch", .groups = arguments});
  }
  // The signature is Context's (backend.hpp).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void copy_buffer(Buffer& source, Buffer& destination, const bw::rhi::BufferCopy& copy) override {
    calls.push_back(
        Call{.name = "copy_buffer", .object = &source, .copy = copy, .other = &destination});
  }
  // The signature is Context's (backend.hpp).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  void copy_texture(bw::rhi::Texture& source, bw::rhi::Texture& destination,
                    const bw::rhi::TextureCopyRegion& region) override {
    calls.push_back(Call{.name = "copy_texture",
                         .object = &source,
                         .other = &destination,
                         .texture_region = region});
  }
  void copy_buffer_to_texture(Buffer& source, const bw::rhi::BufferTextureRegion& region,
                              bw::rhi::Texture& destination) override {
    calls.push_back(Call{.name = "copy_buffer_to_texture",
                         .object = &source,
                         .other = &destination,
                         .buffer_region = region});
  }
  void copy_texture_to_staging(bw::rhi::Texture& source, std::uint8_t mip, std::uint16_t slice,
                               bw::rhi::StagingBuffer& destination,
                               std::uint64_t destination_offset) override {
    calls.push_back(Call{.name = "copy_texture_to_staging",
                         .number = destination_offset,
                         .slot = static_cast<std::uint32_t>(mip) * 65536U + slice,
                         .object = &source,
                         .other = &destination});
  }
  void copy_buffer_to_staging(Buffer& source, bw::rhi::StagingBuffer& destination,
                              const bw::rhi::BufferCopy& copy) override {
    calls.push_back(Call{
        .name = "copy_buffer_to_staging", .object = &source, .copy = copy, .other = &destination});
  }
  void write_fence(bw::rhi::GpuFence& fence) override {
    calls.push_back(Call{.name = "write_fence", .object = &fence});
  }
  void begin_query(bw::rhi::QueryPool& pool, std::uint32_t index) override {
    calls.push_back(Call{.name = "begin_query", .slot = index, .object = &pool});
  }
  void end_query(bw::rhi::QueryPool& pool, std::uint32_t index) override {
    calls.push_back(Call{.name = "end_query", .slot = index, .object = &pool});
  }
  void begin_marker(std::string_view name) override {
    calls.push_back(Call{.name = "begin_marker", .text = std::string(name)});
  }
  void end_marker() override { calls.push_back(Call{.name = "end_marker"}); }

private:
  std::size_t draw_blocks_{};
};

// Test-local fence, staging buffer and query pool, derived from the public
// types as a backend derives them.
class TestFence final : public bw::rhi::GpuFence {
public:
  explicit TestFence(bw::rhi::DeferredDeleteQueue& queue) noexcept : GpuFence(queue, "Fence") {}
  [[nodiscard]] bw::rhi::FenceState state() const noexcept override {
    return bw::rhi::FenceState::Unwritten;
  }
  void wait() noexcept override {}
  void reset() noexcept override {}

private:
  ~TestFence() override = default;
};

class TestStagingBuffer final : public bw::rhi::StagingBuffer {
public:
  explicit TestStagingBuffer(bw::rhi::DeferredDeleteQueue& queue) noexcept
      : StagingBuffer(queue, 4096, "Staging") {}

private:
  ~TestStagingBuffer() override = default;
};

class TestQueryPool final : public bw::rhi::QueryPool {
public:
  explicit TestQueryPool(bw::rhi::DeferredDeleteQueue& queue) noexcept
      : QueryPool(queue,
                  bw::rhi::QueryPoolDescription{bw::rhi::QueryKind::Occlusion, 4, "Queries"}) {}

private:
  ~TestQueryPool() override = default;
};

bool names_are(const RecordingContext& context, std::span<const std::string_view> names) {
  if (context.calls.size() != names.size()) {
    return false;
  }
  for (std::size_t i = 0; i < names.size(); ++i) {
    if (context.calls[i].name != names[i]) {
      return false;
    }
  }
  return true;
}

// (command_list.hpp, CommandList), (command_list.hpp, CommandList::transition)
// and (command_list.hpp, CommandList::replay): every call appends one
// command; the list copies every argument (transition arrays, the pass
// name, draw block bytes, root constant words), so the caller's storage may
// change after the call; replay issues every command in recording order
// with its copied arguments, a combined transition as begin then end, and
// leaves the list intact.
void replay_in_order_with_copied_arguments() {
  bw::rhi::DeferredDeleteQueue queue;
  const Ref<bw_rhi_test::TestBuffer> buffer = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Source"));
  const Ref<bw_rhi_test::TestBuffer> other = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Destination"));
  const Ref<bw_rhi_test::TestTexture> target = bw_rhi_test::make<bw_rhi_test::TestTexture>(
      queue, bw_rhi_test::render_target_description("Target"));

  CommandList list;
  std::array<Transition, 2> transitions{
      Transition{buffer.get(), Access::Unknown, Access::CopySource},
      Transition{other.get(), Access::Unknown, Access::CopyDestination}};
  list.transition(TransitionBatch{transitions, {}});
  transitions[0].after = Access::RenderTarget;
  transitions[1].resource = nullptr;
  list.copy_buffer(*buffer, *other, bw::rhi::BufferCopy{8, 16, 32});

  std::string pass_name = "Main pass";
  bw::rhi::RenderPassDescription pass{};
  pass.color_count = 1;
  pass.colors[0].texture = target.get();
  pass.name = pass_name;
  list.begin_render_pass(pass);
  pass_name.assign("Overwritten");
  list.set_viewport(bw::rhi::Viewport{1.0F, 2.0F, 64.0F, 32.0F, 0.0F, 1.0F});
  list.set_scissor(bw::rhi::ScissorRect{3, 4, 50, 20});
  list.set_stencil_reference(7);
  list.set_vertex_buffer(1, *buffer, 48);
  std::array<std::byte, 24> block{};
  for (std::size_t i = 0; i < block.size(); ++i) {
    block[i] = static_cast<std::byte>(i + 1U);
  }
  list.set_draw_block(block, bw::shaders::LayoutHash{0xFEEDU});
  block.fill(std::byte{0});
  std::array<std::uint32_t, 3> words{10, 20, 30};
  list.set_root_constants(2, words);
  words.fill(0);
  list.draw(bw::rhi::DrawArguments{3, 1, 0, 0});
  list.end_render_pass();
  require(list.command_count() == 11U, "one command per call");
  const std::size_t bytes_before = list.byte_size();

  RecordingContext context;
  const bw::rhi::Status<ExecuteError> status = list.replay(context);
  require(status.ok(), "replay succeeds");
  constexpr std::array<std::string_view, 12> expected{"begin_transitions",
                                                      "end_transitions",
                                                      "copy_buffer",
                                                      "begin_render_pass",
                                                      "set_viewport",
                                                      "set_scissor",
                                                      "set_stencil_reference",
                                                      "set_vertex_buffer",
                                                      "set_draw_block",
                                                      "set_root_constants",
                                                      "draw",
                                                      "end_render_pass"};
  require(names_are(context, expected), "every command in recording order");

  const Call& begin = context.calls[0];
  require(begin.transitions.size() == 2U && begin.transitions[0].after == Access::CopySource &&
              begin.transitions[1].resource == other.get() &&
              begin.transitions[1].after == Access::CopyDestination,
          "the transition array was copied at record time");
  require(context.calls[1].transitions.size() == 2U, "the end carries the same batch");
  require(context.calls[2].object == buffer.get() && context.calls[2].copy.source_offset == 8U &&
              context.calls[2].copy.destination_offset == 16U && context.calls[2].copy.size == 32U,
          "copy arguments");
  require(context.calls[3].text == "Main pass", "the pass name was copied");
  require(context.calls[3].pass.color_count == 1U &&
              context.calls[3].pass.colors[0].texture == target.get(),
          "the pass description");
  require(context.calls[4].viewport.width == 64.0F && context.calls[4].viewport.y == 2.0F,
          "viewport");
  require(context.calls[5].scissor.x == 3 && context.calls[5].scissor.height == 20U, "scissor");
  require(context.calls[6].number == 7U, "stencil reference");
  require(context.calls[7].slot == 1U && context.calls[7].number == 48U &&
              context.calls[7].object == buffer.get(),
          "vertex buffer slot, buffer and offset");
  const Call& draw_block = context.calls[8];
  require(draw_block.bytes.size() == 24U && draw_block.bytes[0] == std::byte{1} &&
              draw_block.bytes[23] == std::byte{24} && draw_block.number == 0xFEEDU,
          "the draw block bytes and layout hash were copied");
  require(context.calls[9].slot == 2U &&
              context.calls[9].words == std::vector<std::uint32_t>{10, 20, 30},
          "the root constant words were copied");
  require(context.calls[10].draw.vertex_count == 3U, "draw arguments");
  require(list.command_count() == 11U && list.byte_size() == bytes_before,
          "replay leaves the list intact");
}

// (command_list.hpp, CommandList::begin_render_pass),
// (command_list.hpp, CommandList::end_render_pass),
// (command_list.hpp, CommandList::inside_render_pass) and
// (command_list.hpp, CommandList::reset): begin_render_pass and
// end_render_pass bracket a pass and inside_render_pass reports it; reset
// empties the list.
void reset_empties_and_pass_tracking() {
  bw::rhi::DeferredDeleteQueue queue;
  const Ref<bw_rhi_test::TestTexture> target = bw_rhi_test::make<bw_rhi_test::TestTexture>(
      queue, bw_rhi_test::render_target_description("Target"));
  CommandList list;
  require(!list.inside_render_pass() && list.command_count() == 0U && list.byte_size() == 0U,
          "a new list is empty and outside a pass");
  bw::rhi::RenderPassDescription pass{};
  pass.color_count = 1;
  pass.colors[0].texture = target.get();
  list.begin_render_pass(pass);
  require(list.inside_render_pass(), "inside after begin");
  list.draw(bw::rhi::DrawArguments{3});
  list.end_render_pass();
  require(!list.inside_render_pass(), "outside after end");
  list.begin_render_pass(pass);
  require(list.command_count() == 4U && list.byte_size() > 0U, "four commands recorded");
  list.reset();
  require(list.command_count() == 0U && list.byte_size() == 0U, "reset empties the list");
  require(!list.inside_render_pass(), "reset leaves no pass open");
  RecordingContext context;
  require(list.replay(context).ok() && context.calls.empty(), "an empty list issues nothing");
}

// (command_list.hpp, CommandList::begin_marker) and
// (command_list.hpp, CommandList::end_marker): begin_marker and end_marker record a
// command each and the name is copied; with BW_RHI_DEBUG 0 they record
// nothing.
void markers_record_in_debug() {
  CommandList list;
  std::string name = "Shadows";
  list.begin_marker(name);
  name.assign("Changed");
  list.end_marker();
  RecordingContext context;
  require(list.replay(context).ok(), "replay succeeds");
#if BW_RHI_DEBUG
  require(list.command_count() == 2U, "two marker commands");
  constexpr std::array<std::string_view, 2> expected{"begin_marker", "end_marker"};
  require(names_are(context, expected), "begin then end");
  require(context.calls[0].text == "Shadows", "the marker name was copied");
#else
  require(list.command_count() == 0U && context.calls.empty(), "markers record nothing");
#endif
}

// (command_list.hpp, CommandList::replay), (command_list.hpp, ExecuteError)
// and (backend.hpp, Context::set_draw_block): replay
// returns the first failure set_draw_block reports, names the command in the
// detail, and issues nothing after it.
void replay_stops_at_first_failure() {
  const std::array<std::byte, 16> block{};
  // A list is replayed at most once per recording (command_list.hpp, CommandList::replay), so
  // each replay gets its own recording of the same commands.
  const auto record = [&block](CommandList& list) {
    list.set_draw_block(block, bw::shaders::LayoutHash{1});
    list.draw(bw::rhi::DrawArguments{3});
    list.set_draw_block(block, bw::shaders::LayoutHash{2});
    list.draw(bw::rhi::DrawArguments{6});
  };
  CommandList list;
  record(list);

  RecordingContext exhausted;
  exhausted.failures = {ExecuteError::None, ExecuteError::TransientBudgetExhausted};
  const bw::rhi::Status<ExecuteError> status = list.replay(exhausted);
  require(!status.ok() && status.error() == ExecuteError::TransientBudgetExhausted,
          "the first failure is returned");
  require(status.detail() == "set_draw_block", "the detail names the command");
  constexpr std::array<std::string_view, 3> issued{"set_draw_block", "draw", "set_draw_block"};
  require(names_are(exhausted, issued), "nothing is issued after the failing command");

  CommandList second;
  record(second);
  RecordingContext too_large;
  too_large.failures = {ExecuteError::DrawBlockTooLarge};
  require(second.replay(too_large).error() == ExecuteError::DrawBlockTooLarge &&
              too_large.calls.size() == 1U,
          "a failure on the first command stops at once");
}

// (command_list.hpp, CommandList): resource pointers are copied as observers; the
// list adds no references.
void recording_adds_no_reference() {
  bw::rhi::DeferredDeleteQueue queue;
  const Ref<bw_rhi_test::TestBuffer> buffer = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Observed"));
  const Ref<bw_rhi_test::TestTexture> target = bw_rhi_test::make<bw_rhi_test::TestTexture>(
      queue, bw_rhi_test::render_target_description("Target"));
  require(buffer->ref_count() == 1U && target->ref_count() == 1U, "one Ref each");
  CommandList list;
  list.set_vertex_buffer(0, *buffer, 0);
  list.copy_buffer(*buffer, *buffer, bw::rhi::BufferCopy{0, 64, 64});
  const std::array<Transition, 1> transitions{
      Transition{buffer.get(), Access::Unknown, Access::CopySource}};
  list.transition(TransitionBatch{transitions, {}});
  bw::rhi::RenderPassDescription pass{};
  pass.color_count = 1;
  pass.colors[0].texture = target.get();
  list.begin_render_pass(pass);
  require(buffer->ref_count() == 1U && target->ref_count() == 1U, "recording adds no reference");
  RecordingContext context;
  require(list.replay(context).ok(), "replay succeeds");
  require(buffer->ref_count() == 1U && target->ref_count() == 1U, "replay adds no reference");
}

// (command_list.hpp, CommandList) and (command_list.hpp, CommandList::replay):
// every recording call replays as
// its Context call with the arguments it was given, copied at record time;
// this scenario covers the commands replay_in_order_with_copied_arguments
// does not (compute pipeline, index buffer, static block, indexed draw,
// dispatch, the copies, fences, queries, and the split transitions).
void every_command_replays_with_copied_arguments() {
  bw::rhi::DeferredDeleteQueue queue;
  const Ref<bw_rhi_test::TestBuffer> buffer = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Buffer"));
  const Ref<bw_rhi_test::TestBuffer> other = bw_rhi_test::make<bw_rhi_test::TestBuffer>(
      queue, bw_rhi_test::shader_buffer_description("Other"));
  const Ref<bw_rhi_test::TestTexture> source = bw_rhi_test::make<bw_rhi_test::TestTexture>(
      queue, bw_rhi_test::render_target_description("Source"));
  const Ref<bw_rhi_test::TestTexture> destination = bw_rhi_test::make<bw_rhi_test::TestTexture>(
      queue, bw_rhi_test::render_target_description("Destination"));
  const bw::shaders::Artifact compute_shader =
      bw_rhi_test::artifact(bw::shaders::ShaderStage::Compute, 9);
  const Ref<bw_rhi_test::TestComputePipeline> pipeline =
      bw_rhi_test::make<bw_rhi_test::TestComputePipeline>(
          queue, bw::rhi::ComputePipelineDescription{&compute_shader, "Compute"});
  const Ref<TestFence> fence = bw_rhi_test::make<TestFence>(queue);
  const Ref<TestStagingBuffer> staging = bw_rhi_test::make<TestStagingBuffer>(queue);
  const Ref<TestQueryPool> queries = bw_rhi_test::make<TestQueryPool>(queue);

  CommandList list;
  std::array<Transition, 1> transitions{
      Transition{buffer.get(), Access::Unknown, Access::CopyDestination}};
  const bw::rhi::TransitionPipelines pipelines{bw::rhi::Pipeline::Graphics,
                                               bw::rhi::Pipeline::Graphics};
  list.begin_transitions(TransitionBatch{transitions, pipelines});
  list.end_transitions(TransitionBatch{transitions, pipelines});
  transitions[0].after = Access::RenderTarget;
  list.set_compute_pipeline(*pipeline);
  list.dispatch(bw::rhi::DispatchArguments{4, 2, 1});
  list.set_index_buffer(*buffer, bw::rhi::IndexFormat::Uint32, 32);
  list.set_static_block(3, *other, 128);
  list.draw_indexed(bw::rhi::DrawIndexedArguments{36, 2, 6, -3, 1});
  bw::rhi::TextureCopyRegion texture_region{};
  texture_region.source_mip = 1;
  texture_region.destination_slice = 2;
  texture_region.extent = bw::rhi::Extent3{8, 4, 1};
  list.copy_texture(*source, *destination, texture_region);
  texture_region.extent.width = 0;
  list.copy_buffer_to_texture(*buffer, bw::rhi::BufferTextureRegion{64, 256, 1024, 1, 0},
                              *destination);
  list.copy_texture_to_staging(*source, 2, 5, *staging, 512);
  list.copy_buffer_to_staging(*other, *staging, bw::rhi::BufferCopy{16, 32, 48});
  list.write_fence(*fence);
  list.begin_query(*queries, 3);
  list.end_query(*queries, 3);
  require(list.command_count() == 14U, "one command per call");

  RecordingContext context;
  require(list.replay(context).ok(), "replay succeeds");
  constexpr std::array<std::string_view, 14> expected{"begin_transitions",
                                                      "end_transitions",
                                                      "set_compute_pipeline",
                                                      "dispatch",
                                                      "set_index_buffer",
                                                      "set_static_block",
                                                      "draw_indexed",
                                                      "copy_texture",
                                                      "copy_buffer_to_texture",
                                                      "copy_texture_to_staging",
                                                      "copy_buffer_to_staging",
                                                      "write_fence",
                                                      "begin_query",
                                                      "end_query"};
  require(names_are(context, expected), "every command, in recording order");
  const auto& c = context.calls;
  require(c[0].transitions.size() == 1U && c[0].transitions[0].after == Access::CopyDestination &&
              c[1].transitions.size() == 1U && c[1].transitions[0].after == Access::CopyDestination,
          "split transitions carry the copied batch");
  require(c[2].object == pipeline.get(), "the compute pipeline");
  require(c[3].groups.x == 4U && c[3].groups.y == 2U && c[3].groups.z == 1U, "dispatch groups");
  require(c[4].object == buffer.get() && c[4].number == 32U &&
              c[4].slot == static_cast<std::uint32_t>(bw::rhi::IndexFormat::Uint32),
          "index buffer, format and offset");
  require(c[5].object == other.get() && c[5].slot == 3U && c[5].number == 128U,
          "static block slot, buffer and offset");
  require(c[6].indexed.index_count == 36U && c[6].indexed.instance_count == 2U &&
              c[6].indexed.first_index == 6U && c[6].indexed.base_vertex == -3 &&
              c[6].indexed.first_instance == 1U,
          "indexed draw arguments");
  require(c[7].object == source.get() && c[7].other == destination.get() &&
              c[7].texture_region.source_mip == 1U && c[7].texture_region.destination_slice == 2U &&
              c[7].texture_region.extent.width == 8U,
          "texture copy region was copied at record time");
  require(c[8].object == buffer.get() && c[8].other == destination.get() &&
              c[8].buffer_region.buffer_offset == 64U && c[8].buffer_region.row_pitch == 256U &&
              c[8].buffer_region.slice_pitch == 1024U && c[8].buffer_region.mip == 1U,
          "buffer to texture region");
  require(c[9].object == source.get() && c[9].other == staging.get() && c[9].number == 512U &&
              c[9].slot == 2U * 65536U + 5U,
          "texture to staging mip, slice and offset");
  require(c[10].object == other.get() && c[10].other == staging.get() &&
              c[10].copy.source_offset == 16U && c[10].copy.destination_offset == 32U &&
              c[10].copy.size == 48U,
          "buffer to staging copy");
  require(c[11].object == fence.get(), "the fence");
  require(c[12].object == queries.get() && c[12].slot == 3U && c[13].object == queries.get() &&
              c[13].slot == 3U,
          "query pool and index");
}

constexpr std::array scenarios{
    bw_rhi_test::Scenario{"replay_in_order_with_copied_arguments",
                          replay_in_order_with_copied_arguments},
    bw_rhi_test::Scenario{"reset_empties_and_pass_tracking", reset_empties_and_pass_tracking},
    bw_rhi_test::Scenario{"markers_record_in_debug", markers_record_in_debug},
    bw_rhi_test::Scenario{"replay_stops_at_first_failure", replay_stops_at_first_failure},
    bw_rhi_test::Scenario{"recording_adds_no_reference", recording_adds_no_reference},
    bw_rhi_test::Scenario{"every_command_replays_with_copied_arguments",
                          every_command_replays_with_copied_arguments},
};
} // namespace

int main(int argc, char** argv) { return bw_rhi_test::run_scenario(argc, argv, scenarios); }
