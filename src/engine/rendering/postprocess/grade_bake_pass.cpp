#include "bw/rendering/graph/access.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/postprocess/grade.hpp"
#include "bw/rendering/postprocess/grade_bake.hpp"
#include "bw/rendering/postprocess/post_shaders.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "post_support.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

// Responsibility: the GradeBake object of bw/rendering/postprocess/
// grade_bake.hpp (contracts 11 and 12): the table, the buffer the kernel
// writes and the two passes that rebake the table.
//
// The bake is a compute pass, one dispatch of the bake kernel writing each
// texel's packed word into a persistent storage buffer, then a copy pass
// that copies the buffer into the table: the root layout has no storage
// array for a 3D texture (grade_bake.hpp). The buffer's rows are
// kTableRowPitch bytes and its slices kTableSlicePitch, the alignment a
// buffer-to-texture copy needs on Direct3D 12, kept on every target so the
// layout is one (post_grade_bake.hlsl states the same two numbers). The
// table and the buffer are made once and both rest in their pass states
// between frames.
namespace bw::postprocess {

namespace {

// Thread group of post_grade_bake.hlsl: 4 by 4 by 4 texels.
constexpr std::uint32_t kBakeGroupSize = 4;
constexpr std::uint32_t kTableRowPitch = 256;
constexpr std::uint32_t kTableSlicePitch = kTableRowPitch * kLookupSize;
constexpr std::uint32_t kTableBytes = kTableSlicePitch * kLookupSize;
static_assert(kLookupSize * 4U <= kTableRowPitch);
constexpr float kNeutralTemperature = 6500.0F;
constexpr float kWhiteBalanceEpsilon = 1e-8F;

GradeBakeFailure failed(GradeBakeError error, std::string detail) {
  return GradeBakeFailure{error, GradeError::None, std::move(detail)};
}

[[nodiscard]] shaders::Float3 float3(Rgb c) noexcept { return shaders::Float3{c.r, c.g, c.b}; }

// A range's terms times the whole range's, the offsets added.
struct RangeTerms {
  shaders::Float3 saturation{};
  shaders::Float3 contrast{};
  shaders::Float3 gamma{};
  shaders::Float3 gain{};
  shaders::Float3 offset{};
};
[[nodiscard]] RangeTerms range_terms(const ColorCorrection& range,
                                     const ColorCorrection& whole) noexcept {
  const auto times = [](Rgb a, Rgb b) { return Rgb{a.r * b.r, a.g * b.g, a.b * b.b}; };
  return RangeTerms{float3(times(range.saturation, whole.saturation)),
                    float3(times(range.contrast, whole.contrast)),
                    float3(times(range.gamma, whole.gamma)), float3(times(range.gain, whole.gain)),
                    float3(Rgb{range.offset.r + whole.offset.r, range.offset.g + whole.offset.g,
                               range.offset.b + whole.offset.b})};
}

// The kernel's block for `grade` (post_shaders.hpp, PostGradeBake): the white
// balance resolved to its matrix, applied only away from 6500 K and tint 0,
// and each range's terms resolved. Neutral is tested within 1e-8 of each.
[[nodiscard]] PostGradeBake bake_block(const Grade& grade, shaders::StorageBufferIndex table) {
  PostGradeBake block{};
  block.table = table;
  block.shadows_max = grade.shadows_max;
  block.highlights_min = grade.highlights_min;
  block.highlights_max = grade.highlights_max;
  block.slope = grade.curve.slope;
  block.toe = grade.curve.toe;
  block.shoulder = grade.curve.shoulder;
  block.black_clip = grade.curve.black_clip;
  block.white_clip = grade.curve.white_clip;
  const bool balanced =
      std::fabs(grade.white_temperature - kNeutralTemperature) > kWhiteBalanceEpsilon ||
      std::fabs(grade.white_tint) > kWhiteBalanceEpsilon;
  block.apply_white_balance = balanced ? 1U : 0U;
  if (balanced) {
    const ColorMatrix matrix = white_balance_matrix(grade.white_temperature, grade.white_tint);
    block.white_balance_row0 = float3(matrix.rows[0]);
    block.white_balance_row1 = float3(matrix.rows[1]);
    block.white_balance_row2 = float3(matrix.rows[2]);
  }
  const RangeTerms shadows = range_terms(grade.shadows, grade.whole);
  block.shadows_saturation = shadows.saturation;
  block.shadows_contrast = shadows.contrast;
  block.shadows_gamma = shadows.gamma;
  block.shadows_gain = shadows.gain;
  block.shadows_offset = shadows.offset;
  const RangeTerms midtones = range_terms(grade.midtones, grade.whole);
  block.midtones_saturation = midtones.saturation;
  block.midtones_contrast = midtones.contrast;
  block.midtones_gamma = midtones.gamma;
  block.midtones_gain = midtones.gain;
  block.midtones_offset = midtones.offset;
  const RangeTerms highlights = range_terms(grade.highlights, grade.whole);
  block.highlights_saturation = highlights.saturation;
  block.highlights_contrast = highlights.contrast;
  block.highlights_gamma = highlights.gamma;
  block.highlights_gain = highlights.gain;
  block.highlights_offset = highlights.offset;
  return block;
}

} // namespace

struct GradeBake::State {
  // The record function of the bake's compute pass or its copy pass.
  struct Record {
    State* state{};
    bool copy{};
    void operator()(graph::RecordContext& context) const;
  };

  rhi::Ref<rhi::ComputePipeline> pipeline{};
  rhi::Ref<rhi::Texture> table{};
  rhi::Ref<rhi::Buffer> words{};
  rhi::Ref<rhi::UnorderedAccessView> words_view{};
  // The pair of the last bake declared, and whether there is one.
  bool baked{};
  Grade grade{};
  rhi::DisplayReport display{};
  // The frame's bake.
  PostGradeBake block{};
  graph::TextureHandle table_handle{};
  graph::BufferHandle words_handle{};
  std::array<Record, 2> records{};
};

struct GradeBakeAccess {
  using State = GradeBake::State;
  [[nodiscard]] static GradeBake::State& state(GradeBake& bake) noexcept { return *bake.state_; }
};

GradeBake::GradeBake() : state_(std::make_unique<State>()) {}
GradeBake::GradeBake(GradeBake&&) noexcept = default;
GradeBake& GradeBake::operator=(GradeBake&&) noexcept = default;
GradeBake::~GradeBake() = default;

void GradeBake::frame_dropped() noexcept { state_->baked = false; }

// The table is a 10-bit volume of size 32, written by the bake.
GradeBakeFailure create_grade_bake(rhi::Device& device,
                                   const std::filesystem::path& shader_directory, GradeBake& out) {
  out = GradeBake{};
  GradeBake built;
  GradeBakeAccess::State& state = GradeBakeAccess::state(built);
  const detail::MakeFailure made =
      detail::make_compute_pipeline(device, shader_directory, kGradeBakeShader, state.pipeline);
  if (!made.ok()) {
    return failed(made.step == detail::MakeStep::Artifact ? GradeBakeError::ArtifactFailed
                                                          : GradeBakeError::PipelineFailed,
                  made.detail);
  }
  rhi::TextureDescription table{};
  table.extent = rhi::Extent3{kLookupSize, kLookupSize, kLookupSize};
  table.dimension = rhi::TextureDimension::Texture3D;
  table.format = kLookupFormat;
  table.usage = rhi::TextureUsage::ShaderResource | rhi::TextureUsage::CopyDestination;
  table.initial_access = rhi::kShaderResourceMask;
  table.debug_name = "grade table";
  rhi::TextureResult texture = device.create_texture(table);
  if (!texture.ok()) {
    return failed(GradeBakeError::TextureFailed,
                  detail::result_text("grade table", texture.error(), texture.detail()));
  }
  rhi::BufferDescription words{};
  words.size = kTableBytes;
  words.usage = rhi::BufferUsage::UnorderedAccess | rhi::BufferUsage::CopySource;
  words.initial_access = rhi::Access::UnorderedAccessCompute;
  words.debug_name = "grade table words";
  rhi::BufferResult buffer = device.create_buffer(words);
  if (!buffer.ok()) {
    return failed(GradeBakeError::BufferFailed,
                  detail::result_text("grade table words", buffer.error(), buffer.detail()));
  }
  rhi::UnorderedAccessViewResult storage =
      device.create_unordered_access_view(buffer.value(), rhi::BufferViewDescription{});
  if (!storage.ok()) {
    return failed(
        GradeBakeError::ViewFailed,
        detail::result_text("grade table words' storage view", storage.error(), storage.detail()));
  }
  state.table = std::move(texture.value());
  state.words = std::move(buffer.value());
  state.words_view = std::move(storage.value());
  out = std::move(built);
  return {};
}

void GradeBake::State::Record::operator()(graph::RecordContext& context) const {
  rhi::CommandList& list = context.list();
  if (copy) {
    list.copy_buffer_to_texture(*context.buffer(state->words_handle),
                                rhi::BufferTextureRegion{0, kTableRowPitch, kTableSlicePitch, 0, 0},
                                *context.texture(state->table_handle));
    return;
  }
  list.set_compute_pipeline(*state->pipeline);
  list.set_draw_block(state->block);
  constexpr std::uint32_t kGroups = kLookupSize / kBakeGroupSize;
  list.dispatch(rhi::DispatchArguments{kGroups, kGroups, kGroups});
}

// The table is rebaked only when a cached value changes.
GradeBakeFailure declare_grade_bake(GradeBake& bake, rhi::Device& /*device*/,
                                    graph::FrameGraph& graph, const Grade& grade,
                                    const rhi::DisplayReport& display,
                                    graph::TextureHandle& table) {
  GradeBakeAccess::State& state = GradeBakeAccess::state(bake);
  const GradeError rule = validate_grade(grade);
  if (rule != GradeError::None) {
    return GradeBakeFailure{GradeBakeError::GradeInvalid, rule, "the grade does not validate"};
  }
  if (display.encoding != rhi::DisplayEncoding::Srgb) {
    return failed(GradeBakeError::DisplayEncodingUnsupported,
                  "the bake has no table for the display's encoding");
  }
  state.table_handle = graph.import_texture(graph::ImportedTexture{
      state.table.get(), rhi::kShaderResourceMask, rhi::kShaderResourceMask});
  table = state.table_handle;
  if (state.baked && state.grade == grade && state.display == display) {
    return {};
  }
  state.block = bake_block(grade, state.words_view->storage_buffer_index());
  state.words_handle = graph.import_buffer(graph::ImportedBuffer{
      state.words.get(), rhi::Access::UnorderedAccessCompute, rhi::Access::UnorderedAccessCompute});
  state.records[0] = GradeBakeAccess::State::Record{&state, false};
  state.records[1] = GradeBakeAccess::State::Record{&state, true};
  const std::array<graph::BufferUse, 1> bake_buffers{
      graph::BufferUse{state.words_handle, graph::Access::ComputeWrite}};
  graph.add_pass(graph::PassDeclaration{
      "grade bake", graph::PassKind::Compute, {}, bake_buffers, state.records[0]});
  const std::array<graph::TextureUse, 1> copy_textures{graph::TextureUse{
      state.table_handle, graph::Access::CopyDestination, graph::Contents::Keep, 0}};
  const std::array<graph::BufferUse, 1> copy_buffers{
      graph::BufferUse{state.words_handle, graph::Access::CopySource}};
  graph.add_pass(graph::PassDeclaration{"grade table copy", graph::PassKind::Copy, copy_textures,
                                        copy_buffers, state.records[1]});
  // The table holds the bake for the frames after this one.
  graph.mark_history(state.table_handle);
  state.baked = true;
  state.grade = grade;
  state.display = display;
  return {};
}

} // namespace bw::postprocess
