// Unit-normal tests apply both-phase Core I.7 (#ri-post) and performance GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): verify the directional
// postcondition through public native draws, with completion before readback.
// Component replacement verification applies both-phase I.1/C.85 and GPU.1:
// exercise transaction rollback and retained native state with device pixels.
// Shadow footprint tests exercise the inferred UE receiver-plane adaptation:
// tilted self-receivers remain lit while distinct thin casters retain contact
// edges; singular projection derivatives stay bounded. Both phases consulted
// Core I.1 (explicit coordinate contracts) and performance GPU.10
// (guidelines/gpu/GPU.10-profile-with-gpu-timelines-and-counters.md): completed
// native pixel checks establish correctness, not throughput measurements.
// Retained camera checks exercise I.1 transactional publication and GPU.1
// resident resources through native pixel results, not internal state.
// Both-phase I.1/GPU.1 also verify optional shader resource consumption with
// actual device pixels and no dummy texture allocation.
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
// Output lifecycle/receiver tests additionally apply both-phase E.4
// (#re-design-invariants), GPU.1 and GPU.9: validate retained state through
// completed native output, including tone toggles and independent instances.
// Receiver cube/visibility additions retrieve E.4 and GPU.1 in both phases:
// late batch failure preserves completed pixels, authored bindings retain
// precedence, and updates reuse resident geometry and textures.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <utility>

#include "bw/formats/model.hpp"
#include "bw/formats/world_texture.hpp"
#include "bw/render/shadow.hpp"
#include "model_fixture.hpp"
#include "platform/macos/buffer_allocation.hpp"
#include "platform/macos/cpu_backing.hpp"
#include "platform/macos/texture_allocation.hpp"
#include "platform/macos/world_postprocess.hpp"
#include "platform/macos/world_renderer.hpp"

void BWTestFinalEdge(id<MTLDevice> device);
void BWTestTextureMips(id<MTLDevice> device);
void BWTestSceneReplay(id<MTLDevice> device);
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
// Invalid shared view state must preserve the already-published camera/image.
template <typename ReadPixel>
void reject_view_update(bw::platform::macos::WorldRenderer& renderer,
                        const bw::render::WorldViewUpdate& update, const ReadPixel& read_pixel) {
  const auto before = read_pixel();
  bool rejected = false;
  try {
    renderer.update_world_view(update);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel() == before,
          "Invalid view/light packet preserves the prior complete presentation");
}
// Authored pixels exercise the recovered surface equations without game data.
// Check receiver identity through native pixels while sharing immutable geometry.
// The callable borrows the existing completed-readback fixture for this call only.
template <class ReadPixel>
void receiver_instance_contract(bw::platform::macos::WorldRenderer& renderer,
                                const bw::render::WorldColorDraw& draw,
                                const bw::render::WorldLightingUpdate& original_light,
                                const ReadPixel& read_pixel, std::uint32_t extent) {
  // Two scene instances share geometry and draw ordinal but retain separate
  // receiver packets. A late invalid instance must reject the entire batch.
  auto left_instance = draw;
  auto right_instance = draw;
  left_instance.instance = 1;
  right_instance.instance = 2;
  left_instance.parameters.clip_rows[0][0] *= .4F;
  right_instance.parameters.clip_rows[0][0] *= .4F;
  left_instance.parameters.clip_rows[0][3] -= .5F;
  right_instance.parameters.clip_rows[0][3] += .5F;
  const std::array instances{left_instance, right_instance};
  renderer.set_world_draws(instances);
  const auto left_lit = read_pixel(extent / 4, extent / 2);
  const auto right_lit = read_pixel(3 * extent / 4, extent / 2);
  require(left_lit[0] > 0 && right_lit[0] > 0, "Both receiver instances are visible");
  auto left_dark = original_light;
  left_dark.irradiance_coefficients = {};
  left_dark.instance = 1;
  renderer.update_world_lighting(std::span(&left_dark, 1));
  const auto left_unlit = read_pixel(extent / 4, extent / 2);
  require(left_unlit[0] < left_lit[0] && read_pixel(3 * extent / 4, extent / 2) == right_lit,
          "Receiver update affects only its explicit scene instance");
  auto left_restore = original_light;
  left_restore.instance = 1;
  auto absent_instance = original_light;
  absent_instance.instance = 3;
  const std::array instance_batch{left_restore, absent_instance};
  bool rejected_light = false;
  try {
    renderer.update_world_lighting(instance_batch);
  } catch (const std::invalid_argument&) {
    rejected_light = true;
  }
  require(rejected_light && read_pixel(extent / 4, extent / 2) == left_unlit &&
              read_pixel(3 * extent / 4, extent / 2) == right_lit,
          "Absent instance rejects the whole batch without broadcasting");
  renderer.update_world_lighting(std::span(&original_light, 1));
  require(read_pixel(extent / 4, extent / 2) == left_lit &&
              read_pixel(3 * extent / 4, extent / 2) == right_lit,
          "Explicit broadcast still updates every matching instance");
  renderer.set_world_draws(std::span(&draw, 1));
}
// Selected receiver maps supply the original default binding; authored LENV
// remains later in source command order. Exercise both retained publication
// paths and independent instances with completed pixels from the same fixture.
struct ReflectionTextures {
  bw::render::WorldTextureHandle cube, base, normal;
};
template <class ReadPixel>
void receiver_reflection_contract(bw::platform::macos::WorldRenderer& renderer,
                                  bw::render::WorldColorDraw draw,
                                  const ReflectionTextures& textures,
                                  bw::render::WorldModelHandle authored_model,
                                  const ReadPixel& read_pixel, std::uint32_t extent) {
  using bw::render::WorldLightingUpdate;
  using bw::render::WorldTextureHandle;
  constexpr std::array<std::uint8_t, 4> kBlack{0, 0, 0, 255}, kRed{0, 0, 255, 255};
  WorldLightingUpdate selected{.model = draw.model,
                               .draw = draw.draw,
                               .irradiance_coefficients = draw.parameters.irradiance_coefficients,
                               .reflection_texture = textures.cube};
  renderer.update_world_lighting(std::span(&selected, 1));
  require(read_pixel() == kRed, "Receiver cube replaces only the default environment binding");
  auto unchanged = selected;
  unchanged.reflection_texture.reset();
  renderer.update_world_lighting(std::span(&unchanged, 1));
  require(read_pixel() == kRed, "Omitted receiver cube preserves its current publication");
  auto black = selected;
  black.reflection_texture = WorldTextureHandle{};
  renderer.update_world_lighting(std::span(&black, 1));
  require(read_pixel() == kBlack, "Explicit zero receiver cube restores opaque black");
  draw.reflection_texture = textures.cube;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == kRed, "Initial draw submission publishes the selected receiver cube");
  for (const auto invalid_handle : {textures.base, WorldTextureHandle{999999}}) {
    auto invalid = selected;
    invalid.reflection_texture = invalid_handle;
    const std::array updates{black, invalid};
    bool rejected = false;
    try {
      renderer.update_world_lighting(updates);
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && read_pixel() == kRed,
            "Late wrong-shape or absent receiver cube rejects the whole lighting batch");
    auto bad_draw = draw;
    bad_draw.reflection_texture = invalid_handle;
    rejected = false;
    try {
      renderer.set_world_draws(std::span(&bad_draw, 1));
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && read_pixel() == kRed,
            "Invalid initial receiver cube preserves the previous complete submission");
  }
  auto left = draw;
  auto right = draw;
  left.instance = 1;
  right.instance = 2;
  left.parameters.clip_rows[0][0] *= .4F;
  right.parameters.clip_rows[0][0] *= .4F;
  left.parameters.clip_rows[0][3] -= .5F;
  right.parameters.clip_rows[0][3] += .5F;
  right.reflection_texture = WorldTextureHandle{};
  const std::array draws{left, right};
  renderer.set_world_draws(draws);
  require(read_pixel(extent / 4, extent / 2) == kRed &&
              read_pixel(3 * extent / 4, extent / 2) == kBlack,
          "Shared model instances retain independently selected receiver cubes");
  black.instance = 1;
  selected.instance = 2;
  const std::array swap{black, selected};
  renderer.update_world_lighting(swap);
  require(read_pixel(extent / 4, extent / 2) == kBlack &&
              read_pixel(3 * extent / 4, extent / 2) == kRed,
          "Explicit receiver identities change only their own fallback cube");
  auto restore_left = selected;
  restore_left.instance = 1;
  auto absent = black;
  absent.instance = 3;
  const std::array invalid_instances{restore_left, absent};
  bool rejected = false;
  try {
    renderer.update_world_lighting(invalid_instances);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel(extent / 4, extent / 2) == kBlack &&
              read_pixel(3 * extent / 4, extent / 2) == kRed,
          "Absent receiver identity cannot partially publish a cube batch");
  draw.model = authored_model;
  draw.reflection_texture = WorldTextureHandle{};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == kRed,
          "Authored material LENV overrides an explicitly black receiver default");
  black.model = authored_model;
  black.instance.reset();
  renderer.update_world_lighting(std::span(&black, 1));
  require(read_pixel() == kRed,
          "Retained receiver publication cannot erase an authored environment map");
  const std::array<WorldTextureHandle, 4> wrong_environment{
      textures.base, textures.normal, {}, textures.base};
  renderer.bind_model_textures(authored_model, wrong_environment);
  bool wrong_shape_rejected = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    wrong_shape_rejected = true;
  }
  require(wrong_shape_rejected, "Explicit LENV with wrong shape cannot use default fallback");
  const std::array<WorldTextureHandle, 4> authored{
      textures.base, textures.normal, {}, textures.cube};
  renderer.bind_model_textures(authored_model, authored);
}
// Source caster admission is independent of visible color and uses the same
// explicit scene identity as receiver publication. Late failures are atomic.
template <class ReadPixel>
void receiver_visibility_contract(bw::platform::macos::WorldRenderer& renderer,
                                  bw::render::WorldColorDraw receiver,
                                  bw::render::WorldColorDraw caster, const ReadPixel& read_pixel,
                                  std::uint32_t extent) {
  using bw::render::WorldVisibilityUpdate;
  receiver.instance = 1;
  caster.instance = 2;
  receiver.parameters.clip_rows[0][0] *= .4F;
  caster.parameters.clip_rows[0][0] *= .4F;
  receiver.parameters.clip_rows[0][3] = -.5F;
  caster.parameters.clip_rows[0][3] = .5F;
  const std::array draws{receiver, caster};
  renderer.set_world_draws(draws);
  const auto shadowed = read_pixel(extent / 4, extent / 2);
  const auto caster_color = read_pixel(3 * extent / 4, extent / 2);
  require(shadowed == std::array<std::uint8_t, 4>{192, 128, 64, 255} && caster_color[0] > 0,
          "Visible caster shadows the independent receiver instance");
  WorldVisibilityUpdate update{.model = caster.model,
                               .draw = caster.draw,
                               .visible = true,
                               .casts_shadow = false,
                               .instance = 2};
  renderer.update_world_visibility(std::span(&update, 1));
  const auto lit = read_pixel(extent / 4, extent / 2);
  require(lit == std::array<std::uint8_t, 4>{240, 160, 80, 255} &&
              read_pixel(3 * extent / 4, extent / 2) == caster_color,
          "Disabling caster admission preserves its visible color and lights the receiver");
  update.visible = false;
  update.casts_shadow.reset();
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel(extent / 4, extent / 2) == lit &&
              read_pixel(3 * extent / 4, extent / 2) == std::array<std::uint8_t, 4>{},
          "Instance visibility hides only the selected caster color");
  update.visible = true;
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel(extent / 4, extent / 2) == lit &&
              read_pixel(3 * extent / 4, extent / 2) == caster_color,
          "Omitted casting update preserves disabled shadow membership when revealed");
  update.casts_shadow = true;
  update.visible = false;
  auto invalid = update;
  invalid.instance = 3;
  const std::array updates{update, invalid};
  bool rejected = false;
  try {
    renderer.update_world_visibility(updates);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel(extent / 4, extent / 2) == lit &&
              read_pixel(3 * extent / 4, extent / 2) == caster_color,
          "Late absent visibility identity preserves color and caster membership");
  update.visible = true;
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel(extent / 4, extent / 2) == shadowed &&
              read_pixel(3 * extent / 4, extent / 2) == caster_color,
          "Reenabling caster membership restores only its shadow contribution");
}
// Color phase, not a depth-mask selector, owns the final alpha test.
template <class ReadPixel>
void color_coverage_contract(bw::platform::macos::WorldRenderer& renderer,
                             bw::render::WorldColorDraw draw, const ReadPixel& read_pixel) {
  draw.parameters.material_color[3] = 0.5F;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() != std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Opaque color phase does not inherit depth-mask coverage from the selector");
  draw.color_phase = bw::render::WorldColorPhase::masked;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Masked color coverage rejects the final material alpha");
  draw.parameters.material_color[3] = 1;
  draw.parameters.indirect_color[3] = .25F;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Masked color tests indirect alpha that the depth texture cutout does not consume");
}
// Source setup/cull/bias affect the shared shadow pass, independently of the
// color material. The receiver exposes depth-color changes through native
// visibility; no test-only access to the renderer's private texture is needed.
template <class ReadPixel>
void source_shadow_state_contract(bw::platform::macos::WorldRenderer& renderer,
                                  bw::render::WorldColorDraw receiver,
                                  bw::render::WorldColorDraw caster, const ReadPixel& read_pixel) {
  using bw::render::WorldViewUpdate;
  constexpr std::array<std::uint8_t, 4> kShadowed{192, 128, 64, 255};
  constexpr std::array<std::uint8_t, 4> kLit{240, 160, 80, 255};
  caster.owner_opacity = 0;
  const std::array draws{receiver, caster};
  renderer.set_world_draws(draws);
  WorldViewUpdate view{};
  view.scene_passes = WorldViewUpdate::ScenePasses{false, false, true, {}};
  view.shadow_pass = WorldViewUpdate::ShadowPass{};
  renderer.update_world_view(view);
  require(read_pixel() == kShadowed,
          "Source shadow setup retains zero-color-opacity caster membership");
  view.shadow_pass->depth_bias = -.625F;
  renderer.update_world_view(view);
  require(read_pixel() == kLit,
          "Source constant bias changes stored depth independently of receiver lighting");
  view.shadow_pass->setup_enabled = false;
  view.shadow_pass->depth_bias = 0;
  renderer.update_world_view(view);
  require(read_pixel() == kLit,
          "Disabled source setup retains the preceding shadow map instead of rebuilding it");
  view.shadow_pass->setup_enabled = true;
  renderer.update_world_view(view);
  require(read_pixel() == kShadowed, "Reenabled shadow setup refreshes the retained map");
  view.shadow_pass->cull = bw::render::WorldCull::clockwise;
  renderer.update_world_view(view);
  require(read_pixel() == kShadowed,
          "Source clockwise culling retains the counter-clockwise fixture");
  view.shadow_pass->cull = bw::render::WorldCull::counter_clockwise;
  renderer.update_world_view(view);
  require(read_pixel() == kLit,
          "Source counter-clockwise culling removes the counter-clockwise fixture");
  renderer.set_world_draws(draws);
}
// Receiver coordinates are derived values owned by the accepted view; rejected
// bases cannot partially replace either the view or its pixel matrix pair.
template <class ReadPixel>
void receiver_projection_publication_contract(bw::platform::macos::WorldRenderer& renderer,
                                              bw::render::WorldColorDraw receiver,
                                              bw::render::WorldColorDraw caster,
                                              const ReadPixel& read_pixel) {
  receiver.view_space = bw::render::WorldViewSpace::world;
  const std::array draws{receiver, caster};
  renderer.set_world_draws(draws);
  bw::render::WorldViewUpdate view{};
  view.world.clip_rows = receiver.parameters.clip_rows;
  view.world.world_rows = receiver.parameters.world_rows;
  view.world.shadow_clip_rows = receiver.parameters.shadow_clip_rows;
  renderer.update_world_view(view);
  const auto baseline = read_pixel();
  auto rejected_view = view;
  rejected_view.world.world_rows[0][0] = 2;
  rejected_view.world.clip_rows[0][3] = 4;
  bool rejected = false;
  try {
    renderer.update_world_view(rejected_view);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel() == baseline,
          "Unsupported receiver basis preserves the prior view and pixel projection");
  auto rejected_draw = receiver;
  rejected_draw.parameters.world_rows[1][0] = .5F;
  rejected = false;
  try {
    renderer.set_world_draws(std::span(&rejected_draw, 1));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel() == baseline,
          "Unsupported fixed receiver basis preserves the prior scene");
  // The shader does not consume the rejected basis when reception is disabled.
  rejected_draw.parameters.shadow_parameters[0] = 0;
  renderer.set_world_draws(std::span(&rejected_draw, 1));
  renderer.update_world_view(rejected_view);
  renderer.set_world_draws(draws);
}
// Exercise shared-owner preparation on the production HDR target, followed by
// source integer-center raster placement. These are geometry survival tests,
// separate from supplied-fragment shader arithmetic witnesses.
template <class ReadPixel>
void scene_preparation_contract(bw::platform::macos::WorldRenderer& renderer,
                                bw::render::WorldColorDraw draw, const ReadPixel& read_pixel) {
  using bw::render::WorldColorPhase;
  using bw::render::WorldViewUpdate;
  draw.state.depth_compare = bw::render::WorldDepthCompare::greater_equal;
  draw.parameters.clip_rows[2] = {0, 0, 0, .75F};
  draw.color_phase = WorldColorPhase::masked;
  draw.parameters.indirect_color[3] = .25F;
  WorldViewUpdate view{};
  view.scene_passes = WorldViewUpdate::ScenePasses{true, true, false, {}};
  renderer.set_world_draws(std::span(&draw, 1));
  renderer.update_world_view(view);
  require(read_pixel() == std::array<std::uint8_t, 4>{191, 191, 191, 255},
          "Masked preparation writes depth color on restored HDR even when final color rejects");
  view.scene_passes->masked_preparation = false;
  renderer.update_world_view(view);
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 255},
          "Disabling the source masked preparation gate removes its surviving depth color");
  view.scene_passes->masked_preparation = true;
  auto opaque = draw;
  opaque.color_phase = WorldColorPhase::opaque;
  opaque.parameters.indirect_color[3] = 1;
  opaque.parameters.material_color = {1, 0, 0, 1};
  const std::array ordered{draw, opaque};
  renderer.set_world_draws(ordered);
  renderer.update_world_view(view);
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 64, 255},
          "Both preparation families precede either color family regardless of submission order");
  opaque.parameters.material_color = {1, 1, 1, 1};
  renderer.set_world_draws(std::span(&opaque, 1));
  view.scene_passes = WorldViewUpdate::ScenePasses{false, false, false, {}};
  renderer.update_world_view(view);
  require(read_pixel(8, 15) == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Diagnostic half-integer samples cover the sloping triangle edge");
  view.scene_passes->integer_pixel_centers = true;
  renderer.update_world_view(view);
  require(read_pixel(8, 15) == std::array<std::uint8_t, 4>{0, 0, 0, 255} &&
              read_pixel(9, 15) == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Production integer samples move edge coverage without modifying material energy");
}
// Owner opacity selects a source section, replaces only published alpha, and
// leaves shadow membership under the independent owner query.
template <class ReadPixel>
void owner_opacity_contract(bw::platform::macos::WorldRenderer& renderer,
                            bw::render::WorldColorDraw draw, const ReadPixel& read_pixel) {
  using bw::render::WorldColorPhase;
  using bw::render::WorldVisibilityUpdate;
  draw.state.depth_compare = bw::render::WorldDepthCompare::greater_equal;
  draw.parameters.clip_rows[2] = {0, 0, 0, .75F};
  draw.parameters.indirect_color[3] = .25F;
  draw.owner_opacity = .5F;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 160, 128, 255},
          "Partial owner replaces indirect alpha once and blends after its source preparation");
  draw.color_phase = WorldColorPhase::masked;
  draw.parameters.clip_rows[2] = {0, 0, 0, .3F};
  draw.parameters.material_color = {1, 0, 0, 1};
  renderer.set_world_draws(std::span(&draw, 1));
  constexpr std::array<std::uint8_t, 4> kPartial{38, 38, 70, 255};
  require(read_pixel() == kPartial,
          "Partial masked owner lowers fixed reference for both depth alpha and final alpha");
  WorldVisibilityUpdate update{draw.model, draw.draw, true, {}, {}, 1};
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 64, 255},
          "Full opacity returns to Main with its ordinary reference and unblended color");
  update.owner_opacity = 0;
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 255},
          "Zero opacity omits both owner preparation and color");
  update.owner_opacity.reset();
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 255},
          "Visibility-only publication retains zero owner opacity");
  update.owner_opacity = .5F;
  renderer.update_world_visibility(std::span(&update, 1));
  auto invalid = update;
  invalid.owner_opacity = std::nanf("");
  update.owner_opacity = 0;
  const std::array batch{update, invalid};
  bool rejected = false;
  try {
    renderer.update_world_visibility(batch);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel() == kPartial,
          "Late invalid opacity rejects the complete publication without hiding the first owner");
  // Main late color precedes Trans early preparation, irrespective of caller
  // order. Equal-depth Trans preparation must overwrite that Main late color.
  auto late = draw;
  late.owner_opacity = 1;
  late.color_phase = WorldColorPhase::material_blended;
  late.parameters.material_color = {0, 1, 0, 1};
  const std::array order{draw, late};
  renderer.set_world_draws(order);
  require(read_pixel() == kPartial,
          "All Main chunks finish before partial-owner preparation and color");
  auto background = draw;
  background.owner_opacity.reset();
  background.color_phase = WorldColorPhase::background;
  background.instance = 1;
  draw.instance = 2;
  const std::array mixed{background, draw};
  renderer.set_world_draws(mixed);
  update.instance = 2;
  update.owner_opacity = .5F;
  renderer.update_world_visibility(std::span(&update, 1));
  require(read_pixel() == kPartial,
          "Instance opacity validation ignores unrelated background instances of the same model");
}
template <class ReadPixel>
void unit_shading_direction_contract(bw::platform::macos::WorldRenderer& renderer,
                                     bw::render::WorldColorDraw draw,
                                     bw::render::WorldTextureHandle base,
                                     const bw::formats::WorldTextureImage& prototype,
                                     const ReadPixel& read_pixel) {
  // The first sample decodes to (1,-1,1), so its cosine is 1/sqrt(3).
  // The second uses a sheared basis: normalized T=(.8,0,.6), N=(0,0,1).
  // Its combined direction has Z cosine .9462734107 rather than the clamped
  // unnormalized value 1. A third orthogonal basis preserves authored detail.
  auto image = prototype;
  draw.parameters.light_direction = {0, 0, -1, 0};
  draw.parameters.light_diffuse = {.25F, .25F, .25F, 0};
  image.pixels = {255, 255, 255, 255};
  const auto outside_disk = renderer.upload_world_texture(image);
  const std::array first_bindings{base, outside_disk};
  renderer.bind_model_textures(draw.model, first_bindings);
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{220, 146, 73, 255},
          "Outside-disk normal remains finite and becomes a unit lighting direction");
  image.pixels = {191, 128, 255, 255};
  const auto mapped = renderer.upload_world_texture(image);
  const std::array second_bindings{base, mapped};
  renderer.bind_model_textures(draw.model, second_bindings);
  draw.parameters.world_rows[2][0] = .75F;
  renderer.set_world_draws(std::span(&draw, 1));
  require(
      read_pixel() == std::array<std::uint8_t, 4>{237, 158, 79, 255},
      "Nonorthogonal mapped basis is normalized before lighting without changing its direction");
  draw.parameters.world_rows[2][0] = 0;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{234, 156, 78, 255},
          "Unit mapped direction preserves its authored angular diffuse response");
  // A spatially constant unit normal and aligned half-vector preserve the
  // authored lobe peak for both positive and zero exponents. Black diffuse
  // isolates the original reflection composition, including its alpha epsilon.
  draw.parameters.world_rows = {{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 1, -1}}};
  draw.parameters.material_color = {0, 0, 0, 1};
  draw.parameters.light_diffuse = {};
  draw.parameters.light_specular = {.4F, .2F, .1F, 1};
  draw.parameters.specular = {32, .5F, 0, 0};
  draw.parameters.reflection = {1, 0, 0, 0};
  draw.parameters.fresnel = {0, 0, 1, 0};
  renderer.set_world_draws(std::span(&draw, 1));
  constexpr std::array<std::uint8_t, 4> kAuthoredPeak{13, 25, 51, 255};
  require(read_pixel() == kAuthoredPeak,
          "Constant normal footprint preserves the authored positive-power specular peak");
  draw.parameters.specular[0] = 0;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == kAuthoredPeak, "Zero-power lobe preserves its authored amplitude");
  draw.parameters.specular[0] = -1;
  bool rejected = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && read_pixel() == kAuthoredPeak,
          "Negative consumed exponent is rejected before replacing the retained draw");
  renderer.release_world_texture(outside_disk);
  renderer.release_world_texture(mapped);
}
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
  image.pixels = {128, 128, 255, 255};
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
  draw.state.mip_filter = bw::render::WorldMipFilter::linear;
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
  const auto read_pixel = [&](std::uint32_t x = extent / 2, std::uint32_t y = extent / 2) {
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
    if (present_scene) {
      renderer.prepare_presentation(command, library, extent, extent,
                                    {.wall_seconds = 1.0F / 60, .presentation_seconds = 1.0F / 60});
    } else {
      renderer.prepare_shadows(command, library);
    }
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
         fromRegion:MTLRegionMake2D(x, y, 1, 1)
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
  scene_preparation_contract(renderer, draw, read_pixel);
  owner_opacity_contract(renderer, draw, read_pixel);
  renderer.set_world_draws(reverse_draws);
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
  unit_shading_direction_contract(renderer, draw, base, image, read_pixel);
  renderer.bind_model_textures(mesh, bindings);
  // A nearly flat unorm normal keeps shadow tests independent of tangent
  // variance. The separate contract above covers outside-disk decoding.
  draw.parameters.light_direction = {0, 0, -1, 0};
  draw.parameters.light_diffuse = {0.25F, 0.25F, 0.25F, 0};
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{240, 160, 80, 255},
          "Flat mapped surface preserves authored diffuse lighting");
  // A separate caster removes directional light without darkening baked
  // vertex illumination. Both passes use the same retained light map.
  auto receiver = draw;
  // Source-enabled variant 1 consumes shadow depth independently of its
  // directional term. The earlier inspection selector explicitly disables it.
  receiver.effective_shader_flags = 0x00820a82;
  receiver.parameters.shadow_clip_rows = receiver.parameters.clip_rows;
  receiver.parameters.shadow_clip_rows[2] = {0, 0, 0, .25F};
  receiver.parameters.shadow_parameters = {1, 0, 0, 0};
  auto caster = receiver;
  caster.casts_shadow = true;
  caster.parameters.clip_rows[0][3] = 4;
  caster.parameters.shadow_clip_rows[2] = {0, 0, 0, .75F};
  std::array shadow_draws{receiver, caster};
  renderer.set_world_draws(shadow_draws);
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Shadow map removes directional light while retaining baked illumination");
  receiver_visibility_contract(renderer, receiver, caster, read_pixel, extent);
  source_shadow_state_contract(renderer, receiver, caster, read_pixel);
  receiver_projection_publication_contract(renderer, receiver, caster, read_pixel);
  shadow_draws[1].visible = false;
  renderer.set_world_draws(shadow_draws);
  require(read_pixel() == std::array<std::uint8_t, 4>{240, 160, 80, 255},
          "Hidden source draw also stops casting into the retained shadow map");
  shadow_draws[1].visible = true;
  shadow_draws[1].casts_shadow = false;
  renderer.set_world_draws(shadow_draws);
  require(read_pixel() == std::array<std::uint8_t, 4>{240, 160, 80, 255},
          "Cleared reverse-depth shadow map restores unobstructed directional light");
  shadow_draws[1].casts_shadow = true;
  shadow_draws[0].parameters.shadow_clip_rows[0][3] = 4;
  renderer.set_world_draws(shadow_draws);
  require(read_pixel() == std::array<std::uint8_t, 4>{240, 160, 80, 255},
          "Original receiver outside the shadow rectangle remains lit");
  // Cover the whole shadow allocation, then verify the protected ring with
  // actual GPU samples. A projection inset keeps real scene coverage inside it.
  shadow_draws[1].parameters.shadow_clip_rows[0] = {4, 0, 0, 0};
  shadow_draws[1].parameters.shadow_clip_rows[1] = {0, 4, 0, 0};
  struct ShadowSample {
    std::array<float, 2> uv;
    float radius;
    float receiver_depth;
  };
  const auto shadow_sample = [&](const ShadowSample& sample) {
    const auto& uv = sample.uv;
    auto& parameters = shadow_draws[0].parameters;
    parameters.shadow_clip_rows = {{{0, 0, 0, 2 * uv[0] - 1},
                                    {0, 0, 0, 1 - 2 * uv[1]},
                                    {0, 0, 0, sample.receiver_depth},
                                    {0, 0, 0, 1}}};
    parameters.shadow_parameters[2] = sample.radius;
    renderer.set_world_draws(shadow_draws);
    return read_pixel();
  };
  constexpr float texel = 1.0F / bw::render::kDirectionalShadowExtent;
  constexpr std::array<std::uint8_t, 4> shadowed{192, 128, 64, 255};
  constexpr std::array<std::uint8_t, 4> unshadowed{240, 160, 80, 255};
  for (const float radius : {0.0F, .058F * texel, 2 * texel}) {
    const float border = static_cast<float>(bw::render::shadow_guard_texels(radius)) * texel;
    const std::array<std::array<float, 2>, 8> edge_centers{{{.5F * texel, .5F},
                                                            {1 - .5F * texel, .5F},
                                                            {.5F, .5F * texel},
                                                            {.5F, 1 - .5F * texel},
                                                            {.5F * texel, .5F * texel},
                                                            {1 - .5F * texel, .5F * texel},
                                                            {.5F * texel, 1 - .5F * texel},
                                                            {1 - .5F * texel, 1 - .5F * texel}}};
    for (const auto& uv : edge_centers) {
      require(shadow_sample({uv, radius, .25F}) == unshadowed,
              "Every edge and corner stays lit when point taps cross the cleared border");
    }
    require(shadow_sample({{.5F, .5F}, radius, .25F}) == shadowed,
            "Protecting shadow borders retains interior caster coverage");
    require(border > radius, "Guard includes the nearest-sample half-texel footprint");
  }
  // Independent expected averages from source seed-1 jitter at screen cell
  // (16,16): three negative X offsets and four negative Y offsets among eight.
  // The source eight-tap compare crosses each inner border and corner here.
  struct FilterCase {
    std::array<float, 2> uv;
    std::array<std::uint8_t, 4> expected;
  };
  constexpr float inner = 3 * texel;
  constexpr std::array<FilterCase, 8> filter_cases{{
      {{inner, .5F}, {210, 140, 70, 255}},
      {{1 - inner, .5F}, {222, 148, 74, 255}},
      {{.5F, inner}, {216, 144, 72, 255}},
      {{.5F, 1 - inner}, {216, 144, 72, 255}},
      {{inner, inner}, {228, 152, 76, 255}},
      {{1 - inner, inner}, {228, 152, 76, 255}},
      {{inner, 1 - inner}, {222, 148, 74, 255}},
      {{1 - inner, 1 - inner}, {234, 156, 78, 255}},
  }};
  for (const auto& sample : filter_cases) {
    require(shadow_sample({sample.uv, 2 * texel, .25F}) == sample.expected,
            "Boundary-crossing filter taps average the exact clear/interior comparisons");
  }
  require(shadow_sample({{.5F, .5F}, 0, .75F}) == unshadowed &&
              shadow_sample({{.5F, .5F}, 0, .8F}) == unshadowed &&
              shadow_sample({{.5F, .5F}, 0, .74F}) == shadowed,
          "Reverse depth compares receiver greater-or-equal, including exact equality");
  for (const auto& uv : std::array<std::array<float, 2>, 4>{
           {{-texel, .5F}, {1 + texel, .5F}, {.5F, -texel}, {.5F, 1 + texel}}}) {
    require(shadow_sample({uv, 2 * texel, .25F}) == unshadowed,
            "Receiver centers outside each source rectangle edge remain unshadowed");
  }
  require(shadow_sample({{.5F, .5F}, 0, .25F}) == shadowed,
          "Perspective shadow comparison starts with a covered interior receiver");
  for (auto& command : shadow_draws) {
    for (auto& row : command.parameters.shadow_clip_rows) {
      for (auto& coefficient : row) coefficient *= 2;
    }
  }
  renderer.set_world_draws(shadow_draws);
  require(read_pixel() == shadowed,
          "Homogeneous caster and receiver W preserve divided shadow coordinates and depth");
  shadow_draws[1].casts_shadow = false;
  renderer.set_world_draws(shadow_draws);
  require(read_pixel() == unshadowed,
          "Reverse clear remains unoccluded for homogeneous perspective shadow coordinates");
  bool rejected_shadow_radius = false;
  try {
    static_cast<void>(shadow_sample({{.5F, .5F}, .5F, .25F}));
  } catch (const std::invalid_argument&) {
    rejected_shadow_radius = true;
  }
  require(rejected_shadow_radius && read_pixel() == unshadowed,
          "Unrepresentable filter footprint rejects atomically before replacing native draws");
  // A slope makes texel-center depth differ from the visible surface sample.
  // Preserve the plane instead of introducing a constant scene depth offset.
  auto plane = receiver;
  plane.casts_shadow = true;
  plane.parameters.shadow_clip_rows[2] = {.01F, 0, 0, .25F};
  for (const float offset : {0.0F, .0005F, -.0005F}) {
    plane.parameters.shadow_clip_rows[0][3] = offset;
    renderer.set_world_draws(std::span(&plane, 1));
    require(read_pixel() == unshadowed,
            "Tilted self-caster stays lit across nearest-texel phase changes");
  }
  plane.parameters.shadow_clip_rows[0][3] = 0;
  auto thin_caster = plane;
  thin_caster.parameters.clip_rows[0][3] = 4;
  thin_caster.parameters.shadow_clip_rows[0] = {.005F, 0, 0, .03125F};
  thin_caster.parameters.shadow_clip_rows[1] = {0, 4, 0, 0};
  // At the measured receiver pixel the plane depth is .2503125: this
  // separation is small, but greater than its derived texel correction.
  thin_caster.parameters.shadow_clip_rows[2] = {0, 0, 0, .25036F};
  const std::array contact_draws{plane, thin_caster};
  renderer.set_world_draws(contact_draws);
  require(read_pixel() == shadowed && read_pixel(extent / 2 + 1) == unshadowed,
          "Thin distinct occluder retains its shadow and adjacent contact edge");
  plane.parameters.shadow_clip_rows = {{{0, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, .25F}, {0, 0, 0, 1}}};
  plane.casts_shadow = false;
  renderer.set_world_draws(std::span(&plane, 1));
  require(read_pixel() == unshadowed,
          "Singular receiver UV derivatives preserve finite unobstructed lighting");
  // Uniformly small homogeneous coordinates represent the same finite view.
  // Derivative normalization must not manufacture an infinite depth slope.
  plane.parameters.shadow_clip_rows = receiver.parameters.shadow_clip_rows;
  for (auto& row : plane.parameters.shadow_clip_rows) {
    for (auto& coefficient : row) coefficient *= 1.0e-20F;
  }
  renderer.set_world_draws(std::span(&plane, 1));
  require(read_pixel() == unshadowed,
          "Near-zero homogeneous scale preserves finite shadow coordinates");
  plane.parameters.shadow_clip_rows[3] = {0, 0, 0, 0};
  renderer.set_world_draws(std::span(&plane, 1));
  require(read_pixel() == unshadowed, "Zero shadow homogeneous W has a bounded unshadowed result");
  plane.parameters.shadow_clip_rows = receiver.parameters.clip_rows;
  plane.parameters.shadow_clip_rows[2] = {1, 0, 0, .25F};
  plane.casts_shadow = true;
  renderer.set_world_draws(std::span(&plane, 1));
  const auto steep_pixel = read_pixel();
  for (std::size_t channel = 0; channel < 3; ++channel) {
    require(steep_pixel.at(channel) >= shadowed.at(channel) &&
                steep_pixel.at(channel) <= unshadowed.at(channel),
            "Steep receiver slope remains within the lighting endpoints");
  }
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
  // No UV attribute or texture exists in this original material family.
  const auto untextured = upload_fixture({.shader = {0, 0}, .untextured = true});
  draw = {.model = untextured,
          .effective_shader_flags = 0x00800200,
          .parameters = surface_parameters,
          .state = surface_state,
          .lighting = bw::render::WorldLightingPolicy::without_direct_lighting};
  draw.parameters.material_color = {.25F, .5F, 1, .75F};
  draw.parameters.illumination = {.125F, .125F, .125F, 0};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    draw.parameters.irradiance_coefficients.at(channel).at(3) = .375F;
  }
  renderer.set_world_draws(std::span(&draw, 1));
  const auto untextured_pixel = read_pixel();
  require(untextured_pixel[0] >= 127 && untextured_pixel[0] <= 128 && untextured_pixel[1] == 64 &&
              untextured_pixel[2] == 32 && untextured_pixel[3] == 191,
          "Untextured material consumes SH and emission without UV or a dummy texture");
  // Visibility changes neither resident bindings nor authored draw order.
  const auto visibility_bytes = domain->snapshot().live_bytes;
  bw::render::WorldVisibilityUpdate visibility{draw.model, draw.draw, false};
  renderer.update_world_visibility(std::span(&visibility, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0} &&
              domain->snapshot().live_bytes == visibility_bytes,
          "Hidden retained draw emits no color and allocates nothing");
  const std::array invalid_visibility{
      bw::render::WorldVisibilityUpdate{draw.model, draw.draw, true},
      bw::render::WorldVisibilityUpdate{{}, draw.draw, true}};
  bool rejected_visibility = false;
  try {
    renderer.update_world_visibility(invalid_visibility);
  } catch (const std::invalid_argument&) {
    rejected_visibility = true;
  }
  require(rejected_visibility && read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Late invalid visibility destination leaves prior state untouched");
  visibility.visible = true;
  renderer.update_world_visibility(std::span(&visibility, 1));
  require(read_pixel() == untextured_pixel,
          "Revealing a retained draw restores the same resident material");
  // Camera publication changes only selected view constants; retained materials
  // and geometry survive both successful updates and late validation failure.
  bw::render::WorldViewParameters view_parameters{
      .clip_rows = draw.parameters.clip_rows,
      .world_rows = draw.parameters.world_rows,
      .fog_color = draw.parameters.fog_color,
      .fog_distance = draw.parameters.fog_distance,
      .fog_height = draw.parameters.fog_height,
      .fog_density = draw.parameters.fog_density,
      .shadow_clip_rows = draw.parameters.shadow_clip_rows};
  bw::render::WorldViewUpdate view_update{view_parameters, view_parameters};
  view_update.world.clip_rows[0][3] += 8;
  draw.view_space = bw::render::WorldViewSpace::world;
  renderer.set_world_draws(std::span(&draw, 1));
  const auto view_budget = domain->snapshot().live_bytes;
  renderer.update_world_view(view_update);
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0} &&
              domain->snapshot().live_bytes == view_budget,
          "Live world projection moves retained geometry without allocation");
  auto invalid_view = bw::render::WorldViewUpdate{view_parameters, view_parameters};
  invalid_view.sky.fog_color[0] = std::nanf("");
  reject_view_update(renderer, invalid_view, read_pixel);
  invalid_view.sky = view_parameters;
  invalid_view.lighting = bw::render::WorldViewUpdate::Lighting{};
  invalid_view.lighting->diffuse[0] = std::nanf("");
  reject_view_update(renderer, invalid_view, read_pixel);
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == untextured_pixel,
          "Republishing a scene resets the prior view override and preserves material constants");
  draw.view_space = bw::render::WorldViewSpace::sky;
  renderer.set_world_draws(std::span(&draw, 1));
  renderer.update_world_view(view_update);
  require(read_pixel() == untextured_pixel,
          "Sky draws select their distinct camera-centered projection");
  draw.view_space = bw::render::WorldViewSpace::fixed;
  renderer.set_world_draws(std::span(&draw, 1));
  view_update.sky.clip_rows[0][3] += 8;
  renderer.update_world_view(view_update);
  require(read_pixel() == untextured_pixel,
          "Fixed draws retain their own projection independently of both live spaces");
  draw.state.mip_filter = bw::render::WorldMipFilter::unresolved;
  bool rejected_mip_filter = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected_mip_filter = true;
  }
  require(rejected_mip_filter && read_pixel() == untextured_pixel,
          "Unresolved mip sampling rejects atomically and retains the prior submission");
  renderer.release_model(untextured);
  // One- and two-influence source programs consume the explicit mapped palette.
  const auto rigid = upload_fixture({.shader = {0xc0000010, 1}, .joint_palette = true});
  const auto weighted = upload_fixture({.shader = {0x80000010, 1}, .joint_palette = true});
  renderer.bind_model_textures(rigid, std::span(&base, 1));
  renderer.bind_model_textures(weighted, std::span(&base, 1));
  std::array<bw::render::WorldJointMatrix, 2> joints{};
  for (auto& matrix : joints) {
    for (std::size_t row = 0; row < 4; ++row) matrix.at(row).at(row) = 1;
  }
  draw = {.model = rigid,
          .effective_shader_flags = 0xc0800210,
          .parameters = surface_parameters,
          .state = surface_state,
          .lighting = bw::render::WorldLightingPolicy::without_direct_lighting,
          .joint_matrices = joints};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    draw.parameters.irradiance_coefficients.at(channel).at(3) = 1;
  }
  renderer.set_world_draws(std::span(&draw, 1));
  joints[0][0][3] = -1.5F;
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Submission owns the joint palette independently of later caller mutation");
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Rigid joint transformation moves the original geometry before view projection");
  joints[1][0][3] = .5F;
  draw.model = weighted;
  draw.effective_shader_flags = 0x80800210;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Two-influence program applies authored normalized-byte weights without renormalization");
  draw.joint_matrices = std::span(joints).first(1);
  bool rejected_palette = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected_palette = true;
  }
  require(rejected_palette && read_pixel() == std::array<std::uint8_t, 4>{192, 128, 64, 255},
          "Out-of-range joint palette rejects before replacing the retained native draw");
  // Four source lanes and separate rest positions share the same draw owner.
  const auto skinned = upload_fixture({.shader = {0x00020490, 1},
                                       .normal_mapped = true,
                                       .shape = true,
                                       .joint_palette = true,
                                       .ambient_occlusion = true,
                                       .joint_indices = {0, 1, 2, 3},
                                       .joint_weights = {63, 64, 64, 64}});
  const std::array skin_textures{base, normal};
  renderer.bind_model_textures(skinned, skin_textures);
  std::array<bw::render::WorldJointMatrix, 4> skin_joints{};
  for (auto& matrix : skin_joints) {
    for (std::size_t row = 0; row < 4; ++row) matrix.at(row).at(row) = 1;
  }
  draw.model = skinned;
  draw.effective_shader_flags = 0x00820690;
  draw.joint_matrices = skin_joints;
  draw.use_shape_positions = true;
  renderer.set_world_draws(std::span(&draw, 1));
  const auto skin_pixel = read_pixel();
  require(skin_pixel[0] > 0,
          "Four-influence skin consumes the explicitly selected rest shape stream");
  const auto pose_budget = domain->snapshot().live_bytes;
  skin_joints[3][0][3] = 8;
  const bw::render::WorldPoseUpdate pose_update{skinned, 0, skin_joints};
  renderer.update_world_poses(std::span(&pose_update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0} &&
              domain->snapshot().live_bytes == pose_budget,
          "Fourth influence updates the retained pose without allocating or resubmitting geometry");
  skin_joints[3][0][3] = 0;
  const std::array pose_batch{pose_update, bw::render::WorldPoseUpdate{{999999}, 0, skin_joints}};
  bool rejected_pose = false;
  try {
    renderer.update_world_poses(pose_batch);
  } catch (const std::invalid_argument&) {
    rejected_pose = true;
  }
  require(rejected_pose && read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Late invalid pose identity rejects the complete batch without partial writes");
  renderer.update_world_poses(std::span(&pose_update, 1));
  require(read_pixel() == skin_pixel, "Retained pose update restores the original skinned pose");
  const bw::render::WorldLightingUpdate original_light{skinned, 0,
                                                       draw.parameters.irradiance_coefficients};
  auto dark_light = original_light;
  dark_light.irradiance_coefficients = {};
  renderer.update_world_lighting(std::span(&dark_light, 1));
  const auto dark_pixel = read_pixel();
  require(dark_pixel[0] < skin_pixel[0] && domain->snapshot().live_bytes == pose_budget,
          "Spatial lighting updates retained skin without geometry upload or allocation");
  const std::array light_batch{
      original_light,
      bw::render::WorldLightingUpdate{{999999}, 0, original_light.irradiance_coefficients}};
  bool rejected_light = false;
  try {
    renderer.update_world_lighting(light_batch);
  } catch (const std::invalid_argument&) {
    rejected_light = true;
  }
  require(rejected_light && read_pixel() == dark_pixel,
          "Late invalid lighting destination preserves all earlier retained constants");
  auto invalid_light = original_light;
  invalid_light.irradiance_coefficients[0][0] = std::numeric_limits<float>::quiet_NaN();
  rejected_light = false;
  try {
    renderer.update_world_lighting(std::span(&invalid_light, 1));
  } catch (const std::invalid_argument&) {
    rejected_light = true;
  }
  require(rejected_light && read_pixel() == dark_pixel,
          "Nonfinite lighting cannot contaminate retained rendering");
  renderer.update_world_lighting(std::span(&original_light, 1));
  require(read_pixel() == skin_pixel, "Spatial lighting restoration preserves skin pose and view");
  receiver_instance_contract(renderer, draw, original_light, read_pixel, extent);
  // Exercise the original direct-light material path independently of SH.
  // One emitter in front of the surface contributes; moving it behind removes
  // that contribution. Invalid batches cannot partially replace either field.
  draw.lighting = bw::render::WorldLightingPolicy::supplied;
  draw.parameters.irradiance_coefficients = {};
  renderer.set_world_draws(std::span(&draw, 1));
  const auto unlit_local_pixel = read_pixel();
  auto direct_light = dark_light;
  direct_light.local_light_count = 1;
  direct_light.local_lights[0] = {{0, 0, 3, 1}, {1, 1, 1, 1}, {0, 0, 0, 0}, {1, 1, 1, .05F}};
  renderer.update_world_lighting(std::span(&direct_light, 1));
  const auto direct_pixel = read_pixel();
  require(direct_pixel[0] > unlit_local_pixel[0],
          "Original local diffuse loop illuminates the retained skin");
  auto invalid_direct = direct_light;
  invalid_direct.local_light_count = bw::render::kMaximumWorldLocalLights + 1;
  const std::array direct_batch{dark_light, invalid_direct};
  rejected_light = false;
  try {
    renderer.update_world_lighting(direct_batch);
  } catch (const std::invalid_argument&) {
    rejected_light = true;
  }
  require(rejected_light && read_pixel() == direct_pixel,
          "Invalid direct count rejects whole SH/direct publication atomically");
  direct_light.local_lights[0].position[2] = -3;
  renderer.update_world_lighting(std::span(&direct_light, 1));
  require(read_pixel() == unlit_local_pixel,
          "Back-facing local light does not illuminate the surface");
  renderer.update_world_lighting(std::span(&dark_light, 1));
  require(read_pixel() == unlit_local_pixel, "Empty packet clears previous direct-light slots");
  draw.lighting = bw::render::WorldLightingPolicy::without_direct_lighting;
  draw.parameters.irradiance_coefficients = original_light.irradiance_coefficients;
  renderer.set_world_draws(std::span(&draw, 1));
  // CPU shape positions are retained once, then consumed by skinning without
  // changing normals, materials, palettes or native allocation ownership.
  std::array<std::array<float, 3>, 3> shape_positions{{{-1, -1, .5F}, {1, -1, .5F}, {0, 1, .5F}}};
  const bw::render::WorldShapePositionUpdate shape_update{skinned, 0, shape_positions};
  for (auto& position : shape_positions) position[0] += 4;
  renderer.update_shape_positions(std::span(&shape_update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0} &&
              domain->snapshot().live_bytes == pose_budget,
          "CPU shape deformation updates native skin positions without allocating");
  for (auto& position : shape_positions) position[0] -= 4;
  const std::array shape_batch{shape_update,
                               bw::render::WorldShapePositionUpdate{{999999}, 0, shape_positions}};
  bool rejected_shape_batch = false;
  try {
    renderer.update_shape_positions(shape_batch);
  } catch (const std::invalid_argument&) {
    rejected_shape_batch = true;
  }
  require(rejected_shape_batch && read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 0},
          "Late invalid shape destination preserves the previous whole publication");
  renderer.update_shape_positions(std::span(&shape_update, 1));
  require(read_pixel() == skin_pixel, "Zero shape deformation restores authored positions");
  // Unrelated live palettes survive replacement, and candidate palettes are
  // owned before the caller resumes or reuses its scratch matrices.
  auto retained_skin = draw;
  retained_skin.parameters.material_color = {1, 0, 0, 1};
  auto previous_skin = draw;
  previous_skin.model = rigid;
  previous_skin.effective_shader_flags = 0xc0800210;
  previous_skin.joint_matrices = joints;
  previous_skin.use_shape_positions = false;
  const std::array palette_scene{previous_skin, retained_skin};
  renderer.set_world_draws(palette_scene);
  skin_joints[3][0][3] = 8;
  renderer.update_world_poses(std::span(&pose_update, 1));
  auto candidate_skin = previous_skin;
  candidate_skin.model = weighted;
  candidate_skin.effective_shader_flags = 0x80800210;
  renderer.replace_world_model_draws(rigid, std::span(&candidate_skin, 1));
  const auto candidate_pixel = read_pixel();
  joints[0][0][3] = 20;
  require(candidate_pixel == std::array<std::uint8_t, 4>{192, 128, 64, 255} &&
              read_pixel() == candidate_pixel,
          "Replacement owns candidate palettes while preserving unrelated latest poses");
  skin_joints[3][0][3] = 0;
  renderer.update_world_poses(std::span(&pose_update, 1));
  require(read_pixel()[0] == 0 && read_pixel()[1] == 0 && read_pixel()[2] > 0,
          "Retained animated component keeps its pose update destination after replacement");
  renderer.set_world_draws(std::span(&draw, 1));
  draw.use_shape_positions = false;
  bool rejected_shape = false;
  try {
    renderer.set_world_draws(std::span(&draw, 1));
  } catch (const std::invalid_argument&) {
    rejected_shape = true;
  }
  require(rejected_shape && read_pixel() == skin_pixel,
          "Shape streams require explicit rest-shape policy before publication");
  // The costume layer interpolates two base RGB values and applies its
  // separate Fresnel coefficient only to output alpha, including the SH path.
  const auto clothing = upload_fixture({.shader = {0x824080b0, 2},
                                        .normal_mapped = true,
                                        .secondary_base = true,
                                        .joint_palette = true,
                                        .vertex_modulation = true});
  const std::array clothing_textures{base, normal, base};
  renderer.bind_model_textures(clothing, clothing_textures);
  draw = {.model = clothing,
          .effective_shader_flags = 0x82c082b0,
          .parameters = surface_parameters,
          .state = surface_state,
          .lighting = bw::render::WorldLightingPolicy::without_direct_lighting,
          .joint_matrices = std::span(skin_joints).first(2)};
  draw.parameters.indirect_secondary = {.5F, .5F, .5F, 1};
  draw.parameters.fresnel = {1, 0, .25F, 0};
  for (std::size_t channel = 0; channel < 3; ++channel) {
    draw.parameters.irradiance_coefficients.at(channel).at(3) = 1;
  }
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{96, 64, 32, 64},
          "Clothing secondary interpolation and Fresnel alpha match the original SH program");
  renderer.release_model(clothing);
  renderer.release_model(skinned);
  renderer.release_model(rigid);
  renderer.release_model(weighted);
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
  // Animate only retained UV constants: the same resident geometry and sampler
  // selects another authored texel without replacing the submission.
  image.width = 2;
  image.pixels = {255, 0, 0, 255, 0, 255, 0, 255};
  const auto offset_texture = renderer.upload_world_texture(image);
  renderer.bind_model_textures(geometric, std::span(&offset_texture, 1));
  draw.state.min_filter = WorldTextureFilter::nearest;
  draw.state.mag_filter = WorldTextureFilter::nearest;
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "Unchanged UV constants address the first authored texel");
  bw::render::WorldTextureOffsetUpdate offset_update{.model = geometric};
  offset_update.texture_offset[0][0] = .5F;
  const auto before_offset_update = budget->snapshot();
  renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 255, 0, 255} &&
              budget->snapshot().live_resources == before_offset_update.live_resources,
          "Retained UV update changes sampling without acquiring device resources");
  auto invalid_update = offset_update;
  invalid_update.draw = model.draws().size();
  offset_update.texture_offset = {};
  const std::array invalid_updates{offset_update, invalid_update};
  bool rejected_offsets = false;
  try {
    renderer.update_world_texture_offsets(invalid_updates);
  } catch (const std::invalid_argument&) {
    rejected_offsets = true;
  }
  require(rejected_offsets && read_pixel() == std::array<std::uint8_t, 4>{0, 255, 0, 255},
          "Invalid late UV update rejects the entire batch before changing constants");
  auto blended_offset_draw = draw;
  blended_offset_draw.state.blend = bw::render::WorldBlend::source_alpha;
  blended_offset_draw.parameters.material_color[3] = .5F;
  const std::array duplicate_offsets{blended_offset_draw, blended_offset_draw};
  renderer.set_world_draws(duplicate_offsets);
  offset_update.texture_offset[0][0] = .5F;
  renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  const auto duplicate_pixel = read_pixel();
  require(duplicate_pixel[0] == 0 && duplicate_pixel[2] == 0 && duplicate_pixel[1] >= 191 &&
              duplicate_pixel[1] <= 192,
          "One indexed UV update reaches every retained submission of the model draw");
  // A paused component swap preserves unrelated live UV/view state, replaces
  // nonadjacent old entries once, and rebuilds only the update destination index.
  const auto previous_body = upload_fixture({.shader = {0x00420802, 1}, .base_only = true});
  const auto replacement_body = upload_fixture({.shader = {0x00420802, 1}, .base_only = true});
  const auto previous_private_texture = renderer.upload_world_texture(image);
  renderer.bind_model_textures(previous_body, std::span(&previous_private_texture, 1));
  renderer.bind_model_textures(replacement_body, std::span(&base, 1));
  auto body_draw = blended_offset_draw;
  body_draw.model = previous_body;
  body_draw.view_space = bw::render::WorldViewSpace::world;
  auto retained_draw = blended_offset_draw;
  retained_draw.view_space = bw::render::WorldViewSpace::world;
  const std::array prior_component_draws{body_draw, retained_draw, body_draw};
  renderer.set_world_draws(prior_component_draws);
  renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  bw::render::WorldViewUpdate retained_view{};
  retained_view.world.clip_rows = surface_parameters.clip_rows;
  retained_view.world.world_rows = surface_parameters.world_rows;
  retained_view.world.clip_rows[0][3] = 4;
  renderer.update_world_view(retained_view);
  auto replacement_draw = body_draw;
  replacement_draw.model = replacement_body;
  auto invalid_replacement = replacement_draw;
  invalid_replacement.draw = 999999;
  const std::array invalid_replacements{replacement_draw, invalid_replacement};
  const auto prior_replacement_budget = budget->snapshot();
  bool rejected_replacement = false;
  try {
    renderer.replace_world_model_draws(previous_body, invalid_replacements);
  } catch (const std::invalid_argument&) {
    rejected_replacement = true;
  }
  const bw::render::WorldTextureOffsetUpdate prior_body_update{.model = previous_body};
  renderer.update_world_texture_offsets(std::span(&prior_body_update, 1));
  require(rejected_replacement && read_pixel() == std::array<std::uint8_t, 4>{} &&
              budget->snapshot().live_resources == prior_replacement_budget.live_resources,
          "Invalid replacement retains previous identities, view and device resources");
  renderer.replace_world_model_draws(previous_body, std::span(&replacement_draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{} &&
              budget->snapshot().live_resources == prior_replacement_budget.live_resources,
          "Successful replacement preserves the current view without GPU resource upload");
  retained_view.world.clip_rows[0][3] = 0;
  renderer.update_world_view(retained_view);
  const auto replaced_pixel = read_pixel();
  require(replaced_pixel[0] >= 47 && replaced_pixel[0] <= 49 && replaced_pixel[1] >= 159 &&
              replaced_pixel[1] <= 161 && replaced_pixel[2] >= 15 && replaced_pixel[2] <= 17,
          "Replacement removes all old draws at their first position and retains live green UVs");
  bool rejected_old_component = false;
  try {
    renderer.update_world_texture_offsets(std::span(&prior_body_update, 1));
  } catch (const std::invalid_argument&) {
    rejected_old_component = true;
  }
  const bw::render::WorldTextureOffsetUpdate new_body_update{.model = replacement_body};
  renderer.update_world_texture_offsets(std::span(&new_body_update, 1));
  renderer.release_model(previous_body);
  renderer.release_world_texture(previous_private_texture);
  require(rejected_old_component && read_pixel() == replaced_pixel,
          "Old component geometry and private texture retirement preserve new and unrelated draws");
  renderer.release_model(replacement_body);
  renderer.set_world_draws(duplicate_offsets);
  renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  renderer.bind_model_textures(geometric, std::span(&offset_texture, 1));
  bool rejected_stale_offset = false;
  try {
    renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  } catch (const std::invalid_argument&) {
    rejected_stale_offset = true;
  }
  require(rejected_stale_offset && !renderer.has_color_draws(),
          "Rebinding textures invalidates all old UV destinations with their submissions");
  renderer.set_world_draws(std::span(&draw, 1));
  renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 255, 0, 255},
          "A new submission rebuilds UV destinations after binding invalidation");
  renderer.release_world_texture(offset_texture);
  rejected_stale_offset = false;
  try {
    renderer.update_world_texture_offsets(std::span(&offset_update, 1));
  } catch (const std::invalid_argument&) {
    rejected_stale_offset = true;
  }
  require(rejected_stale_offset && !renderer.has_color_draws(),
          "Texture release removes stale destinations before subsequent animation updates");
  image.width = 1;
  renderer.bind_model_textures(geometric, std::span(&base, 1));
  draw.state = surface_state;
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
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{192, 128, 32, 255},
          "Layered family accepts supplied lighting with zero directional energy");
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
  // PW5's original pixel123 independently consumes AMOC red and green. The
  // fixture aliases base/AMOC deliberately: no diffuse energy is supplied, so
  // changing green can only admit reflection, not alter lit base color.
  image.shape = bw::formats::WorldTextureShape::two_dimensional;
  image.pixels = {255, 0, 0, 255};
  const auto reflection_blocked = renderer.upload_world_texture(image);
  image.pixels = {255, 255, 0, 255};
  const auto reflection_allowed = renderer.upload_world_texture(image);
  const auto occluded_reflective = upload_fixture({.shader = {0x00022490, 1},
                                                   .normal_mapped = true,
                                                   .environment = true,
                                                   .joint_palette = true,
                                                   .ambient_occlusion = true});
  std::array<bw::render::WorldJointMatrix, 4> reflection_joints{};
  for (auto& matrix : reflection_joints) {
    for (std::size_t row = 0; row < 4; ++row) matrix.at(row).at(row) = 1;
  }
  draw.model = occluded_reflective;
  draw.effective_shader_flags = 0x00822690;
  draw.joint_matrices = reflection_joints;
  const std::array<WorldTextureHandle, 4> blocked_bindings{reflection_blocked, normal, {}, cube};
  renderer.bind_model_textures(occluded_reflective, blocked_bindings);
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 0, 255},
          "AMOC green zero suppresses reflection while red remains one");
  const std::array<WorldTextureHandle, 4> allowed_bindings{reflection_allowed, normal, {}, cube};
  renderer.bind_model_textures(occluded_reflective, allowed_bindings);
  renderer.set_world_draws(std::span(&draw, 1));
  require(read_pixel() == std::array<std::uint8_t, 4>{0, 0, 255, 255},
          "AMOC green one retains the original cubemap response");
  // Keep all color inputs fixed and vary only source family, facing and an
  // actual caster. Static PS225 does not shadow its cubemap; actor PS123 does.
  const auto reflected_shadow_pixel = [&](bool actor_family, bool shadow, bool facing) {
    auto reflected_receiver = draw;
    reflected_receiver.model = actor_family ? occluded_reflective : reflective;
    reflected_receiver.effective_shader_flags = actor_family ? 0x00822690 : 0x00e22882;
    reflected_receiver.joint_matrices =
        actor_family ? std::span<const bw::render::WorldJointMatrix>(reflection_joints)
                     : std::span<const bw::render::WorldJointMatrix>{};
    auto& parameters = reflected_receiver.parameters;
    parameters.material_color = {0, 0, 0, 1};
    parameters.light_direction = {0, 0, facing ? -1.0F : 1.0F, 0};
    parameters.reflection[3] = .25F;
    parameters.shadow_clip_rows = parameters.clip_rows;
    parameters.shadow_clip_rows[2] = {0, 0, 0, .25F};
    parameters.shadow_parameters = {1, 0, 0, 0};
    auto reflected_caster = reflected_receiver;
    reflected_caster.casts_shadow = shadow;
    reflected_caster.parameters.clip_rows[0][3] = 4;
    reflected_caster.parameters.shadow_clip_rows[2] = {0, 0, 0, .75F};
    const std::array reflected_draws{reflected_receiver, reflected_caster};
    renderer.set_world_draws(reflected_draws);
    return read_pixel();
  };
  const auto static_lit = reflected_shadow_pixel(false, false, true);
  require(reflected_shadow_pixel(false, true, true) == static_lit && static_lit[2] == 255,
          "Static environment facing is unchanged by shadow visibility");
  const auto actor_lit = reflected_shadow_pixel(true, false, true);
  const auto actor_shadowed = reflected_shadow_pixel(true, true, true);
  require(actor_lit[2] == 255 && actor_shadowed[2] >= 63 && actor_shadowed[2] <= 64,
          "Receiver environment facing uses visibility and retains authored reflection floor");
  for (const bool actor_family : {false, true}) {
    const auto zero_facing = reflected_shadow_pixel(actor_family, false, false);
    require(reflected_shadow_pixel(actor_family, true, false) == zero_facing &&
                zero_facing[2] >= 63 && zero_facing[2] <= 64,
            "Both environment families retain the reflection floor at zero facing");
  }
  draw.model = reflective;
  draw.effective_shader_flags = 0x00e22882;
  draw.joint_matrices = {};
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
  receiver_reflection_contract(renderer, draw, {cube, base, normal}, reflective, read_pixel,
                               extent);
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
  // Scene-wide phases survive model-major publication: late translucency must
  // composite over opaque work even when that model was submitted first.
  auto opaque_phase = draw;
  opaque_phase.state.blend = bw::render::WorldBlend::opaque;
  opaque_phase.parameters.material_color = {1, 0, 0, 1};
  auto blended_phase = draw;
  blended_phase.state.depth_write = false;
  blended_phase.parameters.material_color = {0, 0, 1, 1};
  blended_phase.color_phase = bw::render::WorldColorPhase::material_blended;
  const std::array ordered_phases{opaque_phase, blended_phase};
  renderer.set_world_draws(ordered_phases);
  const auto ordered_pixel = read_pixel();
  const std::array reversed_phases{blended_phase, opaque_phase};
  renderer.set_world_draws(reversed_phases);
  require(read_pixel() == ordered_pixel && ordered_pixel[0] != 0 && ordered_pixel[2] != 0,
          "Prepared scene phases composite late material over opaque independent of model order");
  blended_phase.color_phase = bw::render::WorldColorPhase::opaque;
  const std::array same_phase{blended_phase, opaque_phase};
  renderer.set_world_draws(same_phase);
  require(read_pixel() != ordered_pixel,
          "Commands within one phase retain caller ordering rather than incidental blend sorting");
  const auto cutout = upload_fixture({.shader = {0x00020883, 1}, .normal_mapped = true});
  const std::array<WorldTextureHandle, 2> cutout_bindings{translucent, normal};
  renderer.bind_model_textures(cutout, cutout_bindings);
  draw = {.model = cutout,
          .effective_shader_flags = 0x00e20883,
          .parameters = surface_parameters,
          .state = surface_state};
  color_coverage_contract(renderer, draw, read_pixel);
  for (auto handle : {geometric, layered, shadow_layered, local_lit, sky, reflective,
                      occluded_reflective, fallback_model, cutout}) {
    renderer.release_model(handle);
  }
  for (auto handle :
       {secondary, sky_texture, cube, translucent, reflection_blocked, reflection_allowed}) {
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
  BWTestFinalEdge(device);
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
    std::array<float, 4> global_color{1, 1, 1, 1};
    std::array<float, 4> first_measurement_bounds{};
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
  // Source pool pixel17 multiplies RGB without saturating HDR and preserves
  // sampled alpha; this fixture also distinguishes point from linear sampling.
  source = bw::platform::macos::TextureAllocation::create(device, budget, 2, 2,
                                                          MTLPixelFormatRGBA16Float);
  std::array<std::uint16_t, 16> colors{0x3400, 0x3800, 0x4000, 0x3600, 0x3c00, 0x3c00,
                                       0x3c00, 0x3c00, 0x4200, 0x4200, 0x4200, 0x3c00,
                                       0x4400, 0x4400, 0x4400, 0x3c00};
  [source.get() replaceRegion:MTLRegionMake2D(0, 0, 2, 2)
                  mipmapLevel:0
                    withBytes:colors.data()
                  bytesPerRow:8 * sizeof(std::uint16_t)];
  parameters.inverse_extent = {.5F, .5F};
  parameters.global_color = {2, 3, .25F, 9};
  execute(@"world_scale_rgb", log_image.get());
  [log_image.get() getBytes:sample.data()
                bytesPerRow:sizeof(sample)
                 fromRegion:MTLRegionMake2D(0, 0, 1, 1)
                mipmapLevel:0];
  require(sample == std::array<std::uint16_t, 4>{0x3800, 0x3e00, 0x3800, 0x3600},
          "Source RGB filter preserves HDR above one and original alpha");

  // Exercise owner ordering: the same filtered HDR must feed both measurement
  // and final display. Test-only readback follows completion; retained targets
  // stay charged and are reused on the next frame (I.11 / GPU.9, both phases).
  for (std::size_t i = 0; i < colors.size(); i += 4) {
    colors.at(i) = 0x3400;
    colors.at(i + 1) = 0x3800;
    colors.at(i + 2) = 0x4000;
    colors.at(i + 3) = 0x3600;
  }
  [source.get() replaceRegion:MTLRegionMake2D(0, 0, 2, 2)
                  mipmapLevel:0
                    withBytes:colors.data()
                  bytesPerRow:8 * sizeof(std::uint16_t)];
  auto* paint_descriptor = [[MTLRenderPipelineDescriptor alloc] init];
  paint_descriptor.vertexFunction = [library newFunctionWithName:@"world_output_vertex"];
  paint_descriptor.fragmentFunction = [library newFunctionWithName:@"world_scale_rgb"];
  paint_descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA16Float;
  paint_descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
  auto paint = [device newRenderPipelineStateWithDescriptor:paint_descriptor error:nil];
  require(paint != nil, "Scene fixture paint pipeline available");
  bw::platform::macos::WorldPostprocess owner(device, domain, budget);
  bw::render::WorldOutputParameters policy;
  policy.global_color_mode = 2;
  policy.global_color = {2, -4, .25F, 9};
  owner.configure(policy);
  parameters.global_color = {1, 1, 1, 1};
  const auto render_frame = [&](float elapsed = 1.0F / 60, bool paint_scene = true,
                                std::array<float, 3> clear = {}) {
    auto command = [queue commandBuffer];
    auto encoder = owner.begin_scene(command, 2, 2, clear);
    if (paint_scene) {
      [encoder setRenderPipelineState:paint];
      [encoder setFragmentTexture:source.get() atIndex:0];
      [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:0];
      [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    [encoder endEncoding];
    owner.prepare_output(command, library,
                         {.wall_seconds = elapsed, .presentation_seconds = elapsed});
    auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = output.get();
    pass.colorAttachments[0].loadAction = MTLLoadActionDontCare;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    encoder = [command renderCommandEncoderWithDescriptor:pass];
    owner.encode_display(encoder, library, 1);
    [encoder endEncoding];
    [command commit];
    [command waitUntilCompleted];
    require(command.status == MTLCommandBufferStatusCompleted, "Filtered scene chain completed");
    [output.get() getBytes:display.data()
               bytesPerRow:4
                fromRegion:MTLRegionMake2D(0, 0, 1, 1)
               mipmapLevel:0];
  };
  render_frame();
  require(display.at(0) == 128 && display.at(1) == 0 && display.at(2) == 128,
          "Pre-tone filter runs when tone mapping is disabled and clamps negative multipliers");
  const auto live = budget->snapshot().live_logical_bytes;
  render_frame();
  require(budget->snapshot().live_logical_bytes == live,
          "Filtered scene storage persists across completed frames");
  policy.tone_mapping_enabled = true;
  policy.exposure_key = .18F;
  policy.gamma = 1;
  owner.configure(policy);
  render_frame();
  require(std::abs(static_cast<int>(display.at(0)) - 111) <= 2 && display.at(1) == 0 &&
              std::abs(static_cast<int>(display.at(2)) - 111) <= 2,
          "Luminance reduction and final transfer both consume the filtered HDR scene");
  // A reset removes every prior GPU measurement. Each following assertion is
  // therefore the genuinely first submitted frame under a different source
  // output-bound policy, rather than a check that only starts passing later.
  policy.bloom_enabled = true;
  policy.bloom_exposure_key = .01F;
  policy.bloom_threshold = .01F;
  policy.bloom_soft_knee = .01F;
  policy.exposure = {64, 1, .07F, 2.6F};
  policy.global_color = {.2F, -4, .025F, 9};
  owner.reset();
  owner.configure(policy);
  render_frame();
  require(std::abs(static_cast<int>(display.at(2)) - 32) <= 1,
          "First native bloom and final transfer obey the source lower luminance bound");
  policy.bloom_enabled = false;
  policy.exposure = {64, 1, .07F, .1F};
  policy.global_color = {2, -4, .25F, 9};
  owner.reset();
  owner.configure(policy);
  render_frame();
  require(std::abs(static_cast<int>(display.at(2)) - 227) <= 1,
          "First native measurement obeys the source upper luminance bound");
  policy.exposure.history_window = 0;
  owner.reset();
  owner.configure(policy);
  render_frame();
  require(std::abs(static_cast<int>(display.at(2)) - 111) <= 2,
          "No-history native output keeps the source unbounded GPU measurement");
  // Establish real history from a uniform surface, then change the surface
  // while tone mapping is disabled. Re-enable must consume the retained old
  // measurement/history, not initialize again from the new image.
  const auto upload_gray = [&](std::uint16_t gray) {
    for (std::size_t i = 0; i < colors.size(); i += 4) {
      colors.at(i) = gray;
      colors.at(i + 1) = gray;
      colors.at(i + 2) = gray;
      colors.at(i + 3) = 0x3c00;
    }
    [source.get() replaceRegion:MTLRegionMake2D(0, 0, 2, 2)
                    mipmapLevel:0
                      withBytes:colors.data()
                    bytesPerRow:8 * sizeof(std::uint16_t)];
  };
  owner.reset();
  policy.global_color_mode = 0;
  policy.exposure = {2, .005F, {}, {}};
  owner.configure(policy);
  upload_gray(0x3400);  // One quarter in IEEE binary16.
  render_frame();
  render_frame(1.0F / 30);
  render_frame(1.0F / 30);
  policy.tone_mapping_enabled = false;
  owner.configure(policy);
  upload_gray(0x3c00);  // One in IEEE binary16.
  render_frame();
  require(display.at(2) == 255, "Disabled tone mapping displays the current scene");
  policy.tone_mapping_enabled = true;
  owner.configure(policy);
  render_frame(0);
  require(std::abs(static_cast<int>(display.at(2)) - 183) <= 2,
          "Tone re-enable preserves the real predecessor and retained history");
  // Event reset keeps the already measured bright frame; full reset below
  // initializes from actual current quarter-gray instead.
  owner.reset_exposure_history();
  upload_gray(0x3400);
  render_frame(0);
  require(std::abs(static_cast<int>(display.at(2)) - 11) <= 2,
          "Event history reset retains the completed luminance predecessor");
  upload_gray(0x3c00);
  render_frame(0);
  owner.release_scene_targets();
  upload_gray(0x3400);
  render_frame(0);
  require(std::abs(static_cast<int>(display.at(2)) - 11) <= 2,
          "Scene replacement preserves output history and the prior measured image");
  owner.reset();
  render_frame(0);
  require(std::abs(static_cast<int>(display.at(2)) - 46) <= 2,
          "Full output lifetime reset initializes from the actual first image");
  policy.tone_mapping_enabled = false;
  owner.configure(policy);
  render_frame(0, false, {.25F, .5F, .75F});
  require(display.at(0) == 191 && display.at(1) == 128 && display.at(2) == 64,
          "Authored clear RGB reaches the HDR and native display path");
  const auto before_invalid_clear = budget->snapshot().live_logical_bytes;
  bool invalid_clear = false;
  try {
    owner.begin_scene([queue commandBuffer], 4, 4,
                      { std::numeric_limits<float>::quiet_NaN(), 0, 0 });
  } catch (const std::invalid_argument&) {
    invalid_clear = true;
  }
  require(invalid_clear && budget->snapshot().live_logical_bytes == before_invalid_clear,
          "Invalid clear rejects before scene allocation or retained mutation");
  auto pending_output = [queue commandBuffer];
  auto pending_encoder = owner.begin_scene(pending_output, 2, 2);
  [pending_encoder endEncoding];
  owner.prepare_output(pending_output, library, {});
  policy.tone_mapping_enabled = true;
  owner.configure(policy);
  bool premature_reuse = false;
  try {
    owner.begin_scene([queue commandBuffer], 2, 2);
  } catch (const std::logic_error&) {
    premature_reuse = true;
  }
  require(premature_reuse, "Tone toggle cannot erase the outstanding GPU completion boundary");
  [pending_output commit];
  [pending_output waitUntilCompleted];
  require(pending_output.status == MTLCommandBufferStatusCompleted,
          "Outstanding output fixture completes before teardown");
  owner.reset();
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
    BWTestSceneReplay(device);
    world_surface_contract(device);
    world_output_contract(device);
    BWTestTextureMips(device);
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
