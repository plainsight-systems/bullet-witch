// Headless native Metal contracts inside the existing memory-tests scenario.
// Authored geometry/pixels exercise original depth addressing, surface shading,
// fog and presentation fade; no original game bytes or Cocoa window are used.
// Load the build's compiled shaders, submit real device work and read completed
// pixels. Check admission rollback, partial residency, resource invalidation,
// native/logical accounting and release after completion. Device absence fails.
// No full shader or original-game visual equivalence follows from these cases.
// Both MCP phases: E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) preserve
// ownership until GPU completion and bounded test storage. Readback is test-only.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

#include "bw/formats/model.hpp"
#include "bw/formats/world_texture.hpp"
#include "model_fixture.hpp"
#include "platform/macos/buffer_allocation.hpp"
#include "platform/macos/cpu_backing.hpp"
#include "platform/macos/texture_allocation.hpp"
#include "platform/macos/world_renderer.hpp"

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
// Exercise native indexed addressing and original depth equations with authored data.
void world_geometry_contract(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  id<MTLCommandQueue> queue = [device newCommandQueue];
  bw::platform::macos::WorldRenderer renderer(device, queue, domain, budget);
  constexpr std::array<float, 12> vertices{99, 99, 99, -1, -1, 0.25F, 1, -1, 0.25F, 0, 1, 0.25F};
  constexpr std::array<std::uint16_t, 4> indices{65535, 0, 1, 2};
  const auto bytes = model_fixture::archive(
      {.positions = vertices, .indices = indices, .draw = {4, 1, 0, 3, 1, 1}});
  auto model =
      bw::formats::ModelDefinition::parse(bw::resources::ResourceArchive::parse(bytes), domain);
  const auto handle = renderer.upload_model(model);
  require(
      renderer.geometry_status(handle).vertex_bytes == vertices.size() * sizeof(float) &&
          renderer.geometry_status(handle).index_bytes == indices.size() * sizeof(std::uint16_t) &&
          !renderer.geometry_status(handle).color_materials_ready &&
          budget->snapshot().live_resources == 2,
      "Geometry upload retains exact buffers without claiming color material readiness");
  constexpr std::uint32_t extent = 32;
  auto* color_descriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatR32Float
                                                         width:extent
                                                        height:extent
                                                     mipmapped:NO];
  color_descriptor.storageMode = MTLStorageModeShared;
  color_descriptor.usage = MTLTextureUsageRenderTarget;
  id<MTLTexture> color = [device newTextureWithDescriptor:color_descriptor];
  auto* depth_descriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                                                         width:extent
                                                        height:extent
                                                     mipmapped:NO];
  depth_descriptor.storageMode = MTLStorageModePrivate;
  depth_descriptor.usage = MTLTextureUsageRenderTarget;
  id<MTLTexture> depth = [device newTextureWithDescriptor:depth_descriptor];
  NSError* error = nil;
  NSURL* library_url = [NSURL fileURLWithPath:@BW_WORLD_SHADER_LIBRARY];
  id<MTLLibrary> library = [device newLibraryWithURL:library_url error:&error];
  require(color != nil && depth != nil && library != nil,
          "Native depth fixtures and compiled shaders available");
  bw::render::WorldDepthParameters parameters;
  for (std::size_t i = 0; i < 4; ++i) {
    parameters.clip_rows.at(i).at(i) = 1;
  }
  parameters.depth_bias[0] = 0.125F;
  constexpr std::array<std::size_t, 1> selected{0};
  renderer.draw_depth(handle, selected, parameters, library, color, depth);
  float pixel{};
  [color getBytes:&pixel
      bytesPerRow:sizeof(float)
       fromRegion:MTLRegionMake2D(extent / 2, extent / 2, 1, 1)
      mipmapLevel:0];
  require(
      std::abs(pixel - 0.375F) < 0.00001F,
      "Original depth equations preserve start index, base vertex, projection and constant bias");
  bool rejected = false;
  try {
    constexpr std::array<std::size_t, 1> invalid{1};
    renderer.draw_depth(handle, invalid, parameters, library, color, depth);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Invalid draw selection rejects before encoding");
  auto unsupported = bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(model_fixture::archive({.shader = {0, 0}})), domain);
  const auto unsupported_handle = renderer.upload_model(unsupported);
  rejected = false;
  try {
    renderer.draw_depth(unsupported_handle, selected, parameters, library, color, depth);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Unknown original shader selector cannot silently use the depth shader");
  const auto shaped = bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(model_fixture::archive({.shape = true})), domain);
  const auto before_shape = budget->snapshot();
  const auto shape_handle = renderer.upload_model(shaped);
  require(renderer.geometry_status(shape_handle).vertex_bytes ==
                  2 * model_fixture::kPositions.size() * sizeof(float) &&
              budget->snapshot().live_resources == before_shape.live_resources + 3,
          "Separate shape-position stream is retained and accounted without static substitution");
  rejected = false;
  try {
    renderer.draw_depth(shape_handle, selected, parameters, library, color, depth);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Deformation-dependent shape draw cannot use static depth renderer");
  renderer.release_model(shape_handle);
  require(budget->snapshot().live_resources == before_shape.live_resources,
          "Shape-position storage retires with its model");
  renderer.release_model(unsupported_handle);
  renderer.release_model(handle);
  renderer.release_model(handle);
  require(budget->snapshot().live_resources == 0 && budget->snapshot().pending_resources == 0,
          "Completed geometry release returns all device charges and stale release is harmless");
  auto limited = bw::memory::DeviceBudget::create(
      domain, {.max_logical_bytes = vertices.size() * sizeof(float)});
  bw::platform::macos::WorldRenderer limited_renderer(device, queue, domain, limited);
  rejected = false;
  try {
    (void)limited_renderer.upload_model(model);
  } catch (const std::bad_alloc&) {
    rejected = true;
  }
  require(rejected && limited->snapshot().live_resources == 0 &&
              limited->snapshot().pending_resources == 0,
          "Index admission failure rolls back the earlier native vertex buffer");
}
// Authored pixels exercise the recovered surface equations without game data.
void world_surface_contract(id<MTLDevice> device) {
  using bw::render::WorldColorDraw;
  using bw::render::WorldCull;
  using bw::render::WorldDepthCompare;
  using bw::render::WorldTextureAddress;
  using bw::render::WorldTextureFilter;
  using bw::render::WorldTextureHandle;
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  id<MTLCommandQueue> queue = [device newCommandQueue];
  bw::platform::macos::WorldRenderer renderer(device, queue, domain, budget);
  auto model = bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(model_fixture::archive({.normal_mapped = true})),
      domain);
  const auto mesh = renderer.upload_model(model);
  bw::formats::WorldTextureImage image{
      1, 1, bw::formats::WorldTextureFormat::rgba8,
      bw::memory::Vector<std::uint8_t>(bw::memory::Allocator<std::uint8_t>(domain))};
  image.pixels = {64, 128, 192, 255};
  const auto base = renderer.upload_world_texture(image);
  image.pixels = {255, 255, 255, 255};
  const auto normal = renderer.upload_world_texture(image);
  const std::array<WorldTextureHandle, 3> bindings{base, normal, WorldTextureHandle{}};
  renderer.bind_model_textures(mesh, bindings);
  require(!renderer.geometry_status(mesh).texture_bindings_ready,
          "Partial runtime table does not claim whole-model texture readiness");
  WorldColorDraw draw{.model = mesh, .draw = 0, .effective_shader_flags = 0x00e20882};
  draw.parameters.position_scale = {1, 1, 1, 1};
  draw.parameters.material_color = {1, 1, 1, 1};
  draw.parameters.indirect_color = {1, 1, 1, 1};
  for (std::size_t i = 0; i < 4; ++i) {
    draw.parameters.clip_rows.at(i).at(i) = 1;
  }
  for (std::size_t i = 0; i < 3; ++i) {
    draw.parameters.world_rows.at(i).at(i) = 1;
  }
  draw.state = {WorldTextureFilter::linear,
                WorldTextureFilter::linear,
                WorldTextureAddress::repeat,
                WorldTextureAddress::repeat,
                WorldCull::none,
                WorldDepthCompare::less_equal,
                true};
  const auto surface_parameters = draw.parameters;
  const auto surface_state = draw.state;
  renderer.set_world_draws(std::span(&draw, 1));
  constexpr std::uint32_t extent = 32;
  auto* descriptor =
      [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                         width:extent
                                                        height:extent
                                                     mipmapped:NO];
  descriptor.storageMode = MTLStorageModeShared;
  descriptor.usage = MTLTextureUsageRenderTarget;
  id<MTLTexture> color = [device newTextureWithDescriptor:descriptor];
  auto depth = bw::platform::macos::TextureAllocation::create(device, budget, extent, extent,
                                                              MTLPixelFormatDepth32Float);
  NSError* error = nil;
  id<MTLLibrary> library =
      [device newLibraryWithURL:[NSURL fileURLWithPath:@BW_WORLD_SHADER_LIBRARY] error:&error];
  require(color != nil && library != nil, "Surface shader and test target available");
  float presentation_opacity = 1;
  double clear_depth = 1;
  bool present_scene = false;
  const auto read_pixel = [&] {
    auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = color;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    if (!present_scene) {
      pass.depthAttachment.texture = depth.get();
      pass.depthAttachment.loadAction = MTLLoadActionClear;
      pass.depthAttachment.clearDepth = clear_depth;
    }
    id<MTLCommandBuffer> command = [queue commandBuffer];
    if (present_scene) renderer.prepare_presentation(command, library, extent, extent);
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (present_scene) {
      renderer.encode_presentation(encoder, library, presentation_opacity);
    } else {
      renderer.encode_color(encoder, library, presentation_opacity);
    }
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    require(command.status == MTLCommandBufferStatusCompleted,
            "Surface device execution completed");
    std::array<std::uint8_t, 4> result{};
    [color getBytes:result.data()
        bytesPerRow:4
         fromRegion:MTLRegionMake2D(extent / 2, extent / 2, 1, 1)
        mipmapLevel:0];
    return result;
  };
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Original vertex illumination and material texture reach the native color target");
  // Reverse-depth ordering must survive submission order: the nearer red
  // surface precedes a farther blue one, and zero is the uncovered depth.
  auto nearer = draw;
  nearer.state.depth_compare = WorldDepthCompare::greater_equal;
  nearer.state.min_filter = WorldTextureFilter::anisotropic;
  nearer.state.maximum_anisotropy = 4;
  nearer.parameters.clip_rows[2] = {0, 0, 0, .75F};
  nearer.parameters.material_color = {1, 0, 0, 1};
  auto farther = nearer;
  farther.parameters.clip_rows[2] = {0, 0, 0, .25F};
  farther.parameters.material_color = {0, 0, 1, 1};
  const std::array reverse_draws{nearer, farther};
  renderer.set_world_draws(reverse_draws);
  clear_depth = 0;
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 64, 255},
          "Reverse depth keeps the nearer surface despite later farther submission");
  present_scene = true;
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 64, 255},
          "Production HDR presentation clears zero and preserves reversed-depth ordering");
  present_scene = false;
  nearer.state.maximum_anisotropy = 0;
  bool rejected_anisotropy = false;
  try {
    renderer.set_world_draws(std::span(&nearer, 1));
  } catch (const std::invalid_argument&) {
    rejected_anisotropy = true;
  }
  require(rejected_anisotropy && read_pixel() == std::array<std::uint8_t, 4>{0, 0, 64, 255},
          "Invalid anisotropy preserves the previous valid native submission");
  clear_depth = 1;
  // This authored normal sample is outside the tangent disk: original rsq uses abs.
  draw.parameters.light_direction = {0, 0, -1, 0};
  draw.parameters.light_diffuse = {0.25F, 0.25F, 0.25F, 0};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{240, 160, 80, 255},
          "Original reciprocal-square-root absolute semantics preserve finite diffuse lighting");
  draw.parameters.fog_color = {1, 0, 0, 1};
  draw.parameters.fog_height = {0, 1, 0, 1};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "Original height fog reaches its color endpoint without changing material alpha");
  presentation_opacity = 0.5F;
  const auto faded = read_pixel();
  require(faded[0] == 0 && faded[1] == 0 && faded[2] >= 127 && faded[2] <= 128 && faded[3] == 255,
          "Presentation fade scales final scene color independently from material alpha");
  // Additional original program families use their actual distinct declarations.
  presentation_opacity = 1;
  const auto original_draw = draw;
  const auto upload_fixture = [&](const model_fixture::Options& options) {
    const auto definition = bw::formats::ModelDefinition::parse(
        bw::resources::ResourceArchive::parse(model_fixture::archive(options)), domain);
    return renderer.upload_model(definition);
  };
  const auto geometric = upload_fixture({.shader = {0x00420802, 1}, .base_only = true});
  renderer.bind_model_textures(geometric, std::span(&base, 1));
  draw = {.model = geometric,
          .effective_shader_flags = 0x00e20802,
          .parameters = surface_parameters,
          .state = surface_state};
  renderer.set_world_draws(std::span(&draw, 1));
  require(
      read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
      "Geometric-normal surface preserves original stride28 and base texture without a normal map");
  draw.effective_shader_flags = 0x00e20a02;
  for (std::size_t channel = 0; channel < 3; ++channel) {
    draw.parameters.irradiance_coefficients.at(channel).at(3) = .25F;
  }
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{240, 160, 80, 255},
          "Ordinary irradiance adds to baked vertex light before base-texture modulation");
  image.pixels = {128, 255, 255, 255};
  const auto secondary = renderer.upload_world_texture(image);
  const auto layered = upload_fixture(
      {.shader = {0x00600882, 0x102}, .normal_mapped = true, .secondary_base = true});
  const std::array<WorldTextureHandle, 3> layered_bindings{base, normal, secondary};
  renderer.bind_model_textures(layered, layered_bindings);
  draw = {.model = layered,
          .effective_shader_flags = 0x00e00882,
          .parameters = surface_parameters,
          .state = surface_state};
  draw.parameters.indirect_secondary = {1, 1, 1, 1};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 32, 255},
          "Original secondary base alpha weights multiplicative detail while retaining base alpha");
  const auto shadow_layered = upload_fixture(
      {.shader = {0x00020882, 0x102}, .normal_mapped = true, .secondary_base = true});
  renderer.bind_model_textures(shadow_layered, layered_bindings);
  draw.model = shadow_layered;
  draw.effective_shader_flags = 0x00820882;
  bool rejected_full_lighting = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected_full_lighting = true;
  }
  require(rejected_full_lighting,
          "Shadow-dependent program requires an explicit omitted-light policy");
  draw.lighting = bw::render::WorldLightingPolicy::without_direct_lighting;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 32, 255},
          "Shadow-dependent program retains its layered material equation when authored lights are "
          "omitted");
  draw.parameters.light_diffuse = {1, 1, 1, 1};
  bool rejected_light_energy = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected_light_energy = true;
  }
  require(rejected_light_energy, "Omitted-light policy rejects supplied directional energy");
  const auto local_lit = upload_fixture({.shader = {0x04200882, 1}, .normal_mapped = true});
  renderer.bind_model_textures(local_lit, bindings);
  draw = {.model = local_lit,
          .effective_shader_flags = 0x04a00882,
          .parameters = surface_parameters,
          .state = surface_state,
          .lighting = bw::render::WorldLightingPolicy::without_direct_lighting};
  renderer.set_world_draws(std::span(&draw, 1));
  require(
      read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
      "Local-light program preserves material and vertex illumination with its light list omitted");
  image.format = bw::formats::WorldTextureFormat::rgba16_float;
  image.pixels = {0, 60, 0, 56, 0, 52, 0, 60};
  const auto sky_texture = renderer.upload_world_texture(image);
  const auto sky = upload_fixture({.shader = {0x06600000, 1}, .sky = true});
  renderer.bind_model_textures(sky, std::span(&sky_texture, 1));
  draw = {.model = sky,
          .effective_shader_flags = 0x06e00000,
          .parameters = surface_parameters,
          .state = surface_state};
  draw.parameters.ambient = {1, 1, 1, 1};
  renderer.set_world_draws(std::span(&draw, 1));
  const auto sky_pixel = read_pixel();
  require(sky_pixel[0] == 64 && sky_pixel[1] >= 127 && sky_pixel[1] <= 128 && sky_pixel[2] == 255 &&
              sky_pixel[3] == 255,
          "Sky program consumes packed half-float pixels and authored constant illumination "
          "without vertex light");
  // Runtime irradiance selection must carry the normal even with directional
  // lighting disabled. Its constant and directional coefficients are independent
  // from material ambience and emissive illumination.
  draw.effective_shader_flags = 0x06e00200;
  draw.parameters.ambient = {};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    draw.parameters.irradiance_coefficients.at(channel).at(3) = 1;
  }
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == sky_pixel,
          "Runtime constant irradiance lights sky without invented material ambience");
  draw.parameters.irradiance_coefficients = {};
  draw.parameters.irradiance_coefficients.at(3).at(2) = .25F;
  draw.parameters.irradiance_coefficients.at(4).at(2) = .5F;
  draw.parameters.irradiance_coefficients.at(5).at(2) = .75F;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{48, 64, 64, 255},
          "Quadratic irradiance evaluates the retained geometric normal");
  draw.parameters.irradiance_coefficients = {};
  draw.parameters.irradiance_coefficients.at(6) = {.25F, .5F, .75F, 1};
  draw.parameters.world_rows = {{{0, 0, 1, 0}, {0, 1, 0, 0}, {1, 0, 0, 0}}};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{48, 64, 64, 255},
          "Irradiance evaluates transformed normals and the x-squared minus y-squared term");
  image.format = bw::formats::WorldTextureFormat::rgba8;
  image.shape = bw::formats::WorldTextureShape::cube;
  image.pixels = {255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255,
                  255, 0, 0, 255, 255, 0, 0, 255, 255, 0, 0, 255};
  const auto cube = renderer.upload_world_texture(image);
  const auto reflective =
      upload_fixture({.shader = {0x00422882, 1}, .normal_mapped = true, .environment = true});
  const std::array<WorldTextureHandle, 4> reflective_bindings{base, normal, {}, cube};
  renderer.bind_model_textures(reflective, reflective_bindings);
  draw = {.model = reflective,
          .effective_shader_flags = 0x00e22882,
          .parameters = surface_parameters,
          .state = surface_state};
  draw.parameters.environment_power = {1, 1, 1, 1};
  draw.parameters.reflection = {1, 0, 0, 1};
  draw.parameters.fresnel = {1, 0, 1, 0};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "Original cubemap reflection combines environment power, reflection alpha and Fresnel "
          "weight");
  // Missing authored LENV restores the original black cube even after an
  // authored red cube draw; a wrong-shape explicit binding remains an error.
  const auto fallback_model = upload_fixture({.shader = {0x00422882, 1}, .normal_mapped = true});
  renderer.bind_model_textures(fallback_model, bindings);
  draw.model = fallback_model;
  const auto before_fallback = budget->snapshot();
  renderer.set_world_draws(std::span(&draw, 1));
  constexpr std::size_t kFallbackCubeBytes = std::size_t{6} * 4;
  require(budget->snapshot().live_logical_bytes ==
              before_fallback.live_logical_bytes + kFallbackCubeBytes,
          "Original fallback cube has one accounted six-face allocation");
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 255},
          "Absent LENV uses original opaque black cube, not prior authored environment");
  renderer.set_world_draws(std::span(&draw, 1));
  require(budget->snapshot().live_logical_bytes ==
              before_fallback.live_logical_bytes + kFallbackCubeBytes,
          "Fallback cube is retained across submissions");
  const std::array<WorldTextureHandle, 4> wrong_environment{base, normal, {}, base};
  renderer.bind_model_textures(reflective, wrong_environment);
  draw.model = reflective;
  bool wrong_shape_rejected = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    wrong_shape_rejected = true;
  }
  require(wrong_shape_rejected, "Explicit LENV with wrong shape cannot use default fallback");
  renderer.bind_model_textures(reflective, reflective_bindings);
  image.shape = bw::formats::WorldTextureShape::two_dimensional;
  image.pixels = {64, 128, 192, 128};
  const auto translucent = renderer.upload_world_texture(image);
  renderer.bind_model_textures(geometric, std::span(&translucent, 1));
  draw = {.model = geometric,
          .effective_shader_flags = 0x00e20802,
          .parameters = surface_parameters,
          .state = surface_state};
  draw.state.blend = bw::render::WorldBlend::source_alpha;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{96, 64, 32, 64},
          "Source-alpha blending uses the original shared color and alpha factors");
  const auto cutout = upload_fixture({.shader = {0x00020883, 1}, .normal_mapped = true});
  const std::array<WorldTextureHandle, 2> cutout_bindings{translucent, normal};
  renderer.bind_model_textures(cutout, cutout_bindings);
  draw = {.model = cutout,
          .effective_shader_flags = 0x00e20883,
          .parameters = surface_parameters,
          .state = surface_state};
  draw.parameters.material_color[3] = 0.5F;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Original depth-mask coverage rejects material times sampled alpha below one half");
  for (auto handle :
       {geometric, layered, shadow_layered, local_lit, sky, reflective, fallback_model, cutout}) {
    renderer.release_model(handle);
  }
  for (auto handle : {secondary, sky_texture, cube, translucent}) {
    renderer.release_world_texture(handle);
  }
  draw = original_draw;
  renderer.set_world_draws(std::span(&draw, 1));
  renderer.release_world_texture(normal);
  require(!renderer.has_color_draws(), "Released texture invalidates retained submissions");
  bool rejected = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Selected material with missing normal texture cannot submit");
  renderer.release_world_texture(base);
  renderer.release_model(mesh);
  depth.reset();
  image.format = bw::formats::WorldTextureFormat::rgba16_float;
  image.pixels = {0, 0, 0, 0, 0, 0, 0, 0};
  const auto hdr = renderer.upload_world_texture(image);
  require(budget->snapshot().live_logical_bytes == 8,
          "Half-float texture charges eight original bytes per texel");
  renderer.release_world_texture(hdr);
  require(budget->snapshot().live_resources == 0 && budget->snapshot().pending_resources == 0,
          "Completed surface owners return all device charges");
}

// Exercise nonuniform native reduction and display transfer independently of
// model decoding. Constants are the public shader ABI, with numerical expected
// values derived from the original equations rather than renderer helpers.
void world_output_contract(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  auto source =
      bw::platform::macos::TextureAllocation::create(device, budget, 4, 4, MTLPixelFormatR32Float);
  std::array<float, 16> pixels{};
  pixels.fill(9);
  pixels.at(0) = .1F;
  pixels.at(3) = .2F;
  pixels.at(12) = .3F;
  pixels.at(15) = .4F;
  [source.get() replaceRegion:MTLRegionMake2D(0, 0, 4, 4)
                  mipmapLevel:0
                    withBytes:pixels.data()
                  bytesPerRow:4 * sizeof(float)];
  auto reduced = bw::platform::macos::TextureAllocation::create(
      device, budget, 1, 1, MTLPixelFormatR32Float, MTLTextureType2D,
      MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
  auto output =
      bw::platform::macos::TextureAllocation::create(device, budget, 1, 1, MTLPixelFormatBGRA8Unorm,
                                                     MTLTextureType2D, MTLTextureUsageRenderTarget);
  auto log_image = bw::platform::macos::TextureAllocation::create(
      device, budget, 2, 2, MTLPixelFormatRGBA16Float, MTLTextureType2D,
      MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
  auto queue = [device newCommandQueue];
  NSError* error = nil;
  auto library = [device newLibraryWithURL:[NSURL fileURLWithPath:@BW_WORLD_SHADER_LIBRARY]
                                     error:&error];
  require(library != nil, "Native output shader available");
  struct Parameters {
    std::array<float, 2> inverse_extent{1, 1};
    float exposure_key{.18F}, inverse_gamma{1 / 2.2F}, bloom_key{}, threshold{}, knee{},
        log_luminance{};
    std::uint32_t adapted{}, bloom{}, enabled{1};
    float opacity{1};
  } parameters;
  const auto execute = [&](NSString* function, id<MTLTexture> target) {
    auto* pipeline_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    pipeline_descriptor.vertexFunction = [library newFunctionWithName:@"world_output_vertex"];
    pipeline_descriptor.fragmentFunction = [library newFunctionWithName:function];
    pipeline_descriptor.colorAttachments[0].pixelFormat = target.pixelFormat;
    auto pipeline = [device newRenderPipelineStateWithDescriptor:pipeline_descriptor error:nil];
    require(pipeline != nil, "Native output pipeline available");
    auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = target;
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    auto command = [queue commandBuffer];
    auto encoder = [command renderCommandEncoderWithDescriptor:pass];
    [encoder setRenderPipelineState:pipeline];
    [encoder setFragmentTexture:source.get() atIndex:0];
    if (target.pixelFormat == MTLPixelFormatBGRA8Unorm) {
      [encoder setFragmentTexture:reduced.get() atIndex:1];
    }
    [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:0];
    [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    require(command.status == MTLCommandBufferStatusCompleted, "Native output pass completed");
  };
  execute(@"world_reduce_luminance", reduced.get());
  float result{};
  [reduced.get() getBytes:&result
              bytesPerRow:sizeof(float)
               fromRegion:MTLRegionMake2D(0, 0, 1, 1)
              mipmapLevel:0];
  require(std::abs(result - .25F) < .00001F,
          "Original destination-normalized reduction clamps to four corners, not interior mean");
  parameters.inverse_extent = {.5F, .5F};
  execute(@"world_log_luminance", log_image.get());
  std::array<std::uint16_t, 4> sample{};
  [log_image.get() getBytes:sample.data()
                bytesPerRow:sizeof(sample)
                 fromRegion:MTLRegionMake2D(0, 0, 1, 1)
                mipmapLevel:0];
  require(sample[0] == 0x2e66 && sample[1] == 0 && sample[2] == 0,
          "D3D9 fullscreen interpolation maps to the first texel center after Metal conversion");
  parameters.inverse_extent = {1, 1};
  pixels.fill(.04F);
  [source.get() replaceRegion:MTLRegionMake2D(0, 0, 4, 4)
                  mipmapLevel:0
                    withBytes:pixels.data()
                  bytesPerRow:4 * sizeof(float)];
  parameters.adapted = 1;
  parameters.log_luminance = std::log(.01F);
  execute(@"world_display_output", output.get());
  std::array<std::uint8_t, 4> display{};
  [output.get() getBytes:display.data()
             bytesPerRow:4
              fromRegion:MTLRegionMake2D(0, 0, 1, 1)
             mipmapLevel:0];
  const auto expected = std::lround(255 * std::pow(.04F * .18F / (.01F + .001F), 1 / 2.2F));
  require(std::abs(static_cast<long>(display.at(2)) - expected) <= 1 && display.at(0) == 0 &&
              display.at(1) == 0 && display.at(3) == 255,
          "Original exposure and gamma map HDR to display without a second transfer");
  source.reset();
  reduced.reset();
  output.reset();
  log_image.reset();
  require(budget->snapshot().live_resources == 0,
          "Completed output test targets return all device charges");
}
}  // namespace
void BWMacosTextureAllocationTests() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    require(device != nil, "No Metal device for native memory acceptance");
    world_geometry_contract(device);
    world_surface_contract(device);
    world_output_contract(device);
    auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
    constexpr std::uint32_t width = 32, height = 16;
    constexpr std::size_t bytes = static_cast<std::size_t>(width) * height * 4;
    auto budget = bw::memory::DeviceBudget::create(domain, {.max_logical_bytes = 2 * bytes});
    std::array<std::uint8_t, bytes> pixels{};
    pixels.fill(0x5a);
    auto source = bw::platform::macos::TextureAllocation::create(device, budget, width, height,
                                                                 MTLPixelFormatRGBA8Unorm);
    source.upload(pixels);
    require(budget->snapshot().live_logical_bytes == bytes &&
                budget->snapshot().live_native_bytes == source.get().allocatedSize &&
                budget->snapshot().pending_resources == 0,
            "Texture reports allocated native bytes separately from logical size");
    auto destination = bw::platform::macos::TextureAllocation::create(device, budget, width, height,
                                                                      MTLPixelFormatRGBA8Unorm);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> command = [queue commandBuffer];
    id<MTLBlitCommandEncoder> encoder = [command blitCommandEncoder];
    require(queue != nil && command != nil && encoder != nil, "Native texture command creation");
    [encoder copyFromTexture:source.get()
                 sourceSlice:0
                 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:destination.get()
            destinationSlice:0
            destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)];
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    require(command.status == MTLCommandBufferStatusCompleted, "Native texture consumer completed");
    std::array<std::uint8_t, bytes> result{};
    [destination.get() getBytes:result.data()
                    bytesPerRow:static_cast<NSUInteger>(width) * 4
                     fromRegion:MTLRegionMake2D(0, 0, width, height)
                    mipmapLevel:0];
    require(result == pixels, "GPU consumer preserves uploaded texture data");
    bool rejected = false;
    try {
      source = bw::platform::macos::TextureAllocation::create(device, budget, width, height,
                                                              MTLPixelFormatRGBA8Unorm);
    } catch (const std::bad_alloc&) {
      rejected = true;
    }
    require(rejected && source.get() != nil && budget->snapshot().live_resources == 2 &&
                budget->snapshot().pending_resources == 0,
            "Rejected replacement retains prior texture and accounting");
    auto limited = bw::memory::DeviceBudget::create(domain, {.max_logical_bytes = bytes - 1});
    rejected = false;
    try {
      (void)bw::platform::macos::TextureAllocation::create(device, limited, width, height,
                                                           MTLPixelFormatRGBA8Unorm);
    } catch (const std::bad_alloc&) {
      rejected = true;
    }
    require(rejected && limited->snapshot().pending_resources == 0 &&
                limited->snapshot().live_resources == 0,
            "Native texture admission rejection leaves no charge");
    // Command references are retired before releasing texture owners/charges.
    encoder = nil;
    command = nil;
    source.reset();
    destination.reset();
    // Six distinct faces make upload ordering visible through native slice readback.
    constexpr std::array<std::uint8_t, 24> face_pixels{10, 0, 0, 255, 20, 0, 0, 255, 30, 0, 0, 255,
                                                       40, 0, 0, 255, 50, 0, 0, 255, 60, 0, 0, 255};
    auto cube = bw::platform::macos::TextureAllocation::create(
        device, budget, 1, 1, MTLPixelFormatRGBA8Unorm, MTLTextureTypeCube);
    cube.upload(face_pixels);
    require(budget->snapshot().live_logical_bytes == face_pixels.size(),
            "Cube accounting includes all six face payloads");
    for (NSUInteger face = 0; face < 6; ++face) {
      std::array<std::uint8_t, 4> sample{};
      [cube.get() getBytes:sample.data()
               bytesPerRow:4
             bytesPerImage:4
                fromRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0
                     slice:face];
      require(std::equal(sample.begin(), sample.end(),
                         std::span(face_pixels).subspan(face * 4, 4).begin()),
              "Native cube slices preserve the original six-face order");
    }
    cube.reset();
    require(budget->snapshot().live_resources == 0 && budget->snapshot().live_native_bytes == 0,
            "Completed native texture owners return all charges");
  }
}
