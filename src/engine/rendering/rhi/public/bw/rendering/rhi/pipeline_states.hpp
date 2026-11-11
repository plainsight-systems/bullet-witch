#ifndef BW_RENDERING_RHI_PIPELINE_STATES_HPP
#define BW_RENDERING_RHI_PIPELINE_STATES_HPP
#include "bw/rendering/rhi/binding_structure.hpp"
#include "bw/rendering/rhi/descriptor_allocator.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstdint>
#include <string_view>

// Responsibility: the immutable state objects, the graphics and compute
// pipeline descriptions, the render pass description and the named creation
// failures (docs/architecture/rhi.md, invariant 6 and "Pipeline states and
// caches"). A pipeline state is created from shader artifacts plus a complete
// state description; creation failure is a named error, never a fallback
// pipeline. Sampler, rasterizer, depth-stencil and blend states are created
// once per distinct description and shared (device.hpp).
//
// Contract.
//   State descriptions are plain values; two with equal fields are the same
//   state and the device returns the same object for both. A Sampler also
//   owns one slot of the sampler table; its index is what a parameter block
//   carries.
//   A GraphicsPipelineDescription names: a vertex artifact (required, stage
//   Vertex), a pixel artifact (optional, stage Pixel; null for a depth-only
//   pipeline), the three state objects (required, non-null), the vertex
//   layout, the render target layout (color formats, depth-stencil format,
//   sample count) and the primitive type. Artifacts must be for the device's
//   backend target and the engine root layout; the loader already checked
//   both (artifact.hpp), the device checks the target again by name.
//   Vertex layout in this slice: the Metal backend creates pipelines with an
//   empty vertex layout only, and returns VertexLayoutUnsupported for a
//   non-empty one. The reason and the follow-up are in the plan: a converter
//   built vertex shader takes its input layout from a stage-in function the
//   converter synthesizes, and the converter is a build prerequisite that
//   does not ship; the first slice that draws a vertex buffer adds the
//   build-time synthesis. This is a declared limit, not a fallback.
//   A RenderPassDescription names color targets 0..n-1 without gaps, each a
//   texture with RenderTarget usage, one mip and one slice, with load and
//   store actions, and an optional depth-stencil target. validate_render_pass
//   names the first violation. render_target_layout derives the layout a
//   pipeline must match; a pipeline whose layout differs from the pass it is
//   set in is rejected by the validation layer.
//   Pipelines are created only by the device (device.hpp), through the
//   description cache; a GraphicsPipeline holds Refs to its state objects
//   and copies of both artifacts' headers; it does not keep the artifact
//   bytes.
//
// Core guidelines read: C.35 and C.67 (through Resource), I.1 and E.27
// (complete descriptions, named codes), F.7 (descriptions name state
// objects by observer pointer; the created pipeline takes Refs), Enum.3.
// Performance guidelines read: GDSA.17 (content keys; pipeline_cache.hpp),
// CACHE.2 (state descriptions are small POD values compared by content).
namespace bw::rhi {

// Enumerations: the values the game's passes use.
enum class Filter : std::uint8_t {
  Point,
  Bilinear,
  Trilinear,
  AnisotropicPoint,
  AnisotropicLinear
};
enum class AddressMode : std::uint8_t { Wrap, Clamp, Mirror, Border };
enum class CompareFunction : std::uint8_t {
  Never,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  Equal,
  NotEqual,
  Always,
};
enum class FillMode : std::uint8_t { Solid, Wireframe };
// Front faces wind counter-clockwise in clip space on every backend, and
// CullMode names the winding that is culled: Clockwise culls back faces,
// CounterClockwise culls front faces. A backend whose API defaults differ
// sets its winding to match.
enum class CullMode : std::uint8_t { None, Clockwise, CounterClockwise };
enum class DepthClipMode : std::uint8_t { Clip, Clamp };
enum class StencilOp : std::uint8_t {
  Keep,
  Zero,
  Replace,
  SaturatedIncrement,
  SaturatedDecrement,
  Invert,
  Increment,
  Decrement,
};
enum class BlendOperation : std::uint8_t { Add, Subtract, Min, Max, ReverseSubtract };
enum class BlendFactor : std::uint8_t {
  Zero,
  One,
  SourceColor,
  InverseSourceColor,
  SourceAlpha,
  InverseSourceAlpha,
  DestinationAlpha,
  InverseDestinationAlpha,
  DestinationColor,
  InverseDestinationColor,
  // No command sets the blend constant in this slice (command_list.hpp
  // has no set_blend_constant); a backend blends against (0, 0, 0, 0)
  // and the first pass of the game that needs the constant adds the
  // command. Stated here so the enumerator is not read as supported.
  ConstantBlendFactor,
  InverseConstantBlendFactor,
  Source1Color,
  InverseSource1Color,
  Source1Alpha,
  InverseSource1Alpha,
};
enum class ColorWriteMask : std::uint8_t {
  None = 0,
  Red = 1U << 0U,
  Green = 1U << 1U,
  Blue = 1U << 2U,
  Alpha = 1U << 3U,
  All = Red | Green | Blue | Alpha,
};
enum class PrimitiveType : std::uint8_t { TriangleList, TriangleStrip, LineList, PointList };
// Border colors a sampler may name: the three Metal offers
// (MTLSamplerBorderColor). No pass of the game samples with another border.
enum class BorderColor : std::uint8_t { TransparentBlack, OpaqueBlack, OpaqueWhite };

// A sampler's compare is a CompareFunction, so a shadow compare of any
// direction is expressible; the three APIs accept every value. Equality is
// over every field.
struct SamplerDescription {
  Filter filter{Filter::Point};
  AddressMode address_u{AddressMode::Wrap};
  AddressMode address_v{AddressMode::Wrap};
  AddressMode address_w{AddressMode::Wrap};
  float mip_bias{};
  float min_mip{};
  float max_mip{1000.0F};
  std::uint8_t max_anisotropy{1};
  BorderColor border_color{BorderColor::TransparentBlack};
  // Never means no comparison.
  CompareFunction compare{CompareFunction::Never};
  [[nodiscard]] friend constexpr bool operator==(const SamplerDescription&,
                                                 const SamplerDescription&) noexcept = default;
};

struct RasterizerDescription {
  FillMode fill{FillMode::Solid};
  CullMode cull{CullMode::None};
  float depth_bias{};
  float slope_scale_depth_bias{};
  DepthClipMode depth_clip{DepthClipMode::Clip};
  bool allow_multisample{};
  [[nodiscard]] friend constexpr bool operator==(const RasterizerDescription&,
                                                 const RasterizerDescription&) noexcept = default;
};

struct StencilFace {
  bool enable{};
  CompareFunction test{CompareFunction::Always};
  StencilOp fail{StencilOp::Keep};
  StencilOp depth_fail{StencilOp::Keep};
  StencilOp pass{StencilOp::Keep};
  [[nodiscard]] friend constexpr bool operator==(const StencilFace&,
                                                 const StencilFace&) noexcept = default;
};
struct DepthStencilDescription {
  bool depth_write{true};
  CompareFunction depth_test{CompareFunction::LessEqual};
  StencilFace front{};
  StencilFace back{};
  std::uint8_t stencil_read_mask{0xFF};
  std::uint8_t stencil_write_mask{0xFF};
  [[nodiscard]] friend constexpr bool operator==(const DepthStencilDescription&,
                                                 const DepthStencilDescription&) noexcept = default;
};

inline constexpr std::uint32_t kMaxRenderTargets = 8;

struct RenderTargetBlend {
  BlendOperation color_op{BlendOperation::Add};
  BlendFactor color_source{BlendFactor::One};
  BlendFactor color_destination{BlendFactor::Zero};
  BlendOperation alpha_op{BlendOperation::Add};
  BlendFactor alpha_source{BlendFactor::One};
  BlendFactor alpha_destination{BlendFactor::Zero};
  ColorWriteMask write_mask{ColorWriteMask::All};
  [[nodiscard]] friend constexpr bool operator==(const RenderTargetBlend&,
                                                 const RenderTargetBlend&) noexcept = default;
};
struct BlendDescription {
  std::array<RenderTargetBlend, kMaxRenderTargets> targets{};
  // When false, targets[0] applies to every target.
  bool independent{};
  bool alpha_to_coverage{};
  [[nodiscard]] friend constexpr bool operator==(const BlendDescription&,
                                                 const BlendDescription&) noexcept = default;
};

// State objects, each with its description. The vertex layout is not an
// object; it is a value in the pipeline description.
class Sampler : public Resource {
public:
  [[nodiscard]] const SamplerDescription& description() const noexcept { return description_; }
  [[nodiscard]] DescriptorSlot slot() const noexcept { return slot_; }
  [[nodiscard]] shaders::SamplerIndex index() const noexcept { return {slot_.index}; }

protected:
  Sampler(DeferredDeleteQueue& queue, const SamplerDescription& description,
          DescriptorSlot slot) noexcept
      : Resource(queue), description_(description), slot_(slot) {}

private:
  SamplerDescription description_;
  DescriptorSlot slot_;
};
class RasterizerState : public Resource {
public:
  [[nodiscard]] const RasterizerDescription& description() const noexcept { return description_; }

protected:
  RasterizerState(DeferredDeleteQueue& queue, const RasterizerDescription& description) noexcept
      : Resource(queue), description_(description) {}

private:
  RasterizerDescription description_;
};
class DepthStencilState : public Resource {
public:
  [[nodiscard]] const DepthStencilDescription& description() const noexcept { return description_; }

protected:
  DepthStencilState(DeferredDeleteQueue& queue, const DepthStencilDescription& description) noexcept
      : Resource(queue), description_(description) {}

private:
  DepthStencilDescription description_;
};
class BlendState : public Resource {
public:
  [[nodiscard]] const BlendDescription& description() const noexcept { return description_; }

protected:
  BlendState(DeferredDeleteQueue& queue, const BlendDescription& description) noexcept
      : Resource(queue), description_(description) {}

private:
  BlendDescription description_;
};

// Vertex layout, a value beside the shaders. The location is the vertex
// input location of the artifact's reflection (artifact.hpp,
// VertexInput::location).
enum class VertexInputRate : std::uint8_t { PerVertex, PerInstance };
struct VertexAttribute {
  std::uint32_t location{};
  Format format{Format::Unknown};
  std::uint8_t buffer_slot{};
  std::uint32_t offset{};
  [[nodiscard]] friend constexpr bool operator==(VertexAttribute,
                                                 VertexAttribute) noexcept = default;
};
struct VertexBufferBinding {
  std::uint32_t stride{};
  VertexInputRate rate{VertexInputRate::PerVertex};
  [[nodiscard]] friend constexpr bool operator==(VertexBufferBinding,
                                                 VertexBufferBinding) noexcept = default;
};
inline constexpr std::uint32_t kMaxVertexBuffers = 8;
struct VertexLayout {
  std::array<VertexAttribute, shaders::kMaxVertexInputs> attributes{};
  std::array<VertexBufferBinding, kMaxVertexBuffers> buffers{};
  std::uint8_t attribute_count{};
  std::uint8_t buffer_count{};
  [[nodiscard]] constexpr bool empty() const noexcept { return attribute_count == 0; }
  [[nodiscard]] friend constexpr bool operator==(const VertexLayout&,
                                                 const VertexLayout&) noexcept = default;
};

// The render target formats and sample count a pipeline is built for. It
// carries no flags: sRGB is in the format.
struct RenderTargetLayout {
  std::array<Format, kMaxRenderTargets> color_formats{};
  std::uint8_t color_count{};
  Format depth_stencil_format{Format::Unknown};
  std::uint8_t sample_count{1};
  [[nodiscard]] friend constexpr bool operator==(const RenderTargetLayout&,
                                                 const RenderTargetLayout&) noexcept = default;
};

// The depth and stencil load and store actions and the depth-stencil access
// belong to the render pass, not to the pipeline.
struct GraphicsPipelineDescription {
  const shaders::Artifact* vertex_shader{};
  // Null for a depth-only pipeline.
  const shaders::Artifact* pixel_shader{};
  const RasterizerState* rasterizer{};
  const DepthStencilState* depth_stencil{};
  const BlendState* blend{};
  VertexLayout vertex_layout{};
  RenderTargetLayout targets{};
  PrimitiveType primitive{PrimitiveType::TriangleList};
  std::string_view debug_name{};
};

struct ComputePipelineDescription {
  const shaders::Artifact* compute_shader{};
  std::string_view debug_name{};
};

// A creation failure is a named error returned to the caller; the device
// never continues with a fallback pipeline (invariant 6).
enum class PipelineError : std::uint8_t {
  None,
  MissingVertexShader,
  MissingComputeShader,
  // An artifact whose stage is not the one its slot requires.
  WrongStage,
  // An artifact built for another backend's target.
  WrongTarget,
  NullState,
  // A non-empty vertex layout on a backend that does not take one yet.
  VertexLayoutUnsupported,
  TooManyRenderTargets,
  // A color or depth format without the RenderTarget or DepthStencil
  // capability on this device.
  FormatNotRenderable,
  SampleCountUnsupported,
  // The API rejected the shader library or the function was not in it.
  ShaderLibraryRejected,
  EntryPointNotFound,
  // The description cache missed in a build whose policy forbids creation
  // at draw time (device.hpp).
  CacheMiss,
  BackendRejected,
};

// The pure part of the device's pipeline checks, the first that applies in
// this order; the device then applies the checks that need its state
// (WrongTarget against the backend's ShaderTarget, FormatNotRenderable and
// SampleCountUnsupported against the capability report) and the backend
// returns VertexLayoutUnsupported, ShaderLibraryRejected, EntryPointNotFound
// and BackendRejected.
//   graphics: MissingVertexShader (null vertex_shader); WrongStage (the
//   vertex artifact's stage is not Vertex, or a non-null pixel artifact's
//   stage is not Pixel); NullState (any of rasterizer, depth_stencil, blend
//   null); TooManyRenderTargets (targets.color_count > kMaxRenderTargets).
//   compute: MissingComputeShader (null compute_shader); WrongStage (stage
//   not Compute).
[[nodiscard]] PipelineError
validate_graphics_pipeline(const GraphicsPipelineDescription& description) noexcept;
[[nodiscard]] PipelineError
validate_compute_pipeline(const ComputePipelineDescription& description) noexcept;

class GraphicsPipeline : public Resource {
public:
  // description().debug_name views this object's copy of the name
  // (resources.hpp, DebugName: empty with BW_RHI_DEBUG 0).
  [[nodiscard]] const GraphicsPipelineDescription& description() const noexcept {
    return description_;
  }
  [[nodiscard]] const PipelineBinding& binding() const noexcept { return binding_; }
  [[nodiscard]] const shaders::InputHash& vertex_input_hash() const noexcept {
    return vertex_hash_;
  }
  [[nodiscard]] const shaders::InputHash& pixel_input_hash() const noexcept { return pixel_hash_; }

protected:
  // Takes Refs on the three state objects; copies the artifacts' input
  // hashes; description().vertex_shader and pixel_shader are not kept alive
  // and are null after construction.
  GraphicsPipeline(DeferredDeleteQueue& queue, const GraphicsPipelineDescription& description,
                   const PipelineBinding& binding) noexcept;

private:
  GraphicsPipelineDescription description_;
  DebugName name_;
  PipelineBinding binding_;
  shaders::InputHash vertex_hash_{};
  shaders::InputHash pixel_hash_{};
  Ref<const RasterizerState> rasterizer_;
  Ref<const DepthStencilState> depth_stencil_;
  Ref<const BlendState> blend_;
};

class ComputePipeline : public Resource {
public:
  // This object's copy of the description's debug name (resources.hpp,
  // DebugName: empty with BW_RHI_DEBUG 0); the validation layer names it.
  [[nodiscard]] std::string_view debug_name() const noexcept { return name_.view(); }
  [[nodiscard]] const PipelineBinding& binding() const noexcept { return binding_; }
  [[nodiscard]] const shaders::InputHash& input_hash() const noexcept { return hash_; }
  [[nodiscard]] const std::array<std::uint32_t, 3>& thread_group_size() const noexcept {
    return thread_group_;
  }

protected:
  ComputePipeline(DeferredDeleteQueue& queue, const ComputePipelineDescription& description,
                  const PipelineBinding& binding) noexcept;

private:
  DebugName name_;
  PipelineBinding binding_;
  shaders::InputHash hash_{};
  std::array<std::uint32_t, 3> thread_group_{};
};

using SamplerResult = Result<Ref<Sampler>, ResourceError>;
using RasterizerStateResult = Result<Ref<RasterizerState>, ResourceError>;
using DepthStencilStateResult = Result<Ref<DepthStencilState>, ResourceError>;
using BlendStateResult = Result<Ref<BlendState>, ResourceError>;
using GraphicsPipelineResult = Result<Ref<GraphicsPipeline>, PipelineError>;
using ComputePipelineResult = Result<Ref<ComputePipeline>, PipelineError>;

// Render passes. Clear writes the target texture's
// TextureDescription::clear_value (resources.hpp); NoAction leaves the
// contents undefined; Load keeps them.
enum class LoadAction : std::uint8_t { NoAction, Load, Clear };
enum class StoreAction : std::uint8_t { NoAction, Store };

class QueryPool;

struct ColorTarget {
  Texture* texture{};
  std::uint8_t mip{};
  std::uint16_t slice{};
  LoadAction load{LoadAction::Load};
  StoreAction store{StoreAction::Store};
};
struct DepthStencilTarget {
  Texture* texture{};
  LoadAction depth_load{LoadAction::Load};
  StoreAction depth_store{StoreAction::Store};
  LoadAction stencil_load{LoadAction::NoAction};
  StoreAction stencil_store{StoreAction::NoAction};
  bool depth_read_only{};
  bool stencil_read_only{};
};
struct RenderPassDescription {
  std::array<ColorTarget, kMaxRenderTargets> colors{};
  std::uint8_t color_count{};
  DepthStencilTarget depth_stencil{};
  // Occlusion queries recorded inside the pass are from this pool; null
  // when none are.
  QueryPool* occlusion_queries{};
  std::string_view name{};
};

// validate_render_pass requires a consistent sample count across color and
// depth targets and no gaps in the color slots, and checks the usage, range
// and extent rules every backend needs.
enum class RenderPassError : std::uint8_t {
  None,
  NoTargets,
  ColorSlotGap,
  TargetUsageMissing,
  MipOrSliceOutOfRange,
  SampleCountMismatch,
  ExtentMismatch,
  // A depth target whose format has no stencil but names a stencil action
  // other than NoAction, or a read-only flag on a load of Clear.
  StencilOnDepthOnlyFormat,
  ClearOnReadOnlyTarget,
  // A target with TextureUsage::Memoryless (resources.hpp) whose load action
  // is Load or whose store action (depth_store for a depth target) is Store.
  MemorylessStored,
};
[[nodiscard]] RenderPassError validate_render_pass(const RenderPassDescription& pass) noexcept;
// Precondition: validate_render_pass returned None.
[[nodiscard]] RenderTargetLayout render_target_layout(const RenderPassDescription& pass) noexcept;

} // namespace bw::rhi
#endif
