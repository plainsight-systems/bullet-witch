// Integration test: bw/rendering/model_draw.hpp on this Mac's Metal device
// with the build's forward lit artifacts. forward_lit_pixels and
// failures_named draw a synthetic model written to a temporary converted
// layer with the converter's encoders (asset_data.hpp) and loaded by
// load_model (model_resources.hpp); pw0_draws_with_materials draws PW0 from
// the cache the CTest fixture pw0_cache converted.
//
// Arguments after the scenario name: the Metal artifact directory and a
// scratch directory; pw0_draws_with_materials takes the PW0 cache third.
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/content_converter/asset_data.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/material.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/materials/material_buffer.hpp"
#include "bw/rendering/model_draw.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/forward_pass.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/target_pass.hpp"
#include "bw/rendering/texture_asset.hpp"
#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace cc = bw::content_converter;
namespace content = bw::content;
namespace geometry = bw::geometry;
namespace m = bw::materials;
namespace rendering = bw::rendering;
namespace rhi = bw::rhi;
using bw::testing::require;

struct Arguments {
  fs::path shaders{};
  fs::path scratch{};
  fs::path pw0_cache{};
};

template <class T, class E> T take(rhi::Result<T, E> result, std::string_view what) {
  if (!result.ok()) {
    throw std::runtime_error(std::string(what) + " failed with code " +
                             std::to_string(static_cast<int>(result.error())) + ": " +
                             std::string(result.detail()));
  }
  return std::move(result.value());
}

std::unique_ptr<rhi::Device> metal_device() {
  rhi::DeviceDescription description{};
  description.backend = rhi::BackendKind::Metal;
  description.miss_policy = rhi::PipelineMissPolicy::CreateOnMiss;
  return take(rhi::Device::create(description), "Device::create");
}

// ---- the synthetic model ----------------------------------------------------

constexpr std::string_view kMeshId = "model.fixture.mesh";
constexpr std::string_view kTextureId = "model.fixture.texture.base";
constexpr std::string_view kNormalTextureId = "model.fixture.texture.normal";
constexpr std::string_view kPackedTextureId = "model.fixture.texture.packed";
constexpr std::uint32_t kColumns = 5;
constexpr std::uint32_t kRows = 5;
constexpr std::uint32_t kCellPixels = 4;
constexpr rhi::Extent2 kTarget{kColumns * kCellPixels, kRows* kCellPixels};
constexpr std::array<float, 4> kClear{0.25F, 0.5F, 0.75F, 1.0F};
// Each texture is 2 by 1, as halfs, with the 1 by 1 mip the payload
// requires as the average; a quad magnifies its texture, so the mip is never
// sampled. The base texture: one colour (0.5, 0.25, 1), alpha 0 in the left
// texel and 1 in the right. A quad's texture coordinate runs 0 to 1 left to
// right, so under the masked threshold 0.5 its leftmost pixel (alpha 0.25
// sampled with wrap) is discarded and its rightmost (0.75) drawn.
constexpr std::array<std::uint16_t, 8> kTexels{0x3800, 0x3400, 0x3C00, 0x0000,
                                               0x3800, 0x3400, 0x3C00, 0x3C00};
constexpr std::array<std::uint16_t, 4> kMip1{0x3800, 0x3400, 0x3C00, 0x3800};
// The normal map: both texels (0.75, 0.625), so t.xy = (0.5, 0.25).
constexpr std::array<std::uint16_t, 4> kNormalTexel{0x3A00, 0x3900, 0x3C00, 0x3C00};
// The occlusion and reflectance map: both texels (0.5, 0.25).
constexpr std::array<std::uint16_t, 4> kPackedTexel{0x3800, 0x3400, 0x0000, 0x3C00};

struct Quad {
  std::uint32_t cell{};
  // The vertex normal's x and z; (0, 1) faces the eye, (0, -1) faces away.
  std::array<float, 2> normal{0.0F, 1.0F};
  std::uint32_t material{};
  // The texture coordinate runs right to left, and the tangent is (-1, 0, 0)
  // with w +1 where an unmirrored quad has (1, 0, 0) with w -1.
  bool mirrored{};
  // Depth: the position's z, which is its depth through an identity
  // world_to_clip.
  float z{0.5F};
  // Wound the other way round.
  bool reversed{};
  // Authored this many cells to the right of its cell.
  float shift{};
};

struct SyntheticModel {
  std::vector<cc::MaterialAssetData> materials{};
  std::vector<Quad> quads{};
  bool tangent{true};
};

// A lit material asset under the table of material_asset.hpp; the defaults
// are the template's.
struct LitSpec {
  m::BlendMode blend{m::BlendMode::Opaque};
  bool base_color_map{};
  bool normal_map{};
  bool occlusion_reflectance_map{};
  std::array<float, 4> tint{1.0F, 1.0F, 1.0F, 1.0F};
  std::array<float, 4> emissive{};
  float roughness{1.0F};
  float reflectance{0.04F};
  float grazing_reflectance{1.0F};
  float fresnel_exponent{5.0F};
  float specular_tint{};
  float opacity_facing{1.0F};
  float opacity_grazing{1.0F};
  float opacity_fresnel_exponent{1.0F};
  float reflectance_base_luminance{};
  std::string_view extra_switch{};
};
cc::MaterialAssetData lit_asset(const LitSpec& s) {
  cc::MaterialAssetData a;
  a.template_name = std::string(m::kLitTemplate);
  a.switches = {{std::string(m::kBaseColorMapSwitch), s.base_color_map},
                {std::string(m::kNormalMapSwitch), s.normal_map},
                {std::string(m::kOcclusionReflectanceMapSwitch), s.occlusion_reflectance_map}};
  if (!s.extra_switch.empty()) {
    a.switches.push_back({std::string(s.extra_switch), true});
  }
  a.scalars = {{std::string(m::kBlendModeParameter), static_cast<float>(static_cast<int>(s.blend))},
               {std::string(m::kRoughness), s.roughness},
               {std::string(m::kReflectance), s.reflectance},
               {std::string(m::kGrazingReflectance), s.grazing_reflectance},
               {std::string(m::kFresnelExponent), s.fresnel_exponent},
               {std::string(m::kSpecularTint), s.specular_tint},
               {std::string(m::kReflectanceBaseLuminance), s.reflectance_base_luminance},
               {std::string(m::kOpacityFacing), s.opacity_facing},
               {std::string(m::kOpacityGrazing), s.opacity_grazing},
               {std::string(m::kOpacityFresnelExponent), s.opacity_fresnel_exponent}};
  a.vectors = {{std::string(m::kBaseColorTint), s.tint}, {std::string(m::kEmissive), s.emissive}};
  if (s.base_color_map) {
    a.textures.push_back({std::string(m::kBaseColorTexture), std::string(kTextureId)});
  }
  if (s.normal_map) {
    a.textures.push_back({std::string(m::kNormalTexture), std::string(kNormalTextureId)});
  }
  if (s.occlusion_reflectance_map) {
    a.textures.push_back(
        {std::string(m::kOcclusionReflectanceTexture), std::string(kPackedTextureId)});
  }
  return a;
}

template <class T> void append(std::vector<std::byte>& out, const T& value) {
  const auto* bytes = reinterpret_cast<const std::byte*>(&value);
  out.insert(out.end(), bytes, bytes + sizeof(T));
}

// The cell's rectangle in clip space; with an identity world_to_clip the
// world position is the clip position at z 0.5.
std::array<float, 4> cell_rect(std::uint32_t cell) {
  const std::uint32_t column = cell % kColumns;
  const std::uint32_t row = cell / kColumns;
  const float x0 = -1.0F + (2.0F * static_cast<float>(column) / kColumns);
  const float y1 = 1.0F - (2.0F * static_cast<float>(row) / kRows);
  return {x0, y1 - (2.0F / kRows), x0 + (2.0F / kColumns), y1};
}

std::int16_t snorm(float value) { return static_cast<std::int16_t>(value * 32767.0F); }

cc::MeshAssetData mesh_of(const SyntheticModel& model) {
  using geometry::VertexAttribute;
  using geometry::VertexElementType;
  cc::MeshGroupData group;
  geometry::VertexStreamLayout& layout = group.layout;
  std::uint32_t offset = 0;
  const auto add = [&](VertexAttribute attribute, VertexElementType type, std::uint32_t size) {
    layout.attributes[layout.attribute_count++] = {attribute, type, 0, offset};
    offset += size;
  };
  add(VertexAttribute::Position, VertexElementType::Float3, 12);
  add(VertexAttribute::Normal, VertexElementType::Short4Norm, 8);
  if (model.tangent) {
    add(VertexAttribute::Tangent, VertexElementType::Short4Norm, 8);
  }
  add(VertexAttribute::TexCoord0, VertexElementType::Half2, 4);
  layout.streams[0].stride = offset;
  layout.stream_count = 1;
  cc::MeshAssetData mesh;
  for (std::uint32_t q = 0; q < model.quads.size(); ++q) {
    const Quad& quad = model.quads[q];
    const auto [x0, y0, x1, y1] = cell_rect(quad.cell);
    const float dx = quad.shift * 2.0F / kColumns;
    const std::uint16_t left = quad.mirrored ? 0x3C00 : 0x0000;
    const std::uint16_t right = quad.mirrored ? 0x0000 : 0x3C00;
    const std::int16_t sign = quad.mirrored ? -32767 : 32767;
    std::array corners{std::tuple{x0, y0, left}, std::tuple{x1, y0, right},
                       std::tuple{x1, y1, right}, std::tuple{x0, y1, left}};
    if (quad.reversed) {
      std::swap(corners[1], corners[3]);
    }
    for (const auto& [x, y, u] : corners) {
      append(group.stream0, std::array{x + dx, y, quad.z});
      append(group.stream0,
             std::array<std::int16_t, 4>{snorm(quad.normal[0]), 0, snorm(quad.normal[1]), 0});
      if (model.tangent) {
        append(group.stream0,
               std::array<std::int16_t, 4>{sign, 0, 0, static_cast<std::int16_t>(-sign)});
      }
      append(group.stream0, std::array<std::uint16_t, 2>{u, 0x3800});
    }
    mesh.draws.push_back(geometry::MeshDrawRecord{.base_vertex = 4 * q,
                                                  .vertex_count = 4,
                                                  .index_count = 6,
                                                  .material = quad.material,
                                                  .bone_map = geometry::kNoBoneMap});
  }
  group.vertex_count = 4 * static_cast<std::uint32_t>(model.quads.size());
  group.indices = {0, 1, 2, 0, 2, 3};
  group.draw_count = static_cast<std::uint32_t>(model.quads.size());
  mesh.groups.push_back(std::move(group));
  mesh.material_count = static_cast<std::uint32_t>(model.materials.size());
  mesh.bounds_max = {1.4F, 1.0F, 0.75F};
  mesh.bounds_min = {-1.0F, -1.0F, 0.25F};
  mesh.center = {0.0F, 0.0F, 0.5F};
  mesh.radius = 1.5F;
  return mesh;
}

void write_asset(const fs::path& root, content::AssetKind kind, std::string_view id,
                 const std::vector<std::byte>& payload, const cc::EncodeFailure& failure) {
  require(failure.ok() && !payload.empty(), "the synthetic asset encodes: " + std::string(id));
  const content::AssetId asset = content::make_asset_id(id).id;
  const std::vector<std::byte> file = cc::encode_cooked_asset(kind, asset, {}, payload);
  const fs::path path = root / content::asset_relative_path(asset);
  fs::create_directories(path.parent_path());
  std::ofstream out(path, std::ios::binary);
  out.write(reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size()));
  require(out.good(), "written: " + path.string());
}

// A 2 by 1 half-float texture of the two texels with its 1 by 1 mip.
void write_texture(const fs::path& root, std::string_view id,
                   const std::array<std::uint16_t, 8>& texels,
                   const std::array<std::uint16_t, 4>& mip) {
  cc::TextureAssetData texture{.width = 2,
                               .height = 1,
                               .mip_levels = 2,
                               .format = rendering::TextureFormat::R16G16B16A16Float};
  texture.subresources.push_back({16, {}});
  append(texture.subresources[0].bytes, texels);
  texture.subresources.push_back({8, {}});
  append(texture.subresources[1].bytes, mip);
  cc::EncodeFailure failure;
  write_asset(root, content::AssetKind::Texture, id, cc::encode_texture_asset(texture, failure),
              failure);
}

std::array<std::uint16_t, 8> both(const std::array<std::uint16_t, 4>& texel) {
  return {texel[0], texel[1], texel[2], texel[3], texel[0], texel[1], texel[2], texel[3]};
}

// The model's converted layer under `root`, loaded with
// model_texture_parameters().
void load_synthetic(rhi::Device& device, const SyntheticModel& model, const fs::path& root,
                    rendering::ModelResources& out) {
  fs::remove_all(root);
  fs::create_directories(root);
  cc::EncodeFailure failure;
  write_asset(root, content::AssetKind::Mesh, kMeshId,
              cc::encode_mesh_asset(mesh_of(model), failure), failure);
  for (std::uint32_t i = 0; i < model.materials.size(); ++i) {
    write_asset(root, content::AssetKind::Material,
                std::string(kMeshId) + ".material." + std::to_string(i),
                cc::encode_material_asset(model.materials[i], failure), failure);
  }
  write_texture(root, kTextureId, kTexels, kMip1);
  write_texture(root, kNormalTextureId, both(kNormalTexel), kNormalTexel);
  write_texture(root, kPackedTextureId, both(kPackedTexel), kPackedTexel);
  const std::string record =
      content::encode_converted_cache_record({content::kCookedFormatVersion, "synthetic", 0});
  std::ofstream(root / content::kConvertedCacheRecordFile) << record;
  const content::OpenLayersResult opened = content::open_layers(std::nullopt, std::nullopt, root);
  require(opened.ok(), "the synthetic layer opens: " + opened.detail);
  const content::Loader loader(opened.layers);
  const rendering::ModelLoadFailure loaded =
      rendering::load_model(device, loader, content::make_asset_id(kMeshId).id,
                            rendering::model_texture_parameters(), out);
  require(loaded.ok(), "the synthetic model loads: " + loaded.id + ": " + loaded.detail);
}

// ---- drawing and reading back -------------------------------------------------

std::vector<float> to_floats(std::span<const std::byte> halves) {
  std::vector<float> out;
  for (std::size_t at = 0; at + 1 < halves.size(); at += 2) {
    const auto h =
        static_cast<std::uint32_t>(std::to_integer<std::uint32_t>(halves[at]) |
                                   (std::to_integer<std::uint32_t>(halves[at + 1]) << 8U));
    const std::uint32_t exponent = (h >> 10U) & 0x1FU;
    const float mantissa = static_cast<float>(h & 0x3FFU);
    const float magnitude = exponent == 0U
                                ? std::ldexp(mantissa, -24)
                                : std::ldexp(1024.0F + mantissa, static_cast<int>(exponent) - 25);
    out.push_back((h & 0x8000U) != 0U ? -magnitude : magnitude);
  }
  return out;
}

// One frame of the model into a cleared target of `format`; the target's
// texels as read back.
struct Frame {
  rhi::StagingLayout layout{};
  std::vector<std::byte> bytes{};
};
Frame draw_frame(rhi::Device& device, const rendering::ModelResources& model,
                 const rendering::ModelDraw& draw, rhi::Format format, rhi::Extent2 extent,
                 const bw::scene::View& view, const bw::scene::FixtureLight& light,
                 const bw::math::Matrix4f& object_to_world) {
  rhi::TextureDescription target_description{};
  target_description.extent = rhi::Extent3{extent.width, extent.height, 1};
  target_description.format = format;
  target_description.usage = rhi::TextureUsage::RenderTarget | rhi::TextureUsage::CopySource;
  target_description.initial_access = rhi::Access::RenderTarget;
  target_description.clear_value.color = kClear;
  target_description.debug_name = "model draw target";
  const rhi::Ref<rhi::Texture> target =
      take(device.create_texture(target_description), "create the target");
  rhi::Ref<rhi::Texture> depth;
  require(rendering::resize_depth_target(device, extent, rendering::kModelDepthFormat,
                                         "model draw depth", depth)
              .ok(),
          "create the depth target");
  rendering::SceneBlocks blocks;
  require(rendering::update_scene_blocks(device, view, light, blocks).ok(), "the scene blocks");

  rhi::CommandList list;
  rendering::begin_cleared_pass(list, *target, depth.get(), extent, "model draw");
  rendering::bind_scene_blocks(list, blocks);
  rendering::record_model_draw(list, draw, model, object_to_world);
  list.end_render_pass();

  Frame frame{rhi::staging_layout(target->description(), 0), {}};
  const rhi::Ref<rhi::StagingBuffer> staging =
      take(device.create_staging_buffer(frame.layout.total_bytes, "model draw readback"),
           "create_staging_buffer");
  const rhi::Ref<rhi::GpuFence> fence = take(device.create_fence("model draw"), "create_fence");
  const rhi::Transition to_copy{target.get(), rhi::Access::RenderTarget, rhi::Access::CopySource};
  list.transition(rhi::TransitionBatch{std::span<const rhi::Transition>{&to_copy, 1}});
  list.copy_texture_to_staging(*target, 0, 0, *staging, 0);
  list.write_fence(*fence);
  require(device.execute(list).ok(), "execute succeeds");
  const rhi::EndFrameResult ended = device.end_frame();
  const rhi::StagingLock lock =
      take(device.lock_staging(*staging, *fence, rhi::WaitPolicy::Block), "lock_staging");
  require(!ended.gpu_fault, "the GPU reports no fault");
  frame.bytes.assign(lock.bytes.begin(), lock.bytes.end());
  require(device.unlock_staging(*staging) == rhi::StagingError::None, "unlock_staging");
  return frame;
}

const bw::math::Matrix4f kIdentity = bw::math::Matrix4f::translation(bw::math::Vector3f{0, 0, 0});

// ---- the lit template and shading model as their headers state them ------------

using V3 = std::array<float, 3>;
float dot(const V3& a, const V3& b) { return (a[0] * b[0]) + (a[1] * b[1]) + (a[2] * b[2]); }
V3 cross(const V3& a, const V3& b) {
  return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]),
          (a[0] * b[1]) - (a[1] * b[0])};
}
V3 normalized(const V3& v) {
  const float length = std::sqrt(dot(v, v));
  return {v[0] / length, v[1] / length, v[2] / length};
}
float saturate(float v) { return std::clamp(v, 0.0F, 1.0F); }
// A vertex normal as the stream stores it, decoded and made unit length.
V3 stored_normal(const std::array<float, 2>& xz) {
  return normalized({static_cast<float>(snorm(xz[0])) / 32767.0F, 0.0F,
                     static_cast<float>(snorm(xz[1])) / 32767.0F});
}

struct Attributes {
  V3 diffuse_albedo{};
  V3 f0{};
  V3 f90{};
  float roughness{};
  V3 normal{};
  V3 emissive{};
  float ambient_occlusion{1.0F};
  float opacity{1.0F};
};

// lit_template.hpp, contract 5, for a quad's pixel seen along v: the base
// texture's colour where the material samples it, the normal map's texel
// in the quad's tangent frame, the packed texture's two channels.
Attributes template_attributes(const LitSpec& s, const Quad& quad, const V3& v) {
  std::array<float, 4> base = s.tint;
  if (s.base_color_map) {
    base = {base[0] * 0.5F, base[1] * 0.25F, base[2], base[3]};
  }
  const V3 rgb{base[0], base[1], base[2]};
  Attributes a;
  a.diffuse_albedo = rgb;
  const V3 n = stored_normal(quad.normal);
  a.normal = n;
  if (s.normal_map) {
    const V3 t{quad.mirrored ? -1.0F : 1.0F, 0.0F, 0.0F};
    const float w = quad.mirrored ? 1.0F : -1.0F;
    const V3 b = cross(n, t);
    constexpr float tx = 0.5F;
    constexpr float ty = 0.25F;
    const float tz = std::sqrt(saturate(1.0F - (tx * tx) - (ty * ty)));
    a.normal = normalized({(tx * t[0]) + (ty * w * b[0]) + (tz * n[0]),
                           (tx * t[1]) + (ty * w * b[1]) + (tz * n[1]),
                           (tx * t[2]) + (ty * w * b[2]) + (tz * n[2])});
  }
  const float n_v = saturate(dot(a.normal, v));
  const auto curve = [n_v](float facing, float grazing, float e) {
    return facing + ((grazing - facing) * std::pow(1.0F - n_v, e));
  };
  float mask =
      1.0F + ((dot(rgb, {0.2126F, 0.7152F, 0.0722F}) - 1.0F) * s.reflectance_base_luminance);
  if (s.occlusion_reflectance_map) {
    a.ambient_occlusion = 0.5F;
    mask *= 0.25F;
  }
  const float r = saturate(curve(s.reflectance, s.grazing_reflectance, s.fresnel_exponent) * mask);
  const float top = std::max({rgb[0], rgb[1], rgb[2], 1e-4F});
  for (std::size_t c = 0; c < 3; ++c) {
    a.f0[c] = r * (1.0F + (((rgb[c] / top) - 1.0F) * s.specular_tint));
    a.emissive[c] = s.emissive[c] * rgb[c];
  }
  a.f90 = a.f0;
  a.roughness = s.roughness;
  a.opacity =
      saturate(base[3] * curve(s.opacity_facing, s.opacity_grazing, s.opacity_fresnel_exponent));
  return a;
}

// forward_pass.hpp, contract 3.
V3 shade(const Attributes& a, const V3& v, const bw::scene::FixtureLight& light) {
  constexpr float pi = 3.14159265358979F;
  const V3 n = a.normal;
  const V3 l{light.direction.x, light.direction.y, light.direction.z};
  const V3 h = normalized({v[0] + l[0], v[1] + l[1], v[2] + l[2]});
  const float n_l = saturate(dot(n, l));
  const float n_h = saturate(dot(n, h));
  const float v_h = saturate(dot(v, h));
  const float n_v = std::abs(dot(n, v)) + 1e-5F;
  const float r = std::clamp(a.roughness, bw::scene::kMinimumRoughness, 1.0F);
  const float a2 = r * r * r * r;
  const float d_term = (n_h * n_h * (a2 - 1.0F)) + 1.0F;
  const float d = a2 / (pi * d_term * d_term);
  const float vis = 0.5F / ((n_l * std::sqrt((n_v * n_v * (1.0F - a2)) + a2)) +
                            (n_v * std::sqrt((n_l * n_l * (1.0F - a2)) + a2)));
  const float fade = saturate(bw::scene::kGrazingFadeScale * std::max({a.f0[0], a.f0[1], a.f0[2]}));
  const std::array<float, 4> q{(r * -1.0F) + 1.0F, (r * -0.0275F) + 0.0425F, (r * -0.572F) + 1.04F,
                               (r * 0.022F) - 0.04F};
  const float t = (std::min(q[0] * q[0], std::exp2(-9.28F * n_v)) * q[0]) + q[1];
  const float ab_x = (-1.04F * t) + q[2];
  const float ab_y = (1.04F * t) + q[3];
  V3 f90{};
  V3 e{};
  for (std::size_t c = 0; c < 3; ++c) {
    f90[c] = a.f90[c] * fade;
    e[c] = (a.f0[c] * ab_x) + (f90[c] * ab_y);
  }
  const float kept = 1.0F - dot(e, {0.2126F, 0.7152F, 0.0722F});
  const std::array<float, 3> color{light.color.x, light.color.y, light.color.z};
  const std::array<float, 3> ambient{light.ambient.x, light.ambient.y, light.ambient.z};
  V3 out{};
  for (std::size_t c = 0; c < 3; ++c) {
    const float f = a.f0[c] + ((f90[c] - a.f0[c]) * std::pow(1.0F - v_h, 5.0F));
    const float diffuse = a.diffuse_albedo[c] * kept;
    out[c] = ((diffuse / pi) + (d * vis * f)) * color[c] * n_l +
             (ambient[c] * a.diffuse_albedo[c] * a.ambient_occlusion) + a.emissive[c];
  }
  return out;
}

// The texel of `frame` at a column of a cell's middle row, and the world
// position of its centre.
struct Texel {
  V3 color{};
  V3 world{};
};
Texel texel_at(const Frame& frame, std::uint32_t cell, std::uint32_t column) {
  const std::uint32_t x = ((cell % kColumns) * kCellPixels) + column;
  const std::uint32_t y = ((cell / kColumns) * kCellPixels) + (kCellPixels / 2U);
  const std::vector<float> texel = to_floats(
      std::span(frame.bytes)
          .subspan((static_cast<std::size_t>(y) * frame.layout.row_pitch) + (std::size_t{x} * 8U),
                   8));
  return {{texel[0], texel[1], texel[2]},
          {((static_cast<float>(x) + 0.5F) / static_cast<float>(kTarget.width) * 2.0F) - 1.0F,
           1.0F - ((static_cast<float>(y) + 0.5F) / static_cast<float>(kTarget.height) * 2.0F),
           0.5F}};
}

// (forward_pass.hpp, contracts 2 and 3; lit_template.hpp, contract 5;
// model_draw.hpp, contracts 2 and 3): one quad per row in a 5 by 5 grid,
// drawn through an identity world_to_clip into a half-float target, texels
// of each cell against the stated attributes of its material and the stated
// shading of them: a metal in reflectance terms under the light (every term
// of D, Vis and F), a quad whose normal faces away
// (ambient and emissive alone), the base color texture times the tint, the
// masked quad (its pixel below the threshold stays the clear color, its
// pixel above is drawn), the translucent blend over the clear color, a
// normal map in an unmirrored and a mirrored quad (the frame's sign), the
// occlusion and reflectance texture (first channel on ambient, second on
// reflectance), the reflectance curve at a tilted view angle, a reflection
// tinted toward the base hue, the opacity curve, a reflectance under 2
// percent seen at a grazing half vector in a second frame whose light is
// the view mirrored about the quad's normal (the grazing fade), the
// additive blend over the clear color, the depth test and write (a far
// opaque quad drawn after a near one is hidden; an opaque quad drawn after a
// nearer translucent one shows), a quad wound the other way (no cull), and
// a dark base masking its reflectance by its luminance. The tinted
// reflection's quad is authored one cell to the right and read in a third
// frame whose object_to_world, a 90-degree turn about the view axis then a
// translation, brings it back (the vertex stage's row-vector transform).
// Each reads its LitMaterial through the generated loader on the GPU.
void forward_lit_pixels(const Arguments& args) {
  using B = m::BlendMode;
  // Vertex normals tilted toward +x, so that n.v is about 0.7 and 0.2.
  const std::array<float, 2> tilt_07{0.714F, 0.7F};
  const std::array<float, 2> tilt_02{0.9798F, 0.2F};
  // One material per cell, then the materials of the depth cells' second
  // quads.
  const std::vector<LitSpec> specs{
      {.tint = {0.9F, 0.6F, 0.3F, 1.0F},
       .roughness = 0.3F,
       .reflectance = 0.9F,
       .grazing_reflectance = 0.9F,
       .specular_tint = 1.0F},
      {.tint = {0.4F, 0.4F, 0.4F, 1.0F}, .emissive = {0.5F, 0.25F, 0.125F, 0.0F}},
      {.base_color_map = true, .tint = {0.5F, 1.0F, 1.0F, 1.0F}},
      {.blend = B::Masked, .base_color_map = true},
      {.blend = B::Translucent, .tint = {1.0F, 0.0F, 0.0F, 0.5F}},
      {.normal_map = true, .tint = {0.7F, 0.7F, 0.7F, 1.0F}, .roughness = 0.5F},
      {.normal_map = true, .tint = {0.7F, 0.7F, 0.7F, 1.0F}, .roughness = 0.5F},
      {.base_color_map = true,
       .occlusion_reflectance_map = true,
       .roughness = 0.4F,
       .reflectance = 0.5F,
       .grazing_reflectance = 0.5F},
      {.tint = {0.6F, 0.6F, 0.6F, 1.0F},
       .roughness = 0.5F,
       .reflectance = 0.05F,
       .grazing_reflectance = 0.8F,
       .fresnel_exponent = 2.0F},
      {.tint = {0.2F, 0.6F, 0.9F, 1.0F},
       .roughness = 0.4F,
       .reflectance = 0.3F,
       .grazing_reflectance = 0.3F,
       .specular_tint = 0.5F},
      {.blend = B::Translucent,
       .tint = {0.3F, 0.8F, 0.5F, 1.0F},
       .opacity_facing = 0.2F,
       .opacity_grazing = 0.6F,
       .opacity_fresnel_exponent = 2.0F},
      {.roughness = 0.4F, .reflectance = 0.01F, .grazing_reflectance = 0.01F},
      {.blend = B::Additive, .tint = {0.3F, 0.2F, 0.1F, 0.5F}},
      {.tint = {0.5F, 0.5F, 0.5F, 1.0F}, .roughness = 0.5F},
      {.blend = B::Translucent, .tint = {0.0F, 0.0F, 1.0F, 0.5F}},
      {.tint = {0.8F, 0.4F, 0.2F, 1.0F}},
      {.tint = {0.05F, 0.05F, 0.8F, 1.0F},
       .roughness = 0.5F,
       .reflectance = 1.0F,
       .grazing_reflectance = 1.0F,
       .reflectance_base_luminance = 1.0F},
      {.tint = {1.0F, 1.0F, 1.0F, 1.0F}, .emissive = {1.0F, 0.0F, 1.0F, 0.0F}},
      {.tint = {0.9F, 0.9F, 0.2F, 1.0F}, .roughness = 0.6F},
  };
  constexpr std::uint32_t kCells = 17;
  constexpr std::uint32_t kMovedCell = 9;
  constexpr std::uint32_t kGrazingCell = 11;
  constexpr std::uint32_t kHiddenCell = 13;
  constexpr std::uint32_t kShownCell = 14;
  constexpr std::uint32_t kReversedCell = 15;
  SyntheticModel model;
  for (std::uint32_t i = 0; i < specs.size(); ++i) {
    model.materials.push_back(lit_asset(specs[i]));
  }
  for (std::uint32_t i = 0; i < kCells; ++i) {
    model.quads.push_back({i, {0.0F, 1.0F}, i, false});
  }
  model.quads[1].normal = {0.0F, -1.0F};
  model.quads[6].mirrored = true;
  model.quads[8].normal = tilt_07;
  model.quads[10].normal = tilt_07;
  model.quads[11].normal = tilt_02;
  model.quads[kMovedCell].shift = 1.0F;
  model.quads[kShownCell].z = 0.25F;
  model.quads[kReversedCell].reversed = true;
  // Drawn after every cell's quad: behind the near opaque quad of the hidden
  // cell, and an opaque quad behind the shown cell's translucent one.
  model.quads.push_back({.cell = kHiddenCell, .material = kCells, .z = 0.75F});
  model.quads.push_back({.cell = kShownCell, .material = kCells + 1});
  const std::unique_ptr<rhi::Device> device = metal_device();
  rendering::ModelResources resources;
  load_synthetic(*device, model, args.scratch / "forward_lit_pixels", resources);
  m::MaterialBuffer buffer;
  require(m::create_material_buffer(
              *device, static_cast<std::uint32_t>(specs.size()) * m::kMaxMaterialBlockBytes,
              "model draw", buffer)
              .ok(),
          "the material buffer");
  rendering::ModelDraw draw;
  const rendering::ModelDrawFailure made = rendering::create_model_draw(
      *device, resources, args.shaders, buffer, rhi::Format::R16G16B16A16Float, draw);
  require(made.ok(), "the draw is made: " + made.detail);

  const V3 eye{0.0F, 0.0F, 10.0F};
  const bw::scene::View view =
      bw::scene::make_view(kIdentity, {eye[0], eye[1], eye[2]}, static_cast<float>(kTarget.width),
                           static_cast<float>(kTarget.height));
  bw::scene::FixtureLight light{};
  const V3 l = normalized({0.3F, -0.4F, 0.8F});
  light.direction = {l[0], l[1], l[2]};
  light.color = {2.0F, 2.0F, 2.0F};
  light.ambient = {0.1F, 0.2F, 0.3F};
  const Frame frame = draw_frame(*device, resources, draw, rhi::Format::R16G16B16A16Float, kTarget,
                                 view, light, kIdentity);
  // The second frame's light: the view at the grazing cell's centre
  // mirrored about its normal.
  bw::scene::FixtureLight grazing_light = light;
  {
    const auto [x0, y0, x1, y1] = cell_rect(kGrazingCell);
    const V3 v =
        normalized({eye[0] - ((x0 + x1) / 2.0F), eye[1] - ((y0 + y1) / 2.0F), eye[2] - 0.5F});
    const V3 n = stored_normal(tilt_02);
    const float twice = 2.0F * dot(n, v);
    const V3 mirrored =
        normalized({(twice * n[0]) - v[0], (twice * n[1]) - v[1], (twice * n[2]) - v[2]});
    grazing_light.direction = {mirrored[0], mirrored[1], mirrored[2]};
    grazing_light.color = {4.0F, 4.0F, 4.0F};
  }
  const Frame grazing_frame = draw_frame(*device, resources, draw, rhi::Format::R16G16B16A16Float,
                                         kTarget, view, grazing_light, kIdentity);
  // The third frame's object_to_world: (x, y) to (-y, x), then the
  // translation that takes the moved quad's authored centre to its cell's.
  bw::math::Matrix4f moved = kIdentity;
  {
    const auto [x0, y0, x1, y1] = cell_rect(kMovedCell);
    const float cx = (x0 + x1) / 2.0F;
    const float cy = (y0 + y1) / 2.0F;
    const float authored_x = cx + (2.0F / kColumns);
    const bw::math::Matrix4f turn(bw::math::Matrix4f::Rows{{{0.0F, 1.0F, 0.0F, 0.0F},
                                                            {-1.0F, 0.0F, 0.0F, 0.0F},
                                                            {0.0F, 0.0F, 1.0F, 0.0F},
                                                            {0.0F, 0.0F, 0.0F, 1.0F}}});
    moved =
        turn * bw::math::Matrix4f::translation(bw::math::Vector3f{cx + cy, cy - authored_x, 0.0F});
  }
  const Frame moved_frame = draw_frame(*device, resources, draw, rhi::Format::R16G16B16A16Float,
                                       kTarget, view, light, moved);

  // Per cell, one or two texels: the column in the cell, drawn or left the
  // clear color; the quad whose shading is expected (the cell's own unless
  // named).
  struct Probe {
    std::uint32_t column{};
    bool drawn{true};
  };
  struct Row {
    std::string_view name;
    std::vector<Probe> probes;
    std::optional<std::uint32_t> quad{};
  };
  constexpr std::uint32_t kCenter = kCellPixels / 2U;
  const std::array<Row, kCells> rows{{
      {"metal", {{kCenter}}},
      {"light behind", {{kCenter}}},
      {"texture times tint", {{kCenter}}},
      {"masked: discarded below the threshold, drawn above", {{0, false}, {kCellPixels - 1U}}},
      {"translucent over", {{kCenter}}},
      {"normal map, unmirrored", {{kCenter}}},
      {"normal map, mirrored", {{kCenter}}},
      {"occlusion and reflectance texture", {{kCenter}}},
      {"reflectance curve, n.v about 0.7", {{kCenter}}},
      {"tinted reflection, authored one cell over and moved back", {{kCenter}}},
      {"opacity curve", {{kCenter}}},
      {"reflectance under 2 percent at a grazing half vector", {{kCenter}}},
      {"additive over", {{kCenter}}},
      {"depth: a far opaque quad drawn after a near one is hidden", {{kCenter}}},
      {"depth: an opaque quad drawn after a nearer translucent one shows", {{kCenter}}, kCells + 1},
      {"back-facing quad drawn", {{kCenter}}},
      {"reflectance masked by base luminance", {{kCenter}}},
  }};
  for (std::uint32_t cell = 0; cell < rows.size(); ++cell) {
    const bool grazing = cell == kGrazingCell;
    const Frame& read = grazing ? grazing_frame : cell == kMovedCell ? moved_frame : frame;
    const Quad& quad = model.quads[rows[cell].quad.value_or(cell)];
    const LitSpec& spec = specs[quad.material];
    for (const Probe& probe : rows[cell].probes) {
      const Texel texel = texel_at(read, cell, probe.column);
      const V3 v =
          normalized({eye[0] - texel.world[0], eye[1] - texel.world[1], eye[2] - texel.world[2]});
      V3 expected{kClear[0], kClear[1], kClear[2]};
      if (probe.drawn) {
        const Attributes a = template_attributes(spec, quad, v);
        const V3 lit = shade(a, v, grazing ? grazing_light : light);
        const bool blended = spec.blend == B::Translucent || spec.blend == B::Additive;
        const float opacity = blended ? a.opacity : 1.0F;
        const float under = spec.blend == B::Additive ? 1.0F : 1.0F - opacity;
        for (std::size_t c = 0; c < 3; ++c) {
          expected[c] = (lit[c] * opacity) + (kClear[c] * under);
        }
      }
      for (std::size_t c = 0; c < 3; ++c) {
        require(std::abs(texel.color[c] - expected[c]) <= 0.004F + (0.004F * std::abs(expected[c])),
                cell,
                std::string(rows[cell].name) + ": column " + std::to_string(probe.column) +
                    " channel " + std::to_string(c) + " is " + std::to_string(texel.color[c]) +
                    ", the stated shading gives " + std::to_string(expected[c]));
      }
    }
  }
}

// (model_draw.hpp, contract 2): one row per error name the content and the
// caller can cause, each a two-quad model with two materials that creates
// but for one change to the second draw or material, whose index is named;
// MaterialFailed carries create_material's error.
void failures_named(const Arguments& args) {
  using E = rendering::ModelDrawError;
  struct Row {
    std::string_view note;
    std::function<void(SyntheticModel&, std::uint32_t&, fs::path&)> change;
    E error;
    std::uint32_t index;
    // The name the failure carries; nullopt where the header promises a name
    // and not its spelling.
    std::optional<std::string_view> name;
  };
  const std::array<Row, 5> rows{{
      {"a draw with no material",
       [](SyntheticModel& s, std::uint32_t&, fs::path&) {
         s.quads[1].material = geometry::kNoMaterial;
       },
       E::DrawWithoutMaterial, 1, ""},
      {"an environment map switch",
       [](SyntheticModel& s, std::uint32_t&, fs::path&) {
         s.materials[1] = lit_asset({.extra_switch = "environment_map"});
       },
       E::MaterialFailed, 1, "environment_map"},
      {"a buffer with room for one block",
       [](SyntheticModel&, std::uint32_t& capacity, fs::path&) { capacity = m::kLitMaterialBytes; },
       E::MaterialBufferFailed, 1, ""},
      {"a normal map on a group without tangents",
       [](SyntheticModel& s, std::uint32_t&, fs::path&) {
         s.materials[1] = lit_asset({.normal_map = true});
         s.tangent = false;
       },
       E::VertexAttributeMissing, 1, std::nullopt},
      {"no artifacts in the shader directory",
       [](SyntheticModel&, std::uint32_t&, fs::path& shaders) { shaders /= "absent"; },
       E::ArtifactFailed, 0, ""},
  }};
  const std::unique_ptr<rhi::Device> device = metal_device();
  for (std::size_t i = 0; i < rows.size(); ++i) {
    SyntheticModel model;
    model.materials = {lit_asset({}), lit_asset({})};
    model.quads = {{.cell = 0, .material = 0}, {.cell = 1, .material = 1}};
    std::uint32_t capacity = 2 * m::kMaxMaterialBlockBytes;
    fs::path shaders = args.shaders;
    rows[i].change(model, capacity, shaders);
    rendering::ModelResources resources;
    load_synthetic(*device, model, args.scratch / ("failures_named_" + std::to_string(i)),
                   resources);
    m::MaterialBuffer buffer;
    require(m::create_material_buffer(*device, capacity, "model draw", buffer).ok(), i,
            "the material buffer");
    rendering::ModelDraw draw;
    const rendering::ModelDrawFailure failure = rendering::create_model_draw(
        *device, resources, shaders, buffer, rhi::Format::R16G16B16A16Float, draw);
    const std::optional<std::string_view> name = rows[i].name;
    const bool named = name.has_value() ? failure.name == *name : !failure.name.empty();
    const bool material_error = rows[i].error != E::MaterialFailed ||
                                failure.material_error == m::MaterialError::SwitchUnknown;
    require(failure.error == rows[i].error && failure.index == rows[i].index && named &&
                material_error && draw.pipelines.empty() && draw.instances.empty(),
            i, rows[i].note);
  }
}

// (model_draw.hpp, contracts 1 to 4) over PW0 from the pw0_cache fixture:
// its materials create, its draws each have a pipeline, and the frame
// executes with no GPU fault and covers the target; update_scene_blocks
// keeps a buffer whose bytes are unchanged and remakes one whose bytes
// changed.
void pw0_draws_with_materials(const Arguments& args) {
  const content::OpenLayersResult opened =
      content::open_layers(std::nullopt, std::nullopt, args.pw0_cache);
  require(opened.ok(), "the converted layer opens: " + opened.detail);
  const content::Loader loader(opened.layers);
  const std::unique_ptr<rhi::Device> device = metal_device();
  rendering::ModelResources model;
  const rendering::ModelLoadFailure loaded =
      rendering::load_model(*device, loader, content::make_asset_id("model.pw0.mesh").id,
                            rendering::model_texture_parameters(), model);
  require(loaded.ok(), "model.pw0.mesh loads: " + loaded.id + ": " + loaded.detail);
  m::MaterialBuffer buffer;
  require(m::create_material_buffer(*device, 16 * m::kMaxMaterialBlockBytes, "pw0", buffer).ok(),
          "the material buffer");
  constexpr rhi::Format kColor = rhi::Format::R8G8B8A8Unorm;
  rendering::ModelDraw draw;
  const rendering::ModelDrawFailure made =
      rendering::create_model_draw(*device, model, args.shaders, buffer, kColor, draw);
  require(made.ok(), "PW0's draw is made: index " + std::to_string(made.index) + " " + made.name +
                         ": " + made.detail);
  require(draw.instances.size() == model.materials.size() &&
              draw.draw_pipelines.size() == model.mesh.draws.size(),
          "an instance per material and a pipeline per draw");

  constexpr rhi::Extent2 kExtent{64, 64};
  const std::array<float, 3>& c = model.mesh.center;
  const std::optional<bw::scene::PerspectiveView> view =
      bw::scene::frame_sphere(bw::math::Vector3f{c[0], c[1], c[2]}, model.mesh.radius, 1.0F);
  if (!view.has_value()) {
    throw std::runtime_error("the bounding sphere cannot be framed");
  }
  const bw::scene::View scene_view =
      bw::scene::make_view(bw::scene::world_to_clip(*view, 1.0F), view->eye, 64.0F, 64.0F);
  const Frame frame = draw_frame(*device, model, draw, kColor, kExtent, scene_view,
                                 bw::scene::fixture_light(), kIdentity);

  rendering::SceneBlocks blocks;
  require(
      rendering::update_scene_blocks(*device, scene_view, bw::scene::fixture_light(), blocks).ok(),
      "the scene blocks");
  const rhi::Buffer* view_buffer = blocks.view.get();
  const rhi::Buffer* light_buffer = blocks.fixture_light.get();
  require(rendering::update_scene_blocks(*device, scene_view, bw::scene::fixture_light(), blocks)
                  .ok() &&
              blocks.view.get() == view_buffer && blocks.fixture_light.get() == light_buffer,
          "equal blocks keep both buffers");
  bw::scene::View moved_eye = scene_view;
  moved_eye.eye_position.x += 1.0F;
  require(
      rendering::update_scene_blocks(*device, moved_eye, bw::scene::fixture_light(), blocks).ok() &&
          blocks.view.get() != view_buffer && blocks.fixture_light.get() == light_buffer,
      "a moved eye remakes the view buffer alone");
  std::size_t covered = 0;
  for (std::uint32_t y = 0; y < kExtent.height; ++y) {
    for (std::uint32_t x = 0; x < kExtent.width; ++x) {
      const std::size_t at =
          (static_cast<std::size_t>(y) * frame.layout.row_pitch) + (std::size_t{x} * 4U);
      bool differs = false;
      for (std::size_t ch = 0; ch < 3; ++ch) {
        const int expected = static_cast<int>(std::lround(kClear[ch] * 255.0F));
        differs = differs || std::abs(std::to_integer<int>(frame.bytes[at + ch]) - expected) > 1;
      }
      covered += differs ? 1U : 0U;
    }
  }
  require(covered >= 128U, std::to_string(covered) + " texels differ from the clear color");
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"forward_lit_pixels", &forward_lit_pixels},
    Scenario{"failures_named", &failures_named},
    Scenario{"pw0_draws_with_materials", &pw0_draws_with_materials},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(
      argc, argv, kScenarios,
      "<metal artifact directory> <scratch directory>, then for pw0_draws_with_materials only "
      "<PW0 cache>",
      [](std::span<char* const> rest, Arguments& out) {
        if (rest.size() != 2U && rest.size() != 3U) {
          return false;
        }
        out = Arguments{rest[0], rest[1], rest.size() == 3U ? fs::path(rest[2]) : fs::path{}};
        return true;
      });
}
