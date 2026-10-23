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
#include "platform/macos/world_renderer.hpp"
#include "platform/macos/world_postprocess.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "bw/formats/model.hpp"
#include "bw/formats/world_texture.hpp"
#include "bw/memory/allocator.hpp"
#include "bw/render/world_material.hpp"
#include "platform/macos/buffer_allocation.hpp"
#include "platform/macos/texture_allocation.hpp"

namespace bw::platform::macos {
namespace {
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
struct ColorStream {
  std::uint32_t stride{}, position{}, normal{}, tangent{}, uv{}, color{};
  std::uint32_t secondary_uv{}, modulation_color{};
  std::uint32_t normal_map{}, secondary_base{}, environment_map{}, vertex_color{};
  std::uint32_t vertex_illumination{}, reflection{}, fresnel{}, directional_light{};
  std::uint32_t alpha_cutout{}, spherical_harmonics{};
};
struct NativeDraw {
  formats::ModelDraw source;
  std::uint32_t position_offset{};
  std::uint32_t index_bytes{};
  bool static_depth_supported{};
  bool requires_deformation{};
  ColorStream color_layout;
  bool normal_present{}, tangent_present{}, uv_present{}, color_present{};
  bool secondary_uv_present{}, modulation_color_present{};
  std::uint32_t source_flags{};
  std::uint16_t source_variant{};
  std::size_t base_texture{}, normal_texture{}, secondary_texture{}, environment_texture{};
  bool base_bound{}, normal_bound{}, secondary_bound{}, environment_bound{};
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
  struct SubmittedDraw {
    render::WorldColorDraw command;
    ColorStream stream;
    id<MTLSamplerState> sampler{};
    id<MTLDepthStencilState> depth{};
    id<MTLSamplerState> environment_sampler{};
  };
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
        output(native_device, cpu_domain, device_budget) {}
  id<MTLDevice> device;
  id<MTLCommandQueue> queue;
  id<MTLRenderPipelineState> depth_pipeline{};
  id<MTLDepthStencilState> depth_state{};
  std::shared_ptr<memory::Domain> cpu_domain;
  std::shared_ptr<memory::DeviceBudget> device_budget;
  memory::Vector<Model> models;
  memory::Vector<Texture> textures;
  TextureAllocation default_environment;
  std::uint64_t next_texture{1};
  memory::Vector<SubmittedDraw> submitted;
  std::array<std::array<id<MTLRenderPipelineState>, 2>, 2> color_pipelines{};
  WorldPostprocess output;
  std::uint64_t next_identity{1};
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
        .requires_deformation = requires_deformation};
    native.color_layout.stride = draw.stream.stride;
    native.color_layout.position = position->offset;
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
    const auto& material = model.materials().at(draw.material);
    native.source_flags = material.shader_selector[0];
    native.source_variant =
        static_cast<std::uint16_t>(material.shader_selector[1] & kShaderVariantMask);
    constexpr std::uint32_t kSecondaryTexture = 0x42545831, kEnvironmentTexture = 0x4c454e56;
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
  // Keep shape-position storage resident without presenting its undeformed pose.
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
      image.shape == formats::WorldTextureShape::cube ? MTLTextureTypeCube : MTLTextureType2D);
  native.upload(image.pixels);
  const render::WorldTextureHandle handle{state.next_texture};
  state.textures.push_back({handle, std::move(native)});
  ++state.next_texture;
  return handle;
}
void WorldRenderer::release_world_texture(render::WorldTextureHandle texture) noexcept {
  // Invalidate submitted work before removing any of its texture bindings.
  state_->submitted.clear();
  // Invalidate each retained table binding before the native owner retires.
  for (auto& model : state_->models) {
    for (auto& binding : model.textures) {
      if (binding == texture) {
        binding = {};
        model.status.texture_bindings_ready = false;
      }
    }
  }
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
  std::erase_if(state.submitted,
                [model](const auto& entry) { return entry.command.model == model; });
  found->textures = std::move(pending);
  found->status.texture_bindings_ready =
      std::ranges::all_of(found->textures, [](auto handle) { return handle.value != 0; });
}
render::WorldGeometryStatus WorldRenderer::geometry_status(render::WorldModelHandle model) const {
  return state_->model(model).status;
}
void WorldRenderer::release_model(render::WorldModelHandle model) noexcept {
  // Every draw waits for completion, so no earlier submission can retain this model.
  std::erase_if(state_->submitted,
                [model](const auto& entry) { return entry.command.model == model; });
  std::erase_if(state_->models, [model](const auto& entry) { return entry.handle == model; });
  if (state_->models.empty()) {
    state_->default_environment.reset();
    state_->output.reset();
  }
}
void WorldRenderer::set_world_draws(std::span<const render::WorldColorDraw> draws) {
  auto& state = *state_;
  memory::Vector<State::SubmittedDraw> pending(
      memory::Allocator<State::SubmittedDraw>(state.cpu_domain));
  pending.reserve(draws.size());
  bool needs_default_environment = false;
  // Resolve the complete ordered submission before replacing the previous frame.
  for (const auto& draw : draws) {
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
    const auto resident = [&](std::size_t slot, MTLTextureType type) {
      if (slot >= model.textures.size() || model.textures.at(slot).value == 0) {
        return false;
      }
      const auto texture =
          std::ranges::find(state.textures, model.textures.at(slot), &State::Texture::handle);
      return texture != state.textures.end() && texture->native.get().textureType == type;
    };
    constexpr std::uint32_t kInspectionPassBits = 0x00e00200;
    const bool omitted_lights =
        draw.lighting == render::WorldLightingPolicy::without_direct_lighting;
    const auto no_energy = [](const auto& light) {
      return std::ranges::all_of(light, [](float value) { return value == 0.0F; });
    };
    if (native.requires_deformation || !requirements.supported ||
        (draw.effective_shader_flags | kInspectionPassBits) !=
            (native.source_flags | kInspectionPassBits) ||
        (omitted_lights && (!no_energy(draw.parameters.light_diffuse) ||
                            !no_energy(draw.parameters.light_specular))) ||
        !native.uv_present || !native.base_bound ||
        !resident(native.base_texture, MTLTextureType2D) ||
        ((requirements.directional_light || requirements.spherical_harmonics) &&
         !native.normal_present) ||
        (requirements.normal_map && (!native.normal_bound || !native.tangent_present ||
                                     !resident(native.normal_texture, MTLTextureType2D))) ||
        (requirements.secondary_base && (!native.secondary_uv_present || !native.secondary_bound ||
                                         !resident(native.secondary_texture, MTLTextureType2D))) ||
        (requirements.environment_map && native.environment_bound &&
         !resident(native.environment_texture, MTLTextureTypeCube)) ||
        (requirements.vertex_illumination && !native.color_present) ||
        (requirements.vertex_color && !native.modulation_color_present) ||
        raster.blend > render::WorldBlend::source_alpha ||
        !std::ranges::all_of(values, [](float value) { return std::isfinite(value); }) ||
        raster.min_filter < render::WorldTextureFilter::nearest ||
        raster.min_filter > render::WorldTextureFilter::anisotropic ||
        raster.maximum_anisotropy == 0 || raster.maximum_anisotropy > kMaximumAnisotropy ||
        (raster.min_filter != render::WorldTextureFilter::anisotropic &&
         raster.maximum_anisotropy != 1) ||
        raster.mag_filter < render::WorldTextureFilter::nearest ||
        raster.mag_filter > render::WorldTextureFilter::linear ||
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
    sampler_descriptor.mipFilter = MTLSamplerMipFilterNotMipmapped;
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
    stream.normal_map = requirements.normal_map;
    stream.secondary_base = requirements.secondary_base;
    stream.environment_map = requirements.environment_map;
    stream.vertex_color = requirements.vertex_color;
    stream.vertex_illumination = requirements.vertex_illumination;
    stream.reflection = requirements.reflection;
    stream.fresnel = requirements.fresnel;
    stream.directional_light = requirements.directional_light;
    stream.alpha_cutout = requirements.alpha_cutout;
    stream.spherical_harmonics = requirements.spherical_harmonics;
    // Environment sampling retains its own clamp/linear baseline; stage
    // anisotropic overrides apply only to the four surface texture slots.
    sampler_descriptor.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_descriptor.maxAnisotropy = 1;
    sampler_descriptor.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.tAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_descriptor.rAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> environment_sampler =
        [state.device newSamplerStateWithDescriptor:sampler_descriptor];
    if (environment_sampler == nil) {
      throw std::runtime_error("World environment sampler creation failed");
    }
    needs_default_environment |= requirements.environment_map && !native.environment_bound;
    pending.push_back({draw, stream, sampler, depth, environment_sampler});
  }
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
  state.submitted = std::move(pending);
}
void WorldRenderer::clear_world_draws() noexcept {
  state_->submitted.clear();
  state_->output.reset();
}
void WorldRenderer::set_world_output_parameters(const render::WorldOutputParameters& parameters) {
  state_->output.configure(parameters);
}
void WorldRenderer::prepare_presentation(id<MTLCommandBuffer> command, id<MTLLibrary> library,
                                         std::uint32_t width, std::uint32_t height) {
  const auto reversed_depth = [](const State::SubmittedDraw& draw) {
    const auto& surface = draw.command.state;
    return surface.depth_compare == render::WorldDepthCompare::greater ||
           surface.depth_compare == render::WorldDepthCompare::greater_equal ||
           (surface.depth_compare == render::WorldDepthCompare::always && !surface.depth_write);
  };
  if (!std::ranges::all_of(state_->submitted, reversed_depth)) {
    throw std::invalid_argument("World presentation requires original reversed-depth submissions");
  }
  auto encoder = state_->output.begin_scene(command, width, height);
  encode_color(encoder, library, 1.0F, MTLPixelFormatRGBA16Float);
  [encoder endEncoding];
  state_->output.prepare_output(command, library);
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
  [encoder setFragmentBytes:&presentation_opacity length:sizeof(presentation_opacity) atIndex:0];
  // Preserve caller-selected draw order, shader constants and native index ranges.
  for (const auto& submitted : state.submitted) {
    const auto& request = submitted.command;
    const auto& model = state.model(request.model);
    const auto& native = model.draws.at(request.draw);
    const auto& draw = native.source;
    const auto base = std::ranges::find(state.textures, model.textures.at(native.base_texture),
                                        &State::Texture::handle);
    const auto normal =
        submitted.stream.normal_map
            ? std::ranges::find(state.textures, model.textures.at(native.normal_texture),
                                &State::Texture::handle)
            : state.textures.end();
    [encoder setRenderPipelineState:pipelines.at(static_cast<std::size_t>(request.state.blend))];
    [encoder setDepthStencilState:submitted.depth];
    [encoder setFrontFacingWinding:MTLWindingClockwise];
    [encoder setCullMode:request.state.cull == render::WorldCull::none        ? MTLCullModeNone
                         : request.state.cull == render::WorldCull::clockwise ? MTLCullModeFront
                                                                              : MTLCullModeBack];
    [encoder setVertexBuffer:model.vertices.at(draw.stream.buffer).get()
                      offset:draw.stream.offset
                     atIndex:0];
    [encoder setVertexBytes:&request.parameters length:sizeof(request.parameters) atIndex:1];
    [encoder setVertexBytes:&submitted.stream length:sizeof(submitted.stream) atIndex:2];
    [encoder setFragmentBytes:&submitted.stream length:sizeof(submitted.stream) atIndex:2];
    [encoder setFragmentBytes:&request.parameters length:sizeof(request.parameters) atIndex:1];
    [encoder setFragmentTexture:base->native.get() atIndex:0];
    [encoder setFragmentTexture:submitted.stream.normal_map ? normal->native.get() : nil atIndex:3];
    if (submitted.stream.secondary_base) {
      const auto secondary = std::ranges::find(
          state.textures, model.textures.at(native.secondary_texture), &State::Texture::handle);
      [encoder setFragmentTexture:secondary->native.get() atIndex:1];
    } else {
      [encoder setFragmentTexture:nil atIndex:1];
    }
    constexpr NSUInteger kEnvironmentSlot = 7;
    [encoder setFragmentTexture:state.default_environment.get() atIndex:kEnvironmentSlot];
    if (submitted.stream.environment_map && native.environment_bound) {
      const auto environment = std::ranges::find(
          state.textures, model.textures.at(native.environment_texture), &State::Texture::handle);
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
