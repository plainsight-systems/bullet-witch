// Output policy is required for every implementation: C.121 (#rh-abstract)
// prevents silent default behavior at a host adapter. Forwarding retains the
// existing device owner/pool; GPU.9 (guidelines/gpu/
// GPU.9-suballocate-and-alias-transient-gpu-memory.md) adds no per-frame
// storage. Portable ownership and submission boundary for immutable world
// geometry, decoded textures and ordered draws using explicit original material
// inputs. Upload borrows a validated ModelDefinition for the call and copies
// its native vertex/index bytes plus draw metadata; returned identities belong
// only to this output. Upload failure publishes no identity and retains prior
// resources. Release invalidates an identity after device consumers retire;
// stale release is harmless. All calls require exclusive owner access. Geometry
// residency is explicitly separate from material/pass readiness: upload never
// means original color shaders, visibility or stage presentation have been
// reconstructed. Algorithm: validate decoded inputs -> reserve device memory ->
// copy immutable streams/indices -> publish handle -> consume selected draws ->
// retire/release. Setup scales with bytes and draw records; unchanged buffers
// stay device-owned. Submitted texture tables may contain explicit absent
// slots; each selected draw must resolve its required inputs. Submission
// replacement is transactional and resource release invalidates retained
// submissions. Native types do not cross this header. See
// world-renderer-foundation.md and
// docs/architecture/world-renderer-reconstruction.md. Both MCP phases retrieved
// cpp-guidelines E.6 (#re-raii), pairing owned resources/rollback, and
// cpp-performance GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md),
// retaining immutable device buffers rather than uploading again every frame.
// Spherical-harmonic inputs apply I.1 (#ri-explicit): their seven packed
// polynomial vectors are explicit per draw, independent of material ambience.
// Raster-state refinement also applies I.5 (#ri-pre): valid filter/anisotropy
// pairs and explicit depth ordering are checked before publishing submissions.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "bw/render/world_material.hpp"
#include "bw/render/world_output.hpp"

namespace bw::formats {
class ModelDefinition;
struct WorldTextureImage;
}  // namespace bw::formats
namespace bw::render {
struct WorldModelHandle {
  std::uint64_t value{};
  friend bool operator==(WorldModelHandle, WorldModelHandle) = default;
};
struct WorldTextureHandle {
  std::uint64_t value{};
  friend bool operator==(WorldTextureHandle, WorldTextureHandle) = default;
};
struct WorldGeometryStatus {
  std::size_t vertex_bytes{}, index_bytes{}, draws{}, materials{};
  // Original color-pass readiness must not be inferred from successful upload.
  bool texture_bindings_ready{};
  bool color_materials_ready{};
};
// Original static depth transform and slope-bias inputs, independent of
// gameplay camera or environment policy. Rows are dot products with the
// scaled/offset position. Clip z lies in [0,w].
struct WorldDepthParameters {
  std::array<float, 4> position_scale{1, 1, 1, 1};
  std::array<float, 4> position_offset{};
  std::array<std::array<float, 4>, 4> clip_rows{};
  std::array<float, 4> depth_bias{};
};
// Recovered static surface-family inputs. Values must come from recovered
// material/instance/environment owners; zero initialization is not a stage
// preset.
struct WorldColorParameters {
  std::array<float, 4> position_scale{}, position_offset{};
  std::array<std::array<float, 4>, 4> clip_rows{};
  std::array<std::array<float, 4>, 3> world_rows{};
  std::array<std::array<float, 4>, 2> texture_offset{};
  std::array<float, 4> indirect_color{}, material_color{}, fresnel{};
  std::array<float, 4> illumination{}, ambient{}, reflection{}, specular{};
  std::array<float, 4> light_diffuse{}, light_specular{}, light_direction{};
  std::array<float, 4> fog_color{}, fog_distance{}, fog_height{}, fog_density{};
  std::array<float, 4> indirect_secondary{}, environment_power{};
  // Packed irradiance polynomial: three linear/constant vectors, three
  // quadratic vectors, and the x²-y² vector. Values are already normalized
  // by the original CPU coefficient packer; the GPU evaluates at the normal.
  static constexpr std::size_t kIrradianceVectors = 7;
  std::array<std::array<float, 4>, kIrradianceVectors>
      irradiance_coefficients{};
};
enum class WorldBlend : std::uint8_t { opaque, source_alpha };
enum class WorldTextureFilter : std::uint8_t {
  unresolved,
  nearest,
  linear,
  anisotropic
};
enum class WorldTextureAddress : std::uint8_t { unresolved, repeat, clamp };
enum class WorldCull : std::uint8_t {
  unresolved,
  none,
  clockwise,
  counter_clockwise
};
enum class WorldDepthCompare : std::uint8_t {
  unresolved,
  less,
  less_equal,
  greater,
  greater_equal,
  always
};
// Minification may use anisotropy (1..16 taps); other filters require one.
// Magnification remains nearest/linear. Depth ordering must match clip-space
// projection and the caller-owned clear value. Native scene presentation uses
// original reversed depth; isolated caller-owned passes may choose otherwise.
struct WorldSurfaceState {
  WorldTextureFilter min_filter{}, mag_filter{};
  WorldTextureAddress address_u{}, address_v{};
  WorldCull cull{};
  WorldDepthCompare depth_compare{};
  bool depth_write{};
  WorldBlend blend{};
  std::uint32_t maximum_anisotropy{1};
};
struct WorldColorDraw {
  WorldModelHandle model;
  std::size_t draw{};
  std::uint32_t effective_shader_flags{};
  WorldColorParameters parameters;
  WorldSurfaceState state;
  WorldLightingPolicy lighting{WorldLightingPolicy::supplied};
};
class WorldOutput {
 public:
  WorldOutput() = default;
  virtual ~WorldOutput() = default;
  WorldOutput(const WorldOutput&) = delete;
  WorldOutput& operator=(const WorldOutput&) = delete;
  WorldOutput(WorldOutput&&) = delete;
  WorldOutput& operator=(WorldOutput&&) = delete;
  // Copy once at upload; callers may release the source definition afterward.
  virtual WorldModelHandle upload_model(
      const formats::ModelDefinition& model) = 0;
  // Copy original decoded base pixels; preserve RGBA8 versus packed half-float.
  virtual WorldTextureHandle upload_world_texture(
      const formats::WorldTextureImage& image) = 0;
  virtual void release_world_texture(WorldTextureHandle texture) noexcept = 0;
  // Runtime table order is distinct from a model's informational texture list.
  // Zero handles explicitly mark unresident slots. Selected draws require their
  // own bindings; whole-table readiness remains false until all slots reside.
  virtual void bind_model_textures(
      WorldModelHandle model, std::span<const WorldTextureHandle> textures) = 0;
  virtual WorldGeometryStatus geometry_status(WorldModelHandle model) const = 0;
  virtual void release_model(WorldModelHandle model) noexcept = 0;
  // Explicit ordered selection; unsupported required materials reject
  // atomically.
  // Consume authored tone/bloom policy explicitly. Geometry-only fixtures must
  // declare their observation behavior; a host cannot silently drop the call.
  virtual void set_world_output_parameters(const WorldOutputParameters&) = 0;
  virtual void set_world_draws(std::span<const WorldColorDraw> draws) = 0;
  virtual void clear_world_draws() noexcept = 0;
};
}  // namespace bw::render
