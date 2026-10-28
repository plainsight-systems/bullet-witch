// Math boundary: both-phase Core F.4 / performance MEM.9 keep generic
// fixed-value operations in bw::math; source policy remains with this owner.
// Rigid pose publication follows both-phase I.1/MEM.9: copied affine rows,
// batch validation, then allocation-free composition with each current view.
// Runtime texture-table overrides follow npc-appearance.md. Both-phase I.1
// and MEM.9 make input borrowing explicit and copy resolved bindings only at
// admission; color/shadow slots never search caller metadata during a frame.
// Instance membership owns copied animation state. I.11/MEM.9 (design),
// C.85/GPU.1 (implementation): validate/allocate before publication; retain
// immutable geometry and allocate no storage during pose/shape updates.
// See animated-instance-publication.md for the reference and cost boundary.
// Native specular filtering uses both-phase I.6/I.7/GPU.1 (world.hpp): reject
// unsupported negative exponents before publishing a draw; keep normal moments
// in the shader, without per-frame allocations, transfers or new ownership.
// Receiver projection follows the source pixel-stage boundary. Both-phase I.1
// (#ri-explicit) validates the world-aligned relative-space precondition before
// publication; CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) retains one prepared
// matrix pair per accepted view and per fixed draw. No frame-loop inverse or
// uniform matrix reconstruction; failed updates preserve all retained values.
// Owner-opacity reconstruction applies design/implementation I.1/E.4/MEM.9:
// validate a finite value before mutation, replace indirect alpha once, and
// replay distinct Main/Trans sections through retained phase storage. Source
// opacity zero omits color without altering caster membership. See
// lighting-owner-opacity.md; no fade producer or extra clock is inferred.
// Engine presentation time is an explicit snapshot, including paused frames;
// Lighting redesign I.1 (#ri-explicit), CDSA.32 and MEM.9 (both phases) retain
// explicit per-instance destinations over the prepared model/draw index. All
// destinations validate before mutation; fixed packets allocate nothing.
// exposure never reads a target-local wall clock. Both-phase I.1 (#ri-explicit)
// exposes the input; MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md)
// keeps the handoff inline and allocation-free. Lighting equations are unchanged.
// Scene-wide color phases apply both-phase I.1 (#ri-explicit) and GPU.6
// (guidelines/gpu/GPU.6-batch-tiny-gpu-work.md): prepare an index plan at
// publication, preserve within-phase order, and stream it without per-frame
// allocation or moving retained palettes. See scene-lighting-pass-contract.md.
// Retained visibility applies design I.1/E.4/GPU.1 and implementation E.4/MEM.9:
// validate indexed destinations, publish booleans, then omit hidden color and
// shadow draws. No geometry or pipeline recreation; see motion-event-timeline.md.
// Component replacement applies I.1 (#ri-explicit), C.85 (#rc-swap-noexcept),
// and GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md): validate/prepare before
// nonthrowing publication, keeping GPU assets resident. Both MCP phases read.
// Live view publication follows both-phase I.1/C.121 and GPU.1: validate
// a complete packet, retain it once, and compose constants during existing
// encoding. Geometry, material bindings and palettes remain resident.
// Skinning extension follows the explicit I.1/E.6 and GPU.1/MEM.9 contracts
// retrieved in both phases: immutable rest streams stay resident; palette
// updates validate first and reuse their published allocation. Source and
// pinned production comparison are in alicia-animation-bindings.md.
// Both-phase P.9 (#rp-waste) replaces full draw scans with a sorted destination
// index. MEM.9 allocates its O(draws) storage at publication; updates are
// O(updates * log draws + destinations), allocation-free and transactional.
// Shadow-edge design/implementation reread I.1 (#ri-explicit) and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): shared projection/filter
// guard is validated at submission; clear the retained map then scissor caster
// writes to its interior. Point-clamped edge taps read unoccluded zero depth,
// following the pinned Unreal clear/border contract. No extra GPU allocation
// or CPU readback. See shadow-filter-baseline.md for the bounded source trace.
// Palette submission copies only bounded joint constants and retains them
// through command completion (both-phase I.11 #ri-raw, GPU.1). Index bounds
// are derived once from referenced immutable vertices, not checked on the GPU.
// Base texture/UV reads follow explicit material requirements (both-phase
// cpp-guidelines I.1 #ri-explicit and cpp-performance GPU.1). Untextured
// surfaces retain constant/SH/fog equations without allocating a dummy image.
// Retain immutable stage geometry and textures, then encode selected original
// depth or base-and-normal-textured surface draws with explicit pass state.
// Contract and deliberate color-readiness boundary are in world_renderer.hpp.
// All source definitions are borrowed only during upload; vectors use the
// injected CPU domain and native buffers each retain a reconciled device charge.
// Validate before encoding; synchronous command completion protects retirement.
// No frame loop geometry copy, inferred visibility or substitute color material.
// Both MCP phases: E.6 (#re-raii) rolls back partial acquisition; GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) retains immutable GPU storage.
// State refinements apply I.5 (#ri-pre) and E.4 (#re-design-invariants):
// reject invalid sampling/depth inputs before replacing the retained draw set.
// GPU.1 keeps the original intermediate/device ownership; no readback is added.
// Mip filtering applies I.1 (#ri-explicit) and GPU.9 (guidelines/gpu/
// GPU.9-suballocate-and-alias-transient-gpu-memory.md), read in both phases:
// validate the supplied filter, build retained sampler state during submission,
// and preserve resident texture levels without hidden generation or frame work.
#include "world_renderer.hpp"
#include "effect_sprite_pass.hpp"

#include <cstring>
#include "bw/math/matrix.hpp"
#include "world_postprocess.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include "buffer_allocation.hpp"
#include "bw/content/formats/model.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "bw/memory/allocator.hpp"
#include "bw/render/materials/world_material.hpp"
#include "bw/render/shadows/shadow.hpp"
#include "texture_allocation.hpp"

namespace bw::platform::macos {
namespace {
// Only the source world-axis-preserving camera translation is admitted. An
// unused view may have another basis; it becomes invalid only when consumed.
std::optional<render::ShadowReceiverProjection> receiver_projection(
    const std::array<std::array<float, 4>, 4>& shadow_rows,
    const std::array<std::array<float, 4>, 3>& world_rows) {
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t column = 0; column < 3; ++column) {
      if (world_rows.at(row).at(column) != (row == column ? 1.0F : 0.0F)) return std::nullopt;
    }
  }
  return render::make_shadow_receiver_projection(
      {shadow_rows}, {-world_rows[0][3], -world_rows[1][3], -world_rows[2][3]});
}
constexpr double kIntegerSampleCenterOffset = .5;
constexpr std::uint32_t kDefaultAlphaReference = 128;
constexpr std::uint32_t kMaximumAnisotropy = 16;
// Compatibility values are local to the decoded original format boundary.
constexpr std::uint32_t kStaticMaterialFlags = 0x00420882U;

constexpr std::uint32_t kShaderVariantMask = 0xffffU;
constexpr std::uint32_t kBaseTextureSemantic = 0x42545830U;
constexpr std::uint32_t kNormalTextureSemantic = 0x4e4d4150U;
constexpr std::uint32_t kTriangleList = 4, kTriangleStrip = 5;
constexpr std::uint8_t kNormalUsage = 3, kTangentUsage = 6, kUvUsage = 5, kColorUsage = 10;
constexpr std::uint8_t kNormalizedShort4 = 10, kHalf2 = 15, kNormalizedByte4 = 8;
constexpr std::size_t kPositionBytes = 3 * sizeof(float);
// The direct-light packet is bounded by the original four-light material loop.
// Validate every slot, including unused entries, before either submission or
// an atomic retained update; stale/nonfinite inactive data cannot leak later.
bool valid_local_lights(
    const std::array<render::WorldLocalLight, render::kMaximumWorldLocalLights>& lights,
    std::uint32_t count) {
  if (count > lights.size()) return false;
  for (std::size_t i = 0; i < lights.size(); ++i) {
    const auto values =
        std::bit_cast<std::array<float, sizeof(render::WorldLocalLight) / sizeof(float)>>(
            lights.at(i));
    if (!std::ranges::all_of(values, [](float value) { return std::isfinite(value); })) {
      return false;
    }
    if (i < count) {
      const auto& attenuation = lights.at(i).attenuation;
      if (!std::ranges::all_of(attenuation, [](float value) { return value >= 0; }) ||
          (attenuation[1] == 0 && attenuation[2] == 0)) {
        return false;
      }
    }
  }
  return true;
}
struct ColorStream {
  std::uint32_t stride{}, position{}, normal{}, tangent{}, uv{}, color{};
  std::uint32_t secondary_uv{}, modulation_color{};
  std::uint32_t normal_map{}, secondary_base{}, environment_map{}, vertex_color{};
  std::uint32_t vertex_illumination{}, reflection{}, fresnel{}, directional_light{};
  std::uint32_t depth_cutout{}, spherical_harmonics{}, base_texture{};
  std::uint32_t joint_indices{}, joint_weights{}, joint_influences{};
  std::uint32_t position_stride{}, ambient_occlusion{};
  std::uint32_t secondary_alpha_blend{}, alpha_fresnel{}, local_lights{};
  std::uint32_t environment_shadow{};
  std::uint32_t normal_required{}, shadow_reception{}, color_alpha_test{};
  std::uint32_t integer_pixel_centers{}, alpha_reference{kDefaultAlphaReference};
};
struct NativeDraw {
  formats::ModelDraw source;
  std::uint32_t position_offset{};
  std::uint32_t index_bytes{};
  bool static_depth_supported{};
  bool requires_deformation{};
  bool shape_deformation{};
  bool joint_indices_present{}, joint_weights_present{};
  std::array<std::uint8_t, 4> maximum_joint_indices{};
  ColorStream color_layout;
  bool normal_present{}, tangent_present{}, uv_present{}, color_present{};
  bool secondary_uv_present{}, modulation_color_present{};
  std::uint32_t source_flags{};
  std::uint16_t source_variant{};
  std::size_t base_texture{}, normal_texture{}, secondary_texture{}, environment_texture{};
  bool base_bound{}, normal_bound{}, secondary_bound{}, environment_bound{};
  std::size_t occlusion_texture{};
  bool occlusion_bound{};
};
struct StreamParameters {
  std::uint32_t stride;
  std::uint32_t position_offset;
};
// Restrict original static depth shading to materials established by the trace.
// Exact shader provenance is retained in the renderer research mapping.
bool supported_depth_material(const formats::ModelMaterial& material) {
  return material.shader_selector[0] == kStaticMaterialFlags &&
         (material.shader_selector[1] & kShaderVariantMask) == 1;
}
bool valid_model_transform(const std::optional<render::WorldJointMatrix>& transform) {
  return !transform ||
         (transform->back() == std::array<float, 4>{0, 0, 0, 1} && math::all_finite(*transform));
}
bool finite_parameters(const render::WorldDepthParameters& parameters) {
  const auto finite = [](float value) { return std::isfinite(value); };
  return std::ranges::all_of(parameters.position_scale, finite) &&
         std::ranges::all_of(parameters.position_offset, finite) &&
         std::ranges::all_of(parameters.depth_bias, finite) &&
         std::ranges::all_of(parameters.clip_rows,
                             [&](const auto& row) { return std::ranges::all_of(row, finite); });
}
}  // namespace

struct WorldRenderer::State {
  struct Model {
    explicit Model(const std::shared_ptr<memory::Domain>& domain)
        : vertices(memory::Allocator<BufferAllocation>(domain)),
          indices(memory::Allocator<BufferAllocation>(domain)),
          shapes(memory::Allocator<BufferAllocation>(domain)),
          draws(memory::Allocator<NativeDraw>(domain)),
          textures(memory::Allocator<render::WorldTextureHandle>(domain)) {}
    render::WorldModelHandle handle;
    render::WorldGeometryStatus status;
    memory::Vector<BufferAllocation> vertices, indices, shapes;
    memory::Vector<NativeDraw> draws;
    memory::Vector<render::WorldTextureHandle> textures;
    std::size_t required_textures{};
  };
  struct ShapeStorage {
    explicit ShapeStorage(const std::shared_ptr<memory::Domain>& domain)
        : buffers(memory::Allocator<BufferAllocation>(domain)) {}
    memory::Vector<BufferAllocation> buffers;
    std::pair<std::uint64_t, std::uint64_t> key;
  };
  struct SubmittedDraw {
    render::WorldColorDraw command;
    ColorStream stream;
    id<MTLSamplerState> sampler{};
    id<MTLDepthStencilState> depth{};
    id<MTLSamplerState> environment_sampler{};
    memory::Vector<render::WorldJointMatrix> joint_matrices;
    render::ShadowReceiverProjection receiver;
    std::shared_ptr<ShapeStorage> shapes;
    memory::Vector<render::WorldTextureHandle> textures;
  };
  struct PreparedDraws {
    memory::Vector<SubmittedDraw> draws;
    std::size_t shadow_guard{};
    bool needs_default_environment{};
  };
  PreparedDraws prepare_draws(std::span<const render::WorldColorDraw> draws,
                              bool retain_view = false);
  struct TextureDestination {
    std::pair<std::uint64_t, std::size_t> key;
    std::size_t submitted_index{};
  };
  // Publication prepares this compact lookup; release only shrinks/rebuilds it.
  static void index_destinations(std::span<const SubmittedDraw> draws,
                                 std::span<TextureDestination> destinations) {
    for (std::size_t index = 0; index < draws.size(); ++index) {
      const auto& command = draws[index].command;
      destinations[index] = {{command.model.value, command.draw}, index};
    }
    std::ranges::sort(destinations, {}, &TextureDestination::key);
  }
  static void index_color_order(std::span<const SubmittedDraw> draws,
                                std::span<std::size_t> order) {
    for (std::size_t index = 0; index < order.size(); ++index) order[index] = index;
    std::ranges::sort(order, [&](std::size_t left, std::size_t right) {
      return std::pair{draws[left].command.color_phase, left} <
             std::pair{draws[right].command.color_phase, right};
    });
  }
  memory::Vector<std::shared_ptr<ShapeStorage>> index_shapes(
      std::span<const SubmittedDraw> draws) const {
    memory::Vector<std::shared_ptr<ShapeStorage>> result{
        memory::Allocator<std::shared_ptr<ShapeStorage>>(cpu_domain)};
    result.reserve(static_cast<std::size_t>(std::ranges::count_if(
        draws, [](const auto& draw) { return static_cast<bool>(draw.shapes); })));
    for (const auto& draw : draws) {
      if (draw.shapes) result.push_back(draw.shapes);
    }
    const auto key = [](const auto& storage) { return storage->key; };
    std::ranges::sort(result, {}, key);
    const auto duplicates = std::ranges::unique(result, {}, key);
    result.erase(duplicates.begin(), duplicates.end());
    return result;
  }
  void refresh_destinations() noexcept {
    // The index is the sole remaining owner when the last consuming draw retires.
    // Shape storage is private to this serialized backend, never lent outside.
    std::erase_if(shape_storage, [](const auto& storage) { return storage.use_count() == 1; });
    texture_destinations.erase(
        texture_destinations.begin() + static_cast<std::ptrdiff_t>(submitted.size()),
        texture_destinations.end());
    index_destinations(submitted, texture_destinations);
    color_order.erase(color_order.begin() + static_cast<std::ptrdiff_t>(submitted.size()),
                      color_order.end());
    index_color_order(submitted, color_order);
  }
  // Membership changes must retain an admitted view's lighting override. With
  // no override, derive coverage from the surviving receiver submissions.
  std::size_t effective_shadow_guard(std::span<const SubmittedDraw> draws,
                                     render::WorldModelHandle excluded = {}) const {
    if (view && view->lighting) {
      return render::shadow_guard_texels(view->lighting->shadow_radius);
    }
    std::size_t guard = 0;
    for (const auto& draw : draws) {
      if (draw.command.model == excluded) continue;
      const auto& shadow = draw.command.parameters.shadow_parameters;
      if (shadow[0] == 1.0F) {
        guard = std::max(guard, render::shadow_guard_texels(shadow[2]));
      }
    }
    return guard;
  }
  struct Texture {
    render::WorldTextureHandle handle;
    TextureAllocation native;
  };
  State(id<MTLDevice> native_device, id<MTLCommandQueue> native_queue,
        std::shared_ptr<memory::Domain> domain, std::shared_ptr<memory::DeviceBudget> budget)
      : device(native_device),
        queue(native_queue),
        cpu_domain(std::move(domain)),
        device_budget(std::move(budget)),
        models(memory::Allocator<Model>(cpu_domain)),
        textures(memory::Allocator<Texture>(cpu_domain)),
        submitted(memory::Allocator<SubmittedDraw>(cpu_domain)),
        texture_destinations(memory::Allocator<TextureDestination>(cpu_domain)),
        color_order(memory::Allocator<std::size_t>(cpu_domain)),
        shape_storage(memory::Allocator<std::shared_ptr<ShapeStorage>>(cpu_domain)),
        output(native_device, cpu_domain, device_budget),
        effects(native_device, cpu_domain, device_budget),
        effect_textures(memory::Allocator<id<MTLTexture>>(cpu_domain)) {}
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLRenderPipelineState> depth_pipeline{};
  id<MTLDepthStencilState> depth_state{};
  std::shared_ptr<memory::Domain> cpu_domain;
  std::shared_ptr<memory::DeviceBudget> device_budget;
  memory::Vector<Model> models;
  memory::Vector<Texture> textures;
  TextureAllocation default_environment;
  TextureAllocation shadow_color, shadow_depth;
  BufferAllocation shadow_jitter;
  id<MTLRenderPipelineState> shadow_pipeline{};
  id<MTLDepthStencilState> shadow_depth_state{}, owner_depth_state{};
  bool shadows_prepared{};
  std::size_t shadow_guard{};
  std::uint64_t next_texture{1};
  std::optional<render::WorldViewUpdate> view;
  std::optional<render::ShadowReceiverProjection> world_receiver, sky_receiver;
  memory::Vector<SubmittedDraw> submitted;
  memory::Vector<TextureDestination> texture_destinations;
  memory::Vector<std::size_t> color_order;
  memory::Vector<std::shared_ptr<ShapeStorage>> shape_storage;
  std::array<std::array<id<MTLRenderPipelineState>, 2>, 2> color_pipelines{};
  std::array<id<MTLRenderPipelineState>, 2> preparation_pipelines{};
  WorldPostprocess output;
  EffectSpritePass effects;
  memory::Vector<id<MTLTexture>> effect_textures;
  std::uint64_t next_identity{1};
  // Merge camera-dependent constants without changing retained material state.
  render::WorldColorParameters parameters(const render::WorldColorDraw& draw) const {
    auto result = draw.parameters;
    if (view && draw.view_space != render::WorldViewSpace::fixed) {
      const auto& selected =
          draw.view_space == render::WorldViewSpace::world ? view->world : view->sky;
      result.clip_rows = selected.clip_rows;
      result.world_rows = selected.world_rows;
      result.fog_color = selected.fog_color;
      result.fog_distance = selected.fog_distance;
      result.fog_height = selected.fog_height;
      result.fog_density = selected.fog_density;
      result.shadow_clip_rows = selected.shadow_clip_rows;
      if (view->lighting) {
        const auto& light = *view->lighting;
        result.light_direction = light.direction;
        result.light_diffuse = light.diffuse;
        result.light_specular = light.specular;
        if (result.shadow_parameters[0] != 0) {
          result.shadow_parameters[2] = light.shadow_radius;
        }
      }
    }
    if (draw.model_transform) {
      result.clip_rows = math::multiply(result.clip_rows, *draw.model_transform);
      result.world_rows = math::multiply(result.world_rows, *draw.model_transform);
      result.shadow_clip_rows = math::multiply(result.shadow_clip_rows, *draw.model_transform);
    }
    return result;
  }
  const Model& model(render::WorldModelHandle identity) const {
    const auto found = std::ranges::find(models, identity, &Model::handle);
    if (found == models.end()) {
      throw std::invalid_argument("Unknown world geometry identity");
    }
    return *found;
  }
};
WorldRenderer::WorldRenderer(id<MTLDevice> device, id<MTLCommandQueue> queue,
                             const std::shared_ptr<memory::Domain>& cpu_domain,
                             std::shared_ptr<memory::DeviceBudget> device_budget) {
  if (device == nil || queue == nil || queue.device != device || !cpu_domain || !device_budget) {
    throw std::invalid_argument("Missing native world renderer capability");
  }
  state_ = std::allocate_shared<State>(memory::Allocator<State>(cpu_domain), device, queue,
                                       cpu_domain, std::move(device_budget));
}
WorldRenderer::~WorldRenderer() = default;
render::WorldModelHandle WorldRenderer::upload_model(const formats::ModelDefinition& model) {
  auto& state = *state_;
  if (model.draws().empty() || state.next_identity == std::numeric_limits<std::uint64_t>::max()) {
    throw std::invalid_argument("Empty model or exhausted world geometry identities");
  }
  State::Model pending(state.cpu_domain);
  pending.draws.reserve(model.draws().size());
  // Validate the supported stream representation before acquiring device memory.
  for (const auto& draw : model.draws()) {
    const auto& elements = model.declarations().at(draw.declaration).elements;
    const auto position = std::ranges::find_if(elements, [](const auto& element) {
      return element.usage == 0 && element.usage_index == 0;
    });
    if (position == elements.end()) {
      throw std::invalid_argument("World geometry has no position declaration");
    }
    const auto& position_stream = draw.shape_stream && position->stream == draw.shape_stream->stream
                                      ? *draw.shape_stream
                                      : draw.stream;
    const bool requires_deformation = model.skeleton().has_value() || draw.shape_stream.has_value();
    if (position->stream != position_stream.stream || position->type != 2 ||
        position->method != 0 || position->offset % 4 != 0 || position_stream.stride % 4 != 0 ||
        position_stream.offset % 4 != 0 || position->offset > position_stream.stride ||
        position_stream.stride - position->offset < kPositionBytes ||
        (draw.primitive_type != kTriangleList && draw.primitive_type != kTriangleStrip)) {
      throw std::invalid_argument("Unsupported world position declaration or primitive topology");
    }
    NativeDraw native{
        .source = draw,
        .position_offset = position->offset,
        .index_bytes = model.index_buffers().at(draw.index_buffer).index_element_bytes,
        .static_depth_supported =
            !requires_deformation && supported_depth_material(model.materials().at(draw.material)),
        .requires_deformation = requires_deformation,
        .shape_deformation = draw.shape_stream.has_value()};
    native.color_layout.stride = draw.stream.stride;
    native.color_layout.position = position->offset;
    native.color_layout.position_stride = position_stream.stride;
    const auto attribute = [&](std::uint8_t usage, std::uint8_t index, std::uint8_t type,
                               std::uint32_t& offset) {
      const auto found = std::ranges::find_if(elements, [&](const auto& element) {
        return element.usage == usage && element.usage_index == index && element.type == type &&
               element.stream == draw.stream.stream && element.method == 0;
      });
      if (found == elements.end() || (type != kNormalizedByte4 && found->offset % 2 != 0)) {
        return false;
      }
      offset = found->offset;
      return true;
    };
    native.normal_present =
        attribute(kNormalUsage, 0, kNormalizedShort4, native.color_layout.normal);
    native.tangent_present =
        attribute(kTangentUsage, 0, kNormalizedShort4, native.color_layout.tangent);
    native.uv_present = attribute(kUvUsage, 1, kHalf2, native.color_layout.uv);
    native.color_present = attribute(kColorUsage, 2, kNormalizedByte4, native.color_layout.color);
    native.secondary_uv_present = attribute(kUvUsage, 2, kHalf2, native.color_layout.secondary_uv);
    native.modulation_color_present =
        attribute(kColorUsage, 0, kNormalizedByte4, native.color_layout.modulation_color);
    constexpr std::uint8_t kBlendUsage = 1, kByte4 = 5;
    native.joint_indices_present =
        attribute(kBlendUsage, 0, kByte4, native.color_layout.joint_indices);
    native.joint_weights_present =
        attribute(kBlendUsage, 1, kNormalizedByte4, native.color_layout.joint_weights);
    // Validate palette range against the vertices actually referenced by this
    // draw, including zero-weight influences that the original program reads.
    if (native.joint_indices_present) {
      const auto vertices = model.vertex_bytes(draw.stream.buffer);
      const auto indices = model.index_bytes(draw.index_buffer);
      const auto count = static_cast<std::size_t>(draw.primitive_count) *
                             (draw.primitive_type == kTriangleList ? 3 : 1) +
                         (draw.primitive_type == kTriangleStrip ? 2 : 0);
      for (std::size_t element = 0; element < count; ++element) {
        const auto index_offset = (draw.start_index + element) * native.index_bytes;
        std::uint32_t vertex_index = 0;
        for (std::uint32_t byte = 0; byte < native.index_bytes; ++byte) {
          vertex_index |= static_cast<std::uint32_t>(indices[index_offset + byte])
                          << (byte * std::numeric_limits<std::uint8_t>::digits);
        }
        const auto vertex =
            static_cast<std::size_t>(static_cast<std::int64_t>(draw.base_vertex) + vertex_index);
        const auto offset =
            draw.stream.offset + vertex * draw.stream.stride + native.color_layout.joint_indices;
        for (std::size_t influence = 0; influence < native.maximum_joint_indices.size();
             ++influence) {
          native.maximum_joint_indices.at(influence) =
              std::max(native.maximum_joint_indices.at(influence), vertices[offset + influence]);
        }
      }
    }
    const auto& material = model.materials().at(draw.material);
    native.source_flags = material.shader_selector[0];
    native.source_variant =
        static_cast<std::uint16_t>(material.shader_selector[1] & kShaderVariantMask);
    constexpr std::uint32_t kSecondaryTexture = 0x42545831, kEnvironmentTexture = 0x4c454e56;
    constexpr std::uint32_t kOcclusionTexture = 0x414d4f43;
    for (const auto& texture : material.textures) {
      if (texture.semantic[0] == kBaseTextureSemantic) {
        native.base_texture = texture.texture;
        native.base_bound = true;
      } else if (texture.semantic[0] == kNormalTextureSemantic) {
        native.normal_texture = texture.texture;
        native.normal_bound = true;
      } else if (texture.semantic[0] == kSecondaryTexture) {
        native.secondary_texture = texture.texture;
        native.secondary_bound = true;
      } else if (texture.semantic[0] == kOcclusionTexture) {
        native.occlusion_texture = texture.texture;
        native.occlusion_bound = true;
      } else if (texture.semantic[0] == kEnvironmentTexture) {
        native.environment_texture = texture.texture;
        native.environment_bound = true;
      }
    }
    pending.draws.push_back(native);
  }
  pending.vertices.reserve(model.vertex_buffers().size());
  pending.indices.reserve(model.index_buffers().size());
  pending.shapes.reserve(model.shape_buffers().size());
  // Owners retain every partial acquisition until the complete model is published.
  for (std::size_t index = 0; index < model.vertex_buffers().size(); ++index) {
    const auto bytes = model.vertex_bytes(index);
    pending.vertices.push_back(BufferAllocation::create(state.device, state.device_budget, bytes));
    pending.status.vertex_bytes += bytes.size();
  }
  // Retain initial rest positions; CPU deformation later updates this same stream.
  for (std::size_t index = 0; index < model.shape_buffers().size(); ++index) {
    const auto bytes = model.shape_bytes(index);
    pending.shapes.push_back(BufferAllocation::create(state.device, state.device_budget, bytes));
    pending.status.vertex_bytes += bytes.size();
  }
  for (std::size_t index = 0; index < model.index_buffers().size(); ++index) {
    const auto bytes = model.index_bytes(index);
    pending.indices.push_back(BufferAllocation::create(state.device, state.device_budget, bytes));
    pending.status.index_bytes += bytes.size();
  }
  for (const auto& material : model.materials()) {
    for (const auto& texture : material.textures) {
      pending.required_textures =
          std::max(pending.required_textures, static_cast<std::size_t>(texture.texture) + 1);
    }
  }
  pending.status.texture_bindings_ready = pending.required_textures == 0;
  pending.status.draws = model.draws().size();
  pending.status.materials = model.materials().size();
  pending.handle = {state.next_identity};
  const auto identity = pending.handle;
  state.models.push_back(std::move(pending));
  ++state.next_identity;
  return identity;
}
render::WorldTextureHandle WorldRenderer::upload_world_texture(
    const formats::WorldTextureImage& image) {
  auto& state = *state_;
  if (state.next_texture == std::numeric_limits<std::uint64_t>::max()) {
    throw std::overflow_error("World texture identities exhausted");
  }
  if (image.format != formats::WorldTextureFormat::rgba8 &&
      image.format != formats::WorldTextureFormat::rgba4 &&
      image.format != formats::WorldTextureFormat::rgba16_float) {
    throw std::invalid_argument("Unsupported world texture representation");
  }
  if (image.shape != formats::WorldTextureShape::two_dimensional &&
      image.shape != formats::WorldTextureShape::cube) {
    throw std::invalid_argument("Unsupported world texture shape");
  }
  const auto format = image.format == formats::WorldTextureFormat::rgba16_float
                          ? MTLPixelFormatRGBA16Float
                          : MTLPixelFormatRGBA8Unorm;
  auto native = TextureAllocation::create(
      state.device, state.device_budget, image.width, image.height, format,
      image.shape == formats::WorldTextureShape::cube ? MTLTextureTypeCube : MTLTextureType2D,
      MTLTextureUsageShaderRead, {.count = image.mip_levels});
  native.upload(image.pixels);
  const render::WorldTextureHandle handle{state.next_texture};
  state.textures.push_back({handle, std::move(native)});
  ++state.next_texture;
  return handle;
}
void WorldRenderer::release_world_texture(render::WorldTextureHandle texture) noexcept {
  if (texture.value == 0 || std::ranges::find(state_->textures, texture, &State::Texture::handle) ==
                                state_->textures.end()) {
    return;
  }
  // Retiring a component's private textures must not invalidate the surviving
  // scene. Remove only consumers of this handle before clearing their bindings.
  state_->shadows_prepared = false;
  for (auto& model : state_->models) {
    if (std::ranges::find(model.textures, texture) == model.textures.end()) continue;
    std::erase_if(state_->submitted,
                  [&](const auto& draw) { return draw.command.model == model.handle; });
    for (auto& binding : model.textures) {
      if (binding == texture) {
        binding = {};
        model.status.texture_bindings_ready = false;
      }
    }
  }
  std::erase_if(state_->submitted, [&](const auto& draw) {
    return draw.command.reflection_texture == texture ||
           std::ranges::find(draw.textures, texture) != draw.textures.end();
  });
  state_->refresh_destinations();
  std::erase_if(state_->textures, [texture](const auto& entry) { return entry.handle == texture; });
}
void WorldRenderer::bind_model_textures(render::WorldModelHandle model,
                                        std::span<const render::WorldTextureHandle> textures) {
  auto& state = *state_;
  const auto found = std::ranges::find(state.models, model, &State::Model::handle);
  if (found == state.models.end() || textures.size() < found->required_textures) {
    throw std::invalid_argument("Missing world model or incomplete original texture table");
  }
  for (const auto handle : textures) {
    if (handle.value != 0 && std::ranges::find(state.textures, handle, &State::Texture::handle) ==
                                 state.textures.end()) {
      throw std::invalid_argument("Unknown world texture identity");
    }
  }
  // Copy only after the complete table validates; a failed replacement preserves it.
  memory::Vector<render::WorldTextureHandle> pending(
      textures.begin(), textures.end(),
      memory::Allocator<render::WorldTextureHandle>(state.cpu_domain));
  state.shadows_prepared = false;
  std::erase_if(state.submitted,
                [model](const auto& entry) { return entry.command.model == model; });
  state.refresh_destinations();
  found->textures = std::move(pending);
  found->status.texture_bindings_ready =
      std::ranges::all_of(found->textures, [](auto handle) { return handle.value != 0; });
}
render::WorldGeometryStatus WorldRenderer::geometry_status(render::WorldModelHandle model) const {
  return state_->model(model).status;
}
void WorldRenderer::release_model(render::WorldModelHandle model) noexcept {
  state_->shadows_prepared = false;
  // Every draw waits for completion, so no earlier submission can retain this model.
  std::erase_if(state_->submitted,
                [model](const auto& entry) { return entry.command.model == model; });
  state_->refresh_destinations();
  std::erase_if(state_->models, [model](const auto& entry) { return entry.handle == model; });
  if (state_->models.empty()) {
    state_->default_environment.reset();
    state_->output.release_scene_targets();
    state_->shadow_color.reset();
    state_->shadow_depth.reset();
    state_->shadow_jitter.reset();
    state_->shadows_prepared = false;
  }
}
WorldRenderer::State::PreparedDraws WorldRenderer::State::prepare_draws(
    std::span<const render::WorldColorDraw> draws, bool retain_view) {
  auto& state = *this;
  memory::Vector<State::SubmittedDraw> pending(
      memory::Allocator<State::SubmittedDraw>(state.cpu_domain));
  std::size_t shadow_guard = 0;
  pending.reserve(draws.size());
  bool needs_default_environment = false;
  // Resolve the complete ordered submission before replacing the previous frame.
  for (const auto& draw : draws) {
    if (draw.color_phase > render::WorldColorPhase::material_blended) {
      throw std::invalid_argument("Unresolved world color phase");
    }
    const auto& model = state.model(draw.model);
    if (draw.draw >= model.draws.size()) {
      throw std::invalid_argument("Unknown world draw identity");
    }
    const auto& native = model.draws.at(draw.draw);
    const auto& raster = draw.state;
    const auto values =
        std::bit_cast<std::array<float, sizeof(render::WorldColorParameters) / sizeof(float)>>(
            draw.parameters);
    const auto requirements = render::world_material_requirements(
        draw.effective_shader_flags, native.source_variant, draw.lighting);
    memory::Vector<render::WorldTextureHandle> texture_table(
        model.textures.begin(), model.textures.end(),
        memory::Allocator<render::WorldTextureHandle>(state.cpu_domain));
    for (std::size_t index = 0; index < draw.texture_overrides.size(); ++index) {
      const auto& replacement = draw.texture_overrides[index];
      if (replacement.slot >= texture_table.size() || replacement.texture.value == 0 ||
          std::ranges::find(state.textures, replacement.texture, &State::Texture::handle) ==
              state.textures.end() ||
          std::ranges::any_of(draw.texture_overrides.first(index), [&](const auto& previous) {
            return previous.slot == replacement.slot;
          })) {
        throw std::invalid_argument("Invalid instance texture-table override");
      }
      texture_table.at(replacement.slot) = replacement.texture;
    }
    const auto resident = [&](std::size_t slot, MTLTextureType type) {
      if (slot >= texture_table.size() || texture_table.at(slot).value == 0) return false;
      const auto texture =
          std::ranges::find(state.textures, texture_table.at(slot), &State::Texture::handle);
      return texture != state.textures.end() && texture->native.get().textureType == type;
    };
    constexpr std::uint32_t kSourcePassFeatures = 0x00e00200;
    const bool omitted_lights =
        draw.lighting == render::WorldLightingPolicy::without_direct_lighting;
    const auto no_energy = [](const auto& light) {
      return std::ranges::all_of(light, [](float value) { return value == 0.0F; });
    };
    constexpr std::size_t kMaximumJoints = 32;
    const bool joint_palette_valid =
        requirements.joint_influences == 0
            ? draw.joint_matrices.empty()
            : native.joint_indices_present && !draw.joint_matrices.empty() &&
                  draw.joint_matrices.size() <= kMaximumJoints &&
                  (requirements.joint_influences == 1 || native.joint_weights_present) &&
                  std::ranges::all_of(
                      std::span(native.maximum_joint_indices).first(requirements.joint_influences),
                      [&](auto index) { return index < draw.joint_matrices.size(); }) &&
                  std::ranges::all_of(draw.joint_matrices, [](const auto& matrix) {
                    return std::ranges::all_of(matrix, [](const auto& row) {
                      return std::ranges::all_of(row,
                                                 [](float value) { return std::isfinite(value); });
                    });
                  });
    const auto& shadow = draw.parameters.shadow_parameters;
    if (shadow[0] == 1.0F) {
      shadow_guard = std::max(shadow_guard, render::shadow_guard_texels(shadow[2]));
    }
    const bool shadow_valid = (shadow[0] == 0.0F || shadow[0] == 1.0F) && shadow[1] >= 0 &&
                              shadow[2] >= 0 && shadow[3] == 0 &&
                              (!draw.casts_shadow || shadow[0] == 1.0F);
    if (!valid_local_lights(draw.parameters.local_lights, draw.parameters.local_light_meta[0]) ||
        !std::ranges::all_of(std::span(draw.parameters.local_light_meta).subspan(1),
                             [](auto value) { return value == 0; }) ||
        draw.view_space > render::WorldViewSpace::sky || !shadow_valid ||
        (draw.owner_opacity && !std::isfinite(*draw.owner_opacity)) ||
        (draw.owner_opacity && draw.color_phase == render::WorldColorPhase::background) ||
        (native.shape_deformation && !draw.use_shape_positions) || !joint_palette_valid ||
        !valid_model_transform(draw.model_transform) ||
        (draw.model_transform && !draw.joint_matrices.empty()) || !requirements.supported ||
        (draw.effective_shader_flags | kSourcePassFeatures) !=
            (native.source_flags | kSourcePassFeatures) ||
        (omitted_lights && (!no_energy(draw.parameters.light_diffuse) ||
                            !no_energy(draw.parameters.light_specular))) ||
        (requirements.base_texture && (!native.uv_present || !native.base_bound ||
                                       !resident(native.base_texture, MTLTextureType2D))) ||
        (requirements.normal_required && !native.normal_present) ||
        (requirements.normal_map && (!native.normal_bound || !native.tangent_present ||
                                     !resident(native.normal_texture, MTLTextureType2D))) ||
        (requirements.ambient_occlusion &&
         (!native.occlusion_bound || !resident(native.occlusion_texture, MTLTextureType2D))) ||
        (requirements.secondary_base && (!native.secondary_uv_present || !native.secondary_bound ||
                                         !resident(native.secondary_texture, MTLTextureType2D))) ||
        (requirements.environment_map && native.environment_bound &&
         !resident(native.environment_texture, MTLTextureTypeCube)) ||
        (requirements.vertex_illumination && !native.color_present) ||
        (requirements.vertex_color && !native.modulation_color_present) ||
        (requirements.reflection && (requirements.directional_light || requirements.local_lights) &&
         draw.parameters.specular[0] < 0) ||
        raster.blend > render::WorldBlend::source_alpha ||
        !std::ranges::all_of(values, [](float value) { return std::isfinite(value); }) ||
        raster.min_filter < render::WorldTextureFilter::nearest ||
        raster.min_filter > render::WorldTextureFilter::anisotropic ||
        raster.maximum_anisotropy == 0 || raster.maximum_anisotropy > kMaximumAnisotropy ||
        (raster.min_filter != render::WorldTextureFilter::anisotropic &&
         raster.maximum_anisotropy != 1) ||
        raster.mag_filter < render::WorldTextureFilter::nearest ||
        raster.mag_filter > render::WorldTextureFilter::linear ||
        raster.mip_filter < render::WorldMipFilter::none ||
        raster.mip_filter > render::WorldMipFilter::linear ||
        raster.address_u < render::WorldTextureAddress::repeat ||
        raster.address_u > render::WorldTextureAddress::clamp ||
        raster.address_v < render::WorldTextureAddress::repeat ||
        raster.address_v > render::WorldTextureAddress::clamp ||
        raster.cull < render::WorldCull::none ||
        raster.cull > render::WorldCull::counter_clockwise ||
        raster.depth_compare < render::WorldDepthCompare::less ||
        raster.depth_compare > render::WorldDepthCompare::always) {
      throw std::invalid_argument("Unresolved original world material, sampling or raster state");
    }
    auto* sampler_descriptor = [[MTLSamplerDescriptor alloc] init];
    sampler_descriptor.minFilter = raster.min_filter == render::WorldTextureFilter::nearest
                                       ? MTLSamplerMinMagFilterNearest
                                       : MTLSamplerMinMagFilterLinear;
    sampler_descriptor.maxAnisotropy = raster.maximum_anisotropy;
    sampler_descriptor.magFilter = raster.mag_filter == render::WorldTextureFilter::linear
                                       ? MTLSamplerMinMagFilterLinear
                                       : MTLSamplerMinMagFilterNearest;
    // Sampling policy does not allocate levels: original default textures have
    // one level when disabled; enabled construction supplies the complete chain.
    sampler_descriptor.mipFilter =
        raster.mip_filter == render::WorldMipFilter::none
            ? MTLSamplerMipFilterNotMipmapped
            : (raster.mip_filter == render::WorldMipFilter::nearest ? MTLSamplerMipFilterNearest
                                                                    : MTLSamplerMipFilterLinear);
    sampler_descriptor.sAddressMode = raster.address_u == render::WorldTextureAddress::repeat
                                          ? MTLSamplerAddressModeRepeat
                                          : MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = raster.address_v == render::WorldTextureAddress::repeat
                                          ? MTLSamplerAddressModeRepeat
                                          : MTLSamplerAddressModeClampToEdge;
    auto* depth_descriptor = [[MTLDepthStencilDescriptor alloc] init];
    depth_descriptor.depthWriteEnabled = raster.depth_write;
    depth_descriptor.depthCompareFunction =
        raster.depth_compare == render::WorldDepthCompare::less ? MTLCompareFunctionLess
        : raster.depth_compare == render::WorldDepthCompare::less_equal
            ? MTLCompareFunctionLessEqual
        : raster.depth_compare == render::WorldDepthCompare::greater ? MTLCompareFunctionGreater
        : raster.depth_compare == render::WorldDepthCompare::greater_equal
            ? MTLCompareFunctionGreaterEqual
            : MTLCompareFunctionAlways;
    id<MTLSamplerState> sampler = [state.device newSamplerStateWithDescriptor:sampler_descriptor];
    id<MTLDepthStencilState> depth =
        [state.device newDepthStencilStateWithDescriptor:depth_descriptor];
    if (sampler == nil || depth == nil) {
      throw std::runtime_error("World material state creation failed");
    }
    auto stream = native.color_layout;
    stream.joint_influences = requirements.joint_influences;
    stream.ambient_occlusion = requirements.ambient_occlusion;
    stream.secondary_alpha_blend = requirements.secondary_alpha_blend;
    stream.alpha_fresnel = requirements.alpha_fresnel;
    stream.local_lights = requirements.local_lights;
    stream.environment_shadow = requirements.environment_shadow;
    stream.normal_required = requirements.normal_required;
    stream.shadow_reception = requirements.shadow_reception;
    stream.color_alpha_test = draw.color_phase == render::WorldColorPhase::masked;
    stream.base_texture = requirements.base_texture;
    stream.normal_map = requirements.normal_map;
    stream.secondary_base = requirements.secondary_base;
    stream.environment_map = requirements.environment_map;
    stream.vertex_color = requirements.vertex_color;
    stream.vertex_illumination = requirements.vertex_illumination;
    stream.reflection = requirements.reflection;
    stream.fresnel = requirements.fresnel;
    stream.directional_light = requirements.directional_light;
    stream.depth_cutout = requirements.depth_cutout;
    stream.spherical_harmonics = requirements.spherical_harmonics;
    // Environment sampling retains its own clamp/linear baseline; stage
    // anisotropic overrides apply only to the four surface texture slots.
    sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.maxAnisotropy = 1;
    sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.mipFilter = MTLSamplerMipFilterLinear;
    id<MTLSamplerState> environment_sampler =
        [state.device newSamplerStateWithDescriptor:sampler_descriptor];
    if (environment_sampler == nil) {
      throw std::runtime_error("World environment sampler creation failed");
    }
    if (draw.reflection_texture && draw.reflection_texture->value != 0) {
      const auto reflection =
          std::ranges::find(state.textures, *draw.reflection_texture, &State::Texture::handle);
      if (reflection == state.textures.end() ||
          reflection->native.get().textureType != MTLTextureTypeCube) {
        throw std::invalid_argument("Invalid receiver environment texture");
      }
    }
    needs_default_environment |= requirements.environment_map && !native.environment_bound;
    memory::Vector<render::WorldJointMatrix> palette(
        draw.joint_matrices.begin(), draw.joint_matrices.end(),
        memory::Allocator<render::WorldJointMatrix>(state.cpu_domain));
    auto command = draw;
    command.texture_overrides = {};
    command.joint_matrices = {};  // Never retain the caller's borrowed storage.
    render::ShadowReceiverProjection receiver{};
    if (requirements.shadow_reception && draw.parameters.shadow_parameters[0] != 0) {
      const auto prepared_receiver =
          receiver_projection(draw.parameters.shadow_clip_rows, draw.parameters.world_rows);
      if (!prepared_receiver) {
        throw std::invalid_argument("Shadow receiver requires world-aligned relative coordinates");
      }
      receiver = *prepared_receiver;
      if (retain_view && state.view && draw.view_space != render::WorldViewSpace::fixed &&
          !(draw.view_space == render::WorldViewSpace::world ? state.world_receiver
                                                             : state.sky_receiver)) {
        throw std::invalid_argument("Retained view has an unsupported shadow receiver basis");
      }
    }
    std::shared_ptr<ShapeStorage> shapes;
    if (native.shape_deformation) {
      const auto previous = std::ranges::find_if(pending, [&](const auto& candidate) {
        return candidate.command.model == draw.model &&
               candidate.command.instance == draw.instance && candidate.shapes;
      });
      if (previous != pending.end()) {
        shapes = previous->shapes;
      } else {
        shapes = std::allocate_shared<ShapeStorage>(
            memory::Allocator<ShapeStorage>(state.cpu_domain), state.cpu_domain);
        shapes->key = {draw.model.value, draw.instance};
        shapes->buffers.reserve(model.shapes.size());
        for (const auto& rest : model.shapes) {
          const auto bytes = std::span<const std::uint8_t>(
              static_cast<const std::uint8_t*>(rest.get().contents), rest.get().length);
          shapes->buffers.push_back(
              BufferAllocation::create(state.device, state.device_budget, bytes));
        }
      }
    }
    pending.push_back({command, stream, sampler, depth, environment_sampler, std::move(palette),
                       receiver, std::move(shapes), std::move(texture_table)});
  }
  return {std::move(pending), shadow_guard, needs_default_environment};
}
void WorldRenderer::set_world_draws(std::span<const render::WorldColorDraw> draws) {
  auto& state = *state_;
  auto prepared = state.prepare_draws(draws);
  auto& pending = prepared.draws;
  const auto shadow_guard = prepared.shadow_guard;
  const auto needs_default_environment = prepared.needs_default_environment;
  if (needs_default_environment && state.default_environment.get() == nil) {
    // Create the original opaque-black environment fallback before applying
    // authored material textures. Keep one accounted
    // native allocation; failed creation/upload cannot replace the prior frame.
    constexpr std::size_t kCubeFaces = 6, kColorChannels = 4;
    constexpr std::array<std::uint8_t, kCubeFaces * kColorChannels> kBlackCube{
        0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255, 0, 0, 0, 255};
    auto fallback = TextureAllocation::create(state.device, state.device_budget, 1, 1,
                                              MTLPixelFormatRGBA8Unorm, MTLTextureTypeCube);
    fallback.upload(kBlackCube);
    state.default_environment = std::move(fallback);
  }
  memory::Vector<State::TextureDestination> destinations(
      pending.size(), memory::Allocator<State::TextureDestination>(state.cpu_domain));
  State::index_destinations(pending, destinations);
  memory::Vector<std::size_t> color_order(pending.size(),
                                          memory::Allocator<std::size_t>(state.cpu_domain));
  State::index_color_order(pending, color_order);
  auto shape_storage = state.index_shapes(pending);
  state.submitted = std::move(pending);
  state.texture_destinations = std::move(destinations);
  state.color_order = std::move(color_order);
  state.shape_storage = std::move(shape_storage);
  state.shadow_guard = shadow_guard;
  state.view.reset();
  state.world_receiver.reset();
  state.sky_receiver.reset();
  state.shadows_prepared = false;
}
// Prepare the complete component change before touching published scene state.
// Unrelated native states and latest constants survive; no GPU assets move.
void WorldRenderer::replace_world_model_draws(render::WorldModelHandle previous,
                                              std::span<const render::WorldColorDraw> replacement) {
  auto& state = *state_;
  const auto is_previous = [&](const auto& draw) { return draw.command.model == previous; };
  if (previous.value == 0 || !std::ranges::any_of(state.submitted, is_previous)) {
    throw std::invalid_argument("World model replacement requires a submitted model");
  }
  if (replacement.empty()) {
    // Explicit removal changes retained membership only. Existing residency,
    // unrelated draws/view and their current constants remain untouched.
    const auto shadow_guard = state.effective_shadow_guard(state.submitted, previous);
    std::erase_if(state.submitted, is_previous);
    state.refresh_destinations();
    state.shadow_guard = shadow_guard;
    state.shadows_prepared = false;
    return;
  }
  const auto next = replacement.front().model;
  if (next.value == 0 || next == previous ||
      !std::ranges::all_of(replacement, [&](const auto& draw) { return draw.model == next; }) ||
      std::ranges::any_of(state.submitted,
                          [&](const auto& draw) { return draw.command.model == next; })) {
    throw std::invalid_argument("World model replacement identities are not distinct and valid");
  }
  auto prepared = state.prepare_draws(replacement, true);
  if (prepared.needs_default_environment && state.default_environment.get() == nil) {
    throw std::invalid_argument("World model replacement requires a resident environment fallback");
  }
  memory::Vector<State::SubmittedDraw> pending(
      memory::Allocator<State::SubmittedDraw>(state.cpu_domain));
  const auto removed =
      static_cast<std::size_t>(std::ranges::count_if(state.submitted, is_previous));
  pending.reserve(state.submitted.size() - removed + prepared.draws.size());
  bool inserted = false;
  for (const auto& draw : state.submitted) {
    if (is_previous(draw)) {
      if (!inserted) {
        for (auto& candidate : prepared.draws) {
          pending.push_back(std::move(candidate));
        }
        inserted = true;
      }
    } else {
      pending.push_back(draw);
    }
  }
  memory::Vector<State::TextureDestination> destinations(
      pending.size(), memory::Allocator<State::TextureDestination>(state.cpu_domain));
  State::index_destinations(pending, destinations);
  memory::Vector<std::size_t> color_order(pending.size(),
                                          memory::Allocator<std::size_t>(state.cpu_domain));
  State::index_color_order(pending, color_order);
  auto shape_storage = state.index_shapes(pending);
  const auto shadow_guard = state.effective_shadow_guard(pending);
  // No operation after this point can fail; view and output remain untouched.
  state.submitted.swap(pending);
  state.texture_destinations.swap(destinations);
  state.color_order.swap(color_order);
  state.shape_storage.swap(shape_storage);
  state.shadow_guard = shadow_guard;
  state.shadows_prepared = false;
}
// Instance setup shares immutable assets and prepares private mutable state before
// one transactional membership commit. The current view and all existing poses survive.
void WorldRenderer::add_world_instance_draws(std::span<const render::WorldColorDraw> draws) {
  auto& state = *state_;
  if (draws.empty()) throw std::invalid_argument("Instance admission requires draws");
  const auto instance = draws.front().instance;
  for (std::size_t index = 0; index < draws.size(); ++index) {
    const auto& candidate = draws[index];
    if (candidate.instance != instance ||
        std::ranges::any_of(state.submitted, [&](const auto& existing) {
          return existing.command.model == candidate.model && existing.command.instance == instance;
        })) {
      throw std::invalid_argument(
          "Instance admission requires fresh model pairs and one owner identity");
    }
    if (std::ranges::any_of(draws.first(index), [&](const auto& previous) {
          return previous.model == candidate.model && previous.draw == candidate.draw;
        })) {
      throw std::invalid_argument("Instance admission contains duplicate draw identities");
    }
  }
  auto prepared = state.prepare_draws(draws, true);
  if (prepared.needs_default_environment && state.default_environment.get() == nil) {
    throw std::invalid_argument("Instance admission requires a resident environment fallback");
  }
  memory::Vector<State::SubmittedDraw> pending(
      state.submitted.begin(), state.submitted.end(),
      memory::Allocator<State::SubmittedDraw>(state.cpu_domain));
  pending.reserve(pending.size() + prepared.draws.size());
  for (auto& draw : prepared.draws) pending.push_back(std::move(draw));
  memory::Vector<State::TextureDestination> destinations(
      pending.size(), memory::Allocator<State::TextureDestination>(state.cpu_domain));
  State::index_destinations(pending, destinations);
  memory::Vector<std::size_t> color_order(pending.size(),
                                          memory::Allocator<std::size_t>(state.cpu_domain));
  State::index_color_order(pending, color_order);
  auto shape_storage = state.index_shapes(pending);
  const auto shadow_guard = state.effective_shadow_guard(pending);
  state.submitted.swap(pending);
  state.texture_destinations.swap(destinations);
  state.color_order.swap(color_order);
  state.shape_storage.swap(shape_storage);
  state.shadow_guard = shadow_guard;
  state.shadows_prepared = false;
}
void WorldRenderer::remove_world_instance_draws(render::WorldModelHandle model,
                                                std::uint64_t instance) {
  auto& state = *state_;
  const auto matches = [&](const auto& draw) {
    return draw.command.model == model && draw.command.instance == instance;
  };
  if (!std::ranges::any_of(state.submitted, matches)) {
    throw std::invalid_argument("Unknown retained world instance");
  }
  std::erase_if(state.submitted, matches);
  state.refresh_destinations();
  state.shadow_guard = state.effective_shadow_guard(state.submitted);
  state.shadows_prepared = false;
}
// Validate the complete batch before changing retained constants; geometry,
// palettes, samplers and shadow coverage remain unchanged (I.1/GPU.1).
void WorldRenderer::update_world_texture_offsets(
    std::span<const render::WorldTextureOffsetUpdate> updates) {
  auto& state = *state_;
  const auto destinations = [&](const auto& update) {
    return std::ranges::equal_range(state.texture_destinations,
                                    std::pair{update.model.value, update.draw}, std::ranges::less{},
                                    &State::TextureDestination::key);
  };
  for (const auto& update : updates) {
    const bool finite = std::ranges::all_of(update.texture_offset, [](const auto& row) {
      return std::ranges::all_of(row, [](float value) { return std::isfinite(value); });
    });
    if (!finite || destinations(update).empty()) {
      throw std::invalid_argument("Invalid retained texture-offset update");
    }
  }
  for (const auto& update : updates) {
    for (const auto& destination : destinations(update)) {
      state.submitted[destination.submitted_index].command.parameters.texture_offset =
          update.texture_offset;
    }
  }
}
// Sampled character irradiance changes constants only; validate the whole
// batch before touching any retained draw, preserving its current pose/view.
void WorldRenderer::update_world_lighting(std::span<const render::WorldLightingUpdate> updates) {
  auto& state = *state_;
  const auto destinations = [&](const auto& update) {
    return std::ranges::equal_range(state.texture_destinations,
                                    std::pair{update.model.value, update.draw}, std::ranges::less{},
                                    &State::TextureDestination::key);
  };
  const auto matches_instance = [&](const auto& update, const auto& destination) {
    return !update.instance ||
           state.submitted[destination.submitted_index].command.instance == *update.instance;
  };
  for (const auto& update : updates) {
    if (update.reflection_texture && update.reflection_texture->value != 0) {
      const auto reflection =
          std::ranges::find(state.textures, *update.reflection_texture, &State::Texture::handle);
      if (reflection == state.textures.end() ||
          reflection->native.get().textureType != MTLTextureTypeCube) {
        throw std::invalid_argument("Invalid receiver environment texture update");
      }
    }
    const bool finite = std::ranges::all_of(update.irradiance_coefficients, [](const auto& row) {
      return std::ranges::all_of(row, [](float value) { return std::isfinite(value); });
    });
    if (!finite || !valid_local_lights(update.local_lights, update.local_light_count) ||
        !std::ranges::any_of(destinations(update), [&](const auto& destination) {
          return matches_instance(update, destination);
        })) {
      throw std::invalid_argument("Invalid retained irradiance/direct-light update");
    }
  }
  for (const auto& update : updates) {
    for (const auto& destination : destinations(update)) {
      if (!matches_instance(update, destination)) continue;
      state.submitted[destination.submitted_index].command.parameters.irradiance_coefficients =
          update.irradiance_coefficients;
      auto& parameters = state.submitted[destination.submitted_index].command.parameters;
      parameters.local_lights = update.local_lights;
      parameters.local_light_meta = {update.local_light_count, 0, 0, 0};
      if (update.reflection_texture) {
        state.submitted[destination.submitted_index].command.reflection_texture =
            update.reflection_texture;
      }
    }
  }
}
// Validate every destination before copying any pose. Palette sizes are fixed
// at publication; retained command indices avoid scanning unrelated stage draws.
void WorldRenderer::update_world_poses(std::span<const render::WorldPoseUpdate> updates) {
  auto& state = *state_;
  const auto destinations = [&](const auto& update) {
    return std::ranges::equal_range(state.texture_destinations,
                                    std::pair{update.model.value, update.draw}, std::ranges::less{},
                                    &State::TextureDestination::key);
  };
  const auto matches = [&](const auto& update, const auto& destination) {
    return !update.instance ||
           state.submitted[destination.submitted_index].command.instance == *update.instance;
  };
  for (const auto& update : updates) {
    const auto selected = destinations(update);
    const bool finite = std::ranges::all_of(update.joint_matrices, [](const auto& matrix) {
      return std::ranges::all_of(matrix, [](const auto& row) {
        return std::ranges::all_of(row, [](float value) { return std::isfinite(value); });
      });
    });
    if (!finite || !valid_model_transform(update.model_transform) ||
        (update.joint_matrices.empty() && !update.model_transform) ||
        (!update.joint_matrices.empty() && update.model_transform) ||
        !std::ranges::any_of(
            selected, [&](const auto& destination) { return matches(update, destination); }) ||
        !std::ranges::all_of(selected, [&](const auto& destination) {
          return !matches(update, destination) ||
                 state.submitted[destination.submitted_index].joint_matrices.size() ==
                     update.joint_matrices.size();
        })) {
      throw std::invalid_argument("Invalid retained world pose update");
    }
  }
  for (const auto& update : updates) {
    for (const auto& destination : destinations(update)) {
      if (!matches(update, destination)) continue;
      auto& submitted = state.submitted[destination.submitted_index];
      std::ranges::copy(update.joint_matrices, submitted.joint_matrices.begin());
      if (update.model_transform) submitted.command.model_transform = update.model_transform;
    }
  }
  if (!updates.empty()) state.shadows_prepared = false;
}
// Shape positions publish once after CPU deformation; both render passes use
// this retained stream. Validation finishes before the first native write.
void WorldRenderer::update_shape_positions(
    std::span<const render::WorldShapePositionUpdate> updates) {
  auto& state = *state_;
  const auto matches = [](const auto& update, const auto& storage) {
    return storage->key.first == update.model.value &&
           (!update.instance || storage->key.second == *update.instance);
  };
  for (const auto& update : updates) {
    const auto& model = state.model(update.model);
    const bool packed_positions = std::ranges::all_of(model.draws, [&](const auto& draw) {
      return !draw.source.shape_stream || draw.source.shape_stream->buffer != update.buffer ||
             (draw.source.shape_stream->stride == sizeof(std::array<float, 3>) &&
              draw.position_offset == 0);
    });
    if (!packed_positions || update.buffer >= model.shapes.size() || update.positions.empty() ||
        !std::ranges::any_of(state.shape_storage,
                             [&](const auto& storage) { return matches(update, storage); }) ||
        update.positions.size_bytes() != model.shapes.at(update.buffer).get().length ||
        !std::ranges::all_of(update.positions, [](const auto& position) {
          return std::ranges::all_of(position, [](float value) { return std::isfinite(value); });
        })) {
      throw std::invalid_argument("Invalid model shape position publication");
    }
  }
  for (const auto& update : updates) {
    for (const auto& storage : state.shape_storage) {
      if (!matches(update, storage)) continue;
      const auto& buffer = storage->buffers.at(update.buffer);
      std::memcpy(buffer.get().contents, update.positions.data(), update.positions.size_bytes());
    }
  }
  if (!updates.empty()) state.shadows_prepared = false;
}
// Preserve the submitted order and resources while changing source visibility.
void WorldRenderer::update_world_visibility(
    std::span<const render::WorldVisibilityUpdate> updates) {
  auto& state = *state_;
  const auto destinations = [&](const auto& update) {
    return std::ranges::equal_range(state.texture_destinations,
                                    std::pair{update.model.value, update.draw}, std::ranges::less{},
                                    &State::TextureDestination::key);
  };
  for (const auto& update : updates) {
    const auto matches = destinations(update);
    if ((update.owner_opacity && !std::isfinite(*update.owner_opacity)) || matches.empty() ||
        (update.owner_opacity &&
         std::ranges::any_of(matches,
                             [&](const auto& destination) {
                               const auto& command =
                                   state.submitted[destination.submitted_index].command;
                               return command.instance ==
                                          update.instance.value_or(command.instance) &&
                                      command.color_phase == render::WorldColorPhase::background;
                             })) ||
        (update.instance && !std::ranges::any_of(matches, [&](const auto& destination) {
           return state.submitted[destination.submitted_index].command.instance == *update.instance;
         }))) {
      throw std::invalid_argument("Invalid retained visibility destination");
    }
  }
  for (const auto& update : updates) {
    for (const auto& destination : destinations(update)) {
      auto& command = state.submitted[destination.submitted_index].command;
      if (update.instance && command.instance != *update.instance) continue;
      command.visible = update.visible;
      if (update.casts_shadow) command.casts_shadow = *update.casts_shadow;
      if (update.owner_opacity) command.owner_opacity = update.owner_opacity;
    }
  }
  if (!updates.empty()) state.shadows_prepared = false;
}
// Validate both spaces before publishing either; the retained packet allocates nothing.
void WorldRenderer::update_world_view(const render::WorldViewUpdate& update) {
  const auto finite = [](const render::WorldViewParameters& parameters) {
    const auto values =
        std::bit_cast<std::array<float, sizeof(render::WorldViewParameters) / sizeof(float)>>(
            parameters);
    return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
  };
  if (!finite(update.world) || !finite(update.sky)) {
    throw std::invalid_argument("Invalid retained world view update");
  }
  auto shadow_guard = state_->shadow_guard;
  if (update.lighting) {
    const auto& light = *update.lighting;
    const auto finite_color = [](const auto& values) {
      return std::ranges::all_of(values, [](float value) { return std::isfinite(value); });
    };
    if (!finite_color(light.direction) || !finite_color(light.diffuse) ||
        !finite_color(light.specular)) {
      throw std::invalid_argument("Invalid shared scene lighting update");
    }
    shadow_guard = render::shadow_guard_texels(light.shadow_radius);
  }
  if (update.scene_passes &&
      !std::ranges::all_of(update.scene_passes->clear_color,
                           [](float value) { return std::isfinite(value); })) {
    throw std::invalid_argument("Invalid scene clear color");
  }
  if (update.shadow_pass) {
    const auto& shadow = *update.shadow_pass;
    if (shadow.cull == render::WorldCull::unresolved ||
        shadow.cull > render::WorldCull::counter_clockwise || !std::isfinite(shadow.depth_bias) ||
        !std::isfinite(shadow.slope_bias)) {
      throw std::invalid_argument("Invalid source shadow pass state");
    }
  }
  const auto world_receiver =
      receiver_projection(update.world.shadow_clip_rows, update.world.world_rows);
  const auto sky_receiver = receiver_projection(update.sky.shadow_clip_rows, update.sky.world_rows);
  for (const auto& draw : state_->submitted) {
    if (draw.stream.shadow_reception && draw.command.parameters.shadow_parameters[0] != 0 &&
        draw.command.view_space != render::WorldViewSpace::fixed &&
        !(draw.command.view_space == render::WorldViewSpace::world ? world_receiver
                                                                   : sky_receiver)) {
      throw std::invalid_argument("View update has an unsupported shadow receiver basis");
    }
  }
  // Commit the view and its derived pixel constants together after validation.
  state_->view = update;
  state_->world_receiver = world_receiver;
  state_->sky_receiver = sky_receiver;
  state_->shadow_guard = shadow_guard;
  state_->shadows_prepared = false;
}
// Resolve only resource identities here; the dedicated sprite pass owns copied
// packets and native buffers. I.11/GPU.1 were read in both phases.
void WorldRenderer::configure_world_sprites(const render::WorldSpriteCapacity& capacity) {
  auto& state = *state_;
  memory::Vector<id<MTLTexture>> textures(memory::Allocator<id<MTLTexture>>(state.cpu_domain));
  textures.reserve(capacity.batches);
  state.effects.configure(capacity);
  state.effect_textures.swap(textures);
}
void WorldRenderer::update_world_sprites(const render::WorldSpriteUpdate& update) {
  auto& state = *state_;
  if (update.batches.size() > state.effect_textures.capacity()) {
    throw std::invalid_argument("Sprite bindings exceed configured capacity");
  }
  state.effect_textures.clear();
  for (const auto& batch : update.batches) {
    const auto found = std::ranges::find(state.textures, batch.texture, &State::Texture::handle);
    if (found == state.textures.end() || found->native.get().textureType != MTLTextureType2D) {
      throw std::invalid_argument("Sprite texture is not resident 2D content");
    }
    state.effect_textures.push_back(found->native.get());
  }
  state.effects.publish(update, state.effect_textures);
}
void WorldRenderer::clear_world_draws() noexcept {
  state_->effects.clear();
  state_->effect_textures.clear();
  state_->view.reset();
  state_->world_receiver.reset();
  state_->sky_receiver.reset();
  state_->submitted.clear();
  state_->shape_storage.clear();
  state_->texture_destinations.clear();
  state_->color_order.clear();
  state_->output.release_scene_targets();
  state_->shadow_color.reset();
  state_->shadow_depth.reset();
  state_->shadow_jitter.reset();
  state_->shadows_prepared = false;
}
void WorldRenderer::set_world_output_parameters(const render::WorldOutputParameters& parameters) {
  state_->output.configure(parameters);
}
void WorldRenderer::prepare_presentation(id<MTLCommandBuffer> command, id<MTLLibrary> library,
                                         std::uint32_t width, std::uint32_t height,
                                         timing::FrameTiming frame) {
  if (!std::isfinite(frame.presentation_seconds) || frame.presentation_seconds < 0) {
    throw std::invalid_argument("Invalid world presentation interval");
  }
  const auto reversed_depth = [](const State::SubmittedDraw& draw) {
    const auto& surface = draw.command.state;
    return surface.depth_compare == render::WorldDepthCompare::greater ||
           surface.depth_compare == render::WorldDepthCompare::greater_equal ||
           (surface.depth_compare == render::WorldDepthCompare::always && !surface.depth_write);
  };
  if (!std::ranges::all_of(state_->submitted, reversed_depth)) {
    throw std::invalid_argument("World presentation requires original reversed-depth submissions");
  }
  prepare_shadows(command, library);
  const auto passes = state_->view ? state_->view->scene_passes : std::nullopt;
  auto encoder = state_->output.begin_scene(command, width, height,
                                            passes ? passes->clear_color : std::array<float, 3>{});
  if (passes && passes->integer_pixel_centers) {
    // Shift raster placement only. Original projected coordinates still feed
    // shadow lookup and interpolation; the fullscreen output path is separate.
    [encoder
        setViewport:MTLViewport{kIntegerSampleCenterOffset, kIntegerSampleCenterOffset,
                                static_cast<double>(width), static_cast<double>(height), 0, 1}];
  }
  encode_color(encoder, library, 1.0F, MTLPixelFormatRGBA16Float);
  // Original sprite command2 is main list45, before output list66.
  state_->effects.encode(encoder, library, MTLPixelFormatRGBA16Float);
  [encoder endEncoding];
  state_->output.prepare_output(command, library, frame);
}
// Retain the map and immutable jitter on device; the pass dependency is the
// command buffer order, with no CPU readback (both-phase I.1/GPU.1).
void WorldRenderer::prepare_shadows(id<MTLCommandBuffer> command, id<MTLLibrary> library) {
  auto& state = *state_;
  if (state.view && state.view->shadow_pass && !state.view->shadow_pass->setup_enabled) {
    // The original disabled setup emits no new map or matrix commands. Keep
    // the preceding completed map, if any, rather than clearing its contents.
    state.shadows_prepared = state.shadow_color.get() != nil;
    return;
  }
  const bool enabled = std::ranges::any_of(state.submitted, [](const auto& draw) {
    return draw.command.visible && draw.command.parameters.shadow_parameters[0] != 0;
  });
  if (!enabled) return;
  if (command == nil || library == nil) {
    throw std::invalid_argument("Missing shadow pass capability");
  }
  constexpr auto kShadowExtent = render::kDirectionalShadowExtent;
  if (state.shadow_color.get() == nil) {
    auto color = TextureAllocation::create(state.device, state.device_budget, kShadowExtent,
                                           kShadowExtent, MTLPixelFormatR32Float, MTLTextureType2D,
                                           MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
    auto depth = TextureAllocation::create(state.device, state.device_budget, kShadowExtent,
                                           kShadowExtent, MTLPixelFormatDepth32Float);
    std::array<std::uint8_t, render::kShadowJitterBytes> samples{};
    render::make_shadow_jitter(samples, 1);
    auto jitter = BufferAllocation::create(state.device, state.device_budget, samples);
    state.shadow_color = std::move(color);
    state.shadow_depth = std::move(depth);
    state.shadow_jitter = std::move(jitter);
  }
  if (state.shadow_pipeline == nil) {
    auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"world_shadow_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"world_shadow_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    NSError* error = nil;
    auto pipeline = [state.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    auto* depth = [[MTLDepthStencilDescriptor alloc] init];
    depth.depthWriteEnabled = YES;
    depth.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    auto depth_state = [state.device newDepthStencilStateWithDescriptor:depth];
    if (pipeline == nil || depth_state == nil) {
      throw std::runtime_error("Shadow pipeline creation failed");
    }
    state.shadow_pipeline = pipeline;
    state.shadow_depth_state = depth_state;
  }
  auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = state.shadow_color.get();
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
  pass.depthAttachment.texture = state.shadow_depth.get();
  pass.depthAttachment.loadAction = MTLLoadActionClear;
  pass.depthAttachment.storeAction = MTLStoreActionDontCare;
  pass.depthAttachment.clearDepth = 0;
  auto encoder = [command renderCommandEncoderWithDescriptor:pass];
  if (encoder == nil) throw std::runtime_error("Shadow encoder creation failed");
  [encoder setRenderPipelineState:state.shadow_pipeline];
  [encoder setDepthStencilState:state.shadow_depth_state];
  const auto shadow_state = state.view && state.view->shadow_pass
                                ? *state.view->shadow_pass
                                : render::WorldViewUpdate::ShadowPass{};
  [encoder setFrontFacingWinding:MTLWindingClockwise];
  [encoder setCullMode:shadow_state.cull == render::WorldCull::none        ? MTLCullModeNone
                       : shadow_state.cull == render::WorldCull::clockwise ? MTLCullModeFront
                                                                           : MTLCullModeBack];
  // D3D9 normalized-depth bias is translated by the shadow fragment's explicit
  // depth output. Metal's device-scaled raster bias must not apply it again.
  [encoder setDepthBias:0 slopeScale:0 clamp:0];
  const std::array<float, 4> depth_bias{shadow_state.depth_bias, shadow_state.slope_bias,
                                        shadow_state.depth_bias, shadow_state.slope_bias};
  [encoder setFragmentBytes:depth_bias.data() length:sizeof(depth_bias) atIndex:3];
  if (state.view && state.view->scene_passes && state.view->scene_passes->integer_pixel_centers) {
    [encoder setViewport:MTLViewport{kIntegerSampleCenterOffset, kIntegerSampleCenterOffset,
                                     kShadowExtent, kShadowExtent, 0, 1}];
  }
  // Clear covers the entire target, including the derived filter footprint.
  // Limit subsequent writes, not the viewport: caster/receiver matrices keep
  // identical normalized coordinates and the outer ring stays at far depth.
  const auto guard = state.shadow_guard;
  [encoder setScissorRect:MTLScissorRect{guard, guard, kShadowExtent - 2 * guard,
                                         kShadowExtent - 2 * guard}];
  for (const auto& submitted : state.submitted) {
    const auto& request = submitted.command;
    auto stream = submitted.stream;
    stream.integer_pixel_centers =
        state.view && state.view->scene_passes && state.view->scene_passes->integer_pixel_centers;
    auto parameters = state.parameters(request);
    if (request.owner_opacity) parameters.indirect_color[3] = *request.owner_opacity;
    if (!request.visible || !request.casts_shadow ||
        (state.view && state.view->lighting && !state.view->lighting->shadow_casters_enabled)) {
      continue;
    }
    const auto& model = state.model(request.model);
    const auto& native = model.draws.at(request.draw);
    const auto& draw = native.source;
    [encoder setVertexBuffer:model.vertices.at(draw.stream.buffer).get()
                      offset:draw.stream.offset
                     atIndex:0];
    // Shape declarations store original rest positions in their own stream;
    // other attributes and immutable skin weights stay in the ordinary stream.
    const auto& positions = draw.shape_stream ? *draw.shape_stream : draw.stream;
    [encoder
        setVertexBuffer:draw.shape_stream ? submitted.shapes->buffers.at(positions.buffer).get()
                                          : model.vertices.at(positions.buffer).get()
                 offset:positions.offset
                atIndex:4];
    [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:1];
    [encoder setVertexBytes:&stream length:sizeof(stream) atIndex:2];
    constexpr render::WorldJointMatrix kUnusedPalette{};
    [encoder setVertexBytes:submitted.joint_matrices.empty() ? &kUnusedPalette
                                                             : submitted.joint_matrices.data()
                     length:submitted.joint_matrices.empty()
                                ? sizeof(kUnusedPalette)
                                : submitted.joint_matrices.size() * sizeof(render::WorldJointMatrix)
                    atIndex:3];
    [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:1];
    [encoder setFragmentBytes:&stream length:sizeof(stream) atIndex:2];
    id<MTLTexture> base = nil;
    if (submitted.stream.base_texture) {
      base = std::ranges::find(state.textures, submitted.textures.at(native.base_texture),
                               &State::Texture::handle)
                 ->native.get();
    }
    [encoder setFragmentTexture:base atIndex:0];
    [encoder setFragmentSamplerState:submitted.sampler atIndex:0];
    const auto count = static_cast<NSUInteger>(draw.primitive_count) *
                           (draw.primitive_type == kTriangleList ? 3 : 1) +
                       (draw.primitive_type == kTriangleStrip ? 2 : 0);
    [encoder
        drawIndexedPrimitives:draw.primitive_type == kTriangleList ? MTLPrimitiveTypeTriangle
                                                                   : MTLPrimitiveTypeTriangleStrip
                   indexCount:count
                    indexType:native.index_bytes == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                  indexBuffer:model.indices.at(draw.index_buffer).get()
            indexBufferOffset:static_cast<NSUInteger>(draw.start_index) * native.index_bytes
                instanceCount:1
                   baseVertex:draw.base_vertex
                 baseInstance:0];
  }
  [encoder endEncoding];
  state.shadows_prepared = true;
}
void WorldRenderer::encode_presentation(id<MTLRenderCommandEncoder> encoder, id<MTLLibrary> library,
                                        float opacity) {
  state_->output.encode_display(encoder, library, opacity);
}
bool WorldRenderer::has_color_draws() const noexcept { return !state_->submitted.empty(); }
void WorldRenderer::encode_color(id<MTLRenderCommandEncoder> encoder, id<MTLLibrary> library,
                                 float presentation_opacity, MTLPixelFormat format) {
  auto& state = *state_;
  if (encoder == nil || library == nil || !std::isfinite(presentation_opacity) ||
      presentation_opacity < 0 || presentation_opacity > 1) {
    throw std::invalid_argument("Missing world color encoder/library");
  }
  if (state.submitted.empty()) {
    return;
  }
  if (format != MTLPixelFormatBGRA8Unorm && format != MTLPixelFormatRGBA16Float) {
    throw std::invalid_argument("Unsupported world color target");
  }
  auto& pipelines = state.color_pipelines.at(format == MTLPixelFormatRGBA16Float ? 1 : 0);
  const auto passes = state.view ? state.view->scene_passes : std::nullopt;
  const auto partial_owner = [](const auto& command) {
    return command.owner_opacity && *command.owner_opacity > 0 && *command.owner_opacity < 1;
  };
  const bool has_partial = std::ranges::any_of(
      state.submitted, [&](const auto& draw) { return partial_owner(draw.command); });
  if (has_partial && state.owner_depth_state == nil) {
    auto* descriptor = [[MTLDepthStencilDescriptor alloc] init];
    descriptor.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    descriptor.depthWriteEnabled = YES;
    state.owner_depth_state = [state.device newDepthStencilStateWithDescriptor:descriptor];
    if (state.owner_depth_state == nil) {
      throw std::runtime_error("Owner opacity depth state creation failed");
    }
  }
  auto& preparation_pipeline =
      state.preparation_pipelines.at(format == MTLPixelFormatRGBA16Float ? 1 : 0);
  if ((has_partial || (passes && (passes->opaque_preparation || passes->masked_preparation))) &&
      preparation_pipeline == nil) {
    auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"world_color_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"world_preparation_fragment"];
    descriptor.colorAttachments[0].pixelFormat = format;
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    NSError* error = nil;
    preparation_pipeline = [state.device newRenderPipelineStateWithDescriptor:descriptor
                                                                        error:&error];
    if (preparation_pipeline == nil) {
      throw std::runtime_error("Original preparation pipeline creation failed");
    }
  }
  // Blend and output precision are immutable pipeline state.
  for (std::size_t index = 0; index < pipelines.size(); ++index) {
    if (pipelines.at(index) != nil) {
      continue;
    }
    auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"world_color_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"world_color_fragment"];
    auto* color = descriptor.colorAttachments[0];
    color.pixelFormat = format;
    if (index == static_cast<std::size_t>(render::WorldBlend::source_alpha)) {
      color.blendingEnabled = YES;
      color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
      color.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      color.rgbBlendOperation = MTLBlendOperationAdd;
      color.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
      color.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
      color.alphaBlendOperation = MTLBlendOperationAdd;
    }
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    NSError* error = nil;
    pipelines.at(index) = [state.device newRenderPipelineStateWithDescriptor:descriptor
                                                                       error:&error];
    if (pipelines.at(index) == nil) {
      throw std::runtime_error("Original surface pipeline creation failed");
    }
  }
  if (!state.shadows_prepared && std::ranges::any_of(state.submitted, [](const auto& draw) {
        return draw.command.visible && draw.command.parameters.shadow_parameters[0] != 0;
      })) {
    throw std::logic_error("Shadow receivers require their prepared map");
  }
  constexpr NSUInteger kShadowDepthSlot = 13;
  [encoder setFragmentTexture:state.shadow_color.get() atIndex:kShadowDepthSlot];
  if (state.shadow_jitter.get() != nil) {
    [encoder setFragmentBuffer:state.shadow_jitter.get() offset:0 atIndex:4];
  } else {
    constexpr std::array<std::uint8_t, 4> kUnusedJitter{};
    [encoder setFragmentBytes:kUnusedJitter.data() length:kUnusedJitter.size() atIndex:4];
  }
  [encoder setFragmentBytes:&presentation_opacity length:sizeof(presentation_opacity) atIndex:0];
  // Consume the prepared scene phase order, preserving original order within
  // each phase and leaving pose/material destination indices unchanged.
  const auto encode_draw = [&](std::size_t submitted_index, bool preparation, bool partial) {
    const auto& submitted = state.submitted[submitted_index];
    const auto& request = submitted.command;
    if (!request.visible || request.owner_opacity.value_or(1) <= 0 ||
        partial_owner(request) != partial) {
      return;
    }
    auto stream = submitted.stream;
    constexpr float kOwnerAlphaReferenceScale = 128;
    if (partial) {
      stream.alpha_reference =
          static_cast<std::uint32_t>(request.owner_opacity.value_or(1) * kOwnerAlphaReferenceScale);
    }
    stream.integer_pixel_centers = passes && passes->integer_pixel_centers;
    auto parameters = state.parameters(request);
    if (request.owner_opacity) parameters.indirect_color[3] = *request.owner_opacity;
    const auto& model = state.model(request.model);
    const auto& native = model.draws.at(request.draw);
    const auto& draw = native.source;
    const auto base =
        submitted.stream.base_texture
            ? std::ranges::find(state.textures, submitted.textures.at(native.base_texture),
                                &State::Texture::handle)
            : state.textures.end();
    const auto normal =
        submitted.stream.normal_map
            ? std::ranges::find(state.textures, submitted.textures.at(native.normal_texture),
                                &State::Texture::handle)
            : state.textures.end();
    const auto blend = partial && request.color_phase != render::WorldColorPhase::material_blended
                           ? render::WorldBlend::source_alpha
                           : request.state.blend;
    [encoder setRenderPipelineState:preparation ? preparation_pipeline
                                                : pipelines.at(static_cast<std::size_t>(blend))];
    [encoder setDepthStencilState:partial ? state.owner_depth_state : submitted.depth];
    [encoder setFrontFacingWinding:MTLWindingClockwise];
    [encoder setCullMode:request.state.cull == render::WorldCull::none        ? MTLCullModeNone
                         : request.state.cull == render::WorldCull::clockwise ? MTLCullModeFront
                                                                              : MTLCullModeBack];
    [encoder setVertexBuffer:model.vertices.at(draw.stream.buffer).get()
                      offset:draw.stream.offset
                     atIndex:0];
    // Shape declarations store original rest positions in their own stream;
    // other attributes and immutable skin weights stay in the ordinary stream.
    const auto& positions = draw.shape_stream ? *draw.shape_stream : draw.stream;
    [encoder
        setVertexBuffer:draw.shape_stream ? submitted.shapes->buffers.at(positions.buffer).get()
                                          : model.vertices.at(positions.buffer).get()
                 offset:positions.offset
                atIndex:4];
    [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:1];
    [encoder setVertexBytes:&stream length:sizeof(stream) atIndex:2];
    if (!submitted.joint_matrices.empty()) {
      [encoder setVertexBytes:submitted.joint_matrices.data()
                       length:submitted.joint_matrices.size() * sizeof(render::WorldJointMatrix)
                      atIndex:3];
    } else {
      constexpr render::WorldJointMatrix kUnusedPalette{};
      [encoder setVertexBytes:&kUnusedPalette length:sizeof(kUnusedPalette) atIndex:3];
    }
    [encoder setFragmentBytes:&stream length:sizeof(stream) atIndex:2];
    constexpr std::array<float, 4> kMainDepthBias{};
    [encoder setFragmentBytes:kMainDepthBias.data() length:sizeof(kMainDepthBias) atIndex:3];
    [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:1];
    const auto* receiver = &submitted.receiver;
    if (stream.shadow_reception && parameters.shadow_parameters[0] != 0 && state.view &&
        request.view_space != render::WorldViewSpace::fixed) {
      receiver = &*(request.view_space == render::WorldViewSpace::world ? state.world_receiver
                                                                        : state.sky_receiver);
    }
    constexpr NSUInteger kReceiverProjectionSlot = 5;
    [encoder setFragmentBytes:receiver length:sizeof(*receiver) atIndex:kReceiverProjectionSlot];
    [encoder setFragmentTexture:submitted.stream.base_texture ? base->native.get() : nil atIndex:0];
    [encoder setFragmentTexture:submitted.stream.normal_map ? normal->native.get() : nil atIndex:3];
    id<MTLTexture> occlusion = nil;
    if (submitted.stream.ambient_occlusion) {
      occlusion = std::ranges::find(state.textures, submitted.textures.at(native.occlusion_texture),
                                    &State::Texture::handle)
                      ->native.get();
    }
    constexpr NSUInteger kOcclusionSlot = 5;
    [encoder setFragmentTexture:occlusion atIndex:kOcclusionSlot];
    if (submitted.stream.secondary_base) {
      const auto secondary = std::ranges::find(
          state.textures, submitted.textures.at(native.secondary_texture), &State::Texture::handle);
      [encoder setFragmentTexture:secondary->native.get() atIndex:1];
    } else {
      [encoder setFragmentTexture:nil atIndex:1];
    }
    constexpr NSUInteger kEnvironmentSlot = 7;
    [encoder setFragmentTexture:state.default_environment.get() atIndex:kEnvironmentSlot];
    if (submitted.stream.environment_map && request.reflection_texture &&
        request.reflection_texture->value != 0) {
      const auto selected =
          std::ranges::find(state.textures, *request.reflection_texture, &State::Texture::handle);
      [encoder setFragmentTexture:selected->native.get() atIndex:kEnvironmentSlot];
    }
    // Source receiver publication precedes the model command list. A later
    // authored LENV record overrides that default on the selected material.
    if (submitted.stream.environment_map && native.environment_bound) {
      const auto environment =
          std::ranges::find(state.textures, submitted.textures.at(native.environment_texture),
                            &State::Texture::handle);
      [encoder setFragmentTexture:environment->native.get() atIndex:kEnvironmentSlot];
    }
    [encoder setFragmentSamplerState:submitted.environment_sampler atIndex:kEnvironmentSlot];
    [encoder setFragmentSamplerState:submitted.sampler atIndex:0];
    const auto primitive = draw.primitive_type == kTriangleList ? MTLPrimitiveTypeTriangle
                                                                : MTLPrimitiveTypeTriangleStrip;
    const auto count = static_cast<NSUInteger>(draw.primitive_count) *
                           (draw.primitive_type == kTriangleList ? 3 : 1) +
                       (draw.primitive_type == kTriangleStrip ? 2 : 0);
    [encoder drawIndexedPrimitives:primitive
                        indexCount:count
                         indexType:native.index_bytes == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                       indexBuffer:model.indices.at(draw.index_buffer).get()
                 indexBufferOffset:static_cast<NSUInteger>(draw.start_index) * native.index_bytes
                     instanceCount:1
                        baseVertex:draw.base_vertex
                      baseInstance:0];
  };
  const auto phase = [&](std::size_t index) { return state.submitted[index].command.color_phase; };
  const auto first_surface = std::ranges::lower_bound(
      state.color_order, render::WorldColorPhase::opaque, std::ranges::less{}, phase);
  const auto first_late = std::ranges::upper_bound(
      state.color_order, render::WorldColorPhase::masked, std::ranges::less{}, phase);
  // Source owner sections are separate from material chunks. Complete Main
  // first, then Trans S0/S1/T0/T1/late, reusing prepared order and identities.
  for (auto it = state.color_order.begin(); it != first_surface; ++it) {
    encode_draw(*it, false, false);
  }
  for (const bool partial : {false, true}) {
    for (auto it = first_surface; it != first_late; ++it) {
      const bool enabled = partial || (passes && (phase(*it) == render::WorldColorPhase::opaque
                                                      ? passes->opaque_preparation
                                                      : passes->masked_preparation));
      if (enabled) encode_draw(*it, true, partial);
    }
    for (auto it = first_surface; it != state.color_order.end(); ++it) {
      encode_draw(*it, false, partial);
    }
  }
}
void WorldRenderer::draw_depth(render::WorldModelHandle model, std::span<const std::size_t> draws,
                               const render::WorldDepthParameters& parameters,
                               id<MTLLibrary> library, id<MTLTexture> color, id<MTLTexture> depth) {
  auto& state = *state_;
  const auto& geometry = state.model(model);
  if (library == nil || color == nil || depth == nil || color.device != state.device ||
      depth.device != state.device || color.pixelFormat != MTLPixelFormatR32Float ||
      depth.pixelFormat != MTLPixelFormatDepth32Float || color.width != depth.width ||
      color.height != depth.height || color.sampleCount != 1 || depth.sampleCount != 1 ||
      !finite_parameters(parameters)) {
    throw std::invalid_argument("Invalid world depth pass attachments or constants");
  }
  // An unsupported selected record rejects the whole pass, never a partial view.
  for (const auto index : draws) {
    if (index >= geometry.draws.size() || !geometry.draws.at(index).static_depth_supported) {
      throw std::invalid_argument("Unsupported original static depth material or draw identity");
    }
  }
  @autoreleasepool {
    if (state.depth_pipeline == nil) {
      auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
      descriptor.vertexFunction = [library newFunctionWithName:@"world_depth_vertex"];
      descriptor.fragmentFunction = [library newFunctionWithName:@"world_depth_fragment"];
      descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatR32Float;
      descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
      NSError* error = nil;
      id<MTLRenderPipelineState> pipeline =
          [state.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
      auto* depth_descriptor = [[MTLDepthStencilDescriptor alloc] init];
      depth_descriptor.depthCompareFunction = MTLCompareFunctionLessEqual;
      depth_descriptor.depthWriteEnabled = YES;
      id<MTLDepthStencilState> depth_state =
          [state.device newDepthStencilStateWithDescriptor:depth_descriptor];
      if (pipeline == nil || depth_state == nil) {
        throw std::runtime_error("World depth pipeline creation failed");
      }
      state.depth_pipeline = pipeline;
      state.depth_state = depth_state;
    }
    auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(1, 1, 1, 1);
    pass.depthAttachment.texture = depth;
    pass.depthAttachment.loadAction = MTLLoadActionClear;
    pass.depthAttachment.storeAction = MTLStoreActionStore;
    pass.depthAttachment.clearDepth = 1;
    id<MTLCommandBuffer> command = [state.queue commandBuffer];
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (command == nil || encoder == nil) {
      throw std::runtime_error("World depth encoding failed");
    }
    [encoder setRenderPipelineState:state.depth_pipeline];
    [encoder setDepthStencilState:state.depth_state];
    // This explicit two-sided contract is for depth verification, not stage visibility.
    [encoder setCullMode:MTLCullModeNone];
    [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:1];
    [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:1];
    for (const auto index : draws) {
      const auto& entry = geometry.draws.at(index);
      const auto& draw = entry.source;
      const StreamParameters stream{draw.stream.stride, entry.position_offset};
      [encoder setVertexBuffer:geometry.vertices.at(draw.stream.buffer).get()
                        offset:draw.stream.offset
                       atIndex:0];
      [encoder setVertexBytes:&stream length:sizeof(stream) atIndex:2];
      const auto primitive = draw.primitive_type == kTriangleList ? MTLPrimitiveTypeTriangle
                                                                  : MTLPrimitiveTypeTriangleStrip;
      const auto count = static_cast<NSUInteger>(draw.primitive_count) *
                             (draw.primitive_type == kTriangleList ? 3 : 1) +
                         (draw.primitive_type == kTriangleStrip ? 2 : 0);
      [encoder
          drawIndexedPrimitives:primitive
                     indexCount:count
                      indexType:entry.index_bytes == 2 ? MTLIndexTypeUInt16 : MTLIndexTypeUInt32
                    indexBuffer:geometry.indices.at(draw.index_buffer).get()
              indexBufferOffset:static_cast<NSUInteger>(draw.start_index) * entry.index_bytes
                  instanceCount:1
                     baseVertex:draw.base_vertex
                   baseInstance:0];
    }
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusError) {
      throw std::runtime_error("World depth device execution failed");
    }
  }
}
}  // namespace bw::platform::macos
