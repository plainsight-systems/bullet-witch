// Runtime texture-table overrides follow npc-appearance.md. Both-phase I.1
// and MEM.9 make input borrowing explicit and copy resolved bindings only at
// admission; color/shadow slots never search caller metadata during a frame.
// Instance admission owns copied animation state separately from shared assets.
// Design I.11 (ri-raw)/MEM.9 and implementation C.85/GPU.1: borrowed packets,
// allocation only at admission, atomic commit and retained geometry. See
// research/engine/subsystems/animated-instance-publication.md.
// Shading admission applies both-phase I.6 (#ri-expects), I.7 (#ri-post) and
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md): consumed power exponents
// are nonnegative; native filtering owns derived device-local normal moments.
// No mutable game publication or CPU readback accompanies that computation.
// Residency and scene publication are distinct capabilities. MissionLoading
// borrows WorldResources; WorldRuntime borrows WorldPresentation. Both may
// observe geometry readiness through WorldObservation, but neither can obtain
// the other's mutation authority. Only the native host composes WorldOutput.
// All interfaces have stable identity and no retained data. Borrowers outlive
// no owner and calls require the existing exclusive main-thread boundary.
// Core C.121 (#rh-abstract), I.4 (#ri-typed), I.11 (#ri-raw), and performance
// COPY.5 (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md)
// were read for design and implementation: pure capabilities, const
// observations and no copying device ownership. Capability separation adds no
// per-frame allocation. Color phases apply both-phase I.1 (#ri-explicit) and
// GPU.6 (guidelines/gpu/GPU.6-batch-tiny-gpu-work.md): publish explicit
// scene-wide order once, retain within-phase submission order, and reuse the
// native plan across pose/light/view updates. See
// scene-lighting-pass-contract.md. Component draw replacement applies I.1
// (#ri-explicit), C.85
// (#rc-swap-noexcept), and GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md):
// prepare discrete changes before nonthrowing publication, retaining unrelated
// state and resident resources. See model-draw-replacement.md; both MCP phases.
// View publication applies I.1/C.121 and GPU.1 (both phases): publish finite
// world/sky constants atomically, independently of resident geometry/materials.
// Native consumers retain one packet and compose it during their existing draw
// walk, without rebuilding scene bindings or allocating per camera update.
// The exclusive owner retires prior device work before publication. See
// research/engine/subsystems/live-view-publication.md for the reference design.
// Pose updates apply both-phase I.1/E.6 and GPU.1/MEM.9: explicit borrowed
// palettes update retained setup-sized storage transactionally, without mesh
// copies or allocation. Shape-position stream admission is explicit.
// Joint palettes apply I.11 (#ri-raw) and GPU.1: submission borrows a bounded
// span then owns a copy through device completion. The scene owns mapping and
// pose; the native renderer validates and consumes one/two/four source
// influences. Output policy is required for every implementation: C.121
// (#rh-abstract) prevents silent default behavior at a host adapter. Forwarding
// retains the existing device owner/pool; GPU.9 (guidelines/gpu/
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
// Mip sampling is explicit and independent of texture storage: I.1
// (#ri-explicit), retrieved for design and implementation, prevents native
// defaults from overriding authored sampler state. GPU.9 (guidelines/gpu/
// GPU.9-suballocate-and-alias-transient-gpu-memory.md) keeps sampler creation
// at submission setup; changing the filter adds no per-frame allocation.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include "bw/render/materials/world_material.hpp"
#include "bw/render/postprocess/world_output.hpp"

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
inline constexpr std::size_t kMaximumWorldLocalLights = 4;
// Original point-light packet: world position, RGB diffuse/specular (alpha1),
// and {distance exponent, scale, bias, diffuse contribution cutoff}.
struct WorldLocalLight {
  std::array<float, 4> position{}, diffuse{}, specular{}, attenuation{};
};
struct WorldColorParameters {
  std::array<float, 4> position_scale{}, position_offset{};
  std::array<std::array<float, 4>, 4> clip_rows{};
  std::array<std::array<float, 4>, 3> world_rows{};
  std::array<std::array<float, 4>, 2> texture_offset{};
  std::array<float, 4> indirect_color{}, material_color{}, fresnel{};
  // Consumed direct specular lobes require a nonnegative exponent in x.
  // Native pixel-footprint filtering preserves x/y in constant normal fields;
  // the renderer owns derived per-pixel variance, never mutable game state.
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
  // Shared light clip transform of the same post-palette position used by
  // clip_rows. Shadow rows use reverse depth, near=1/far=0. Parameters are
  // {enabled 0/1, receiver depth bias, filter radius in UV, reserved zero}.
  // Projection reserves shadow_guard_texels(max receiver radius) on each edge
  // of the shared 1024 map. Native casters are scissored to that interior;
  // the zero-cleared border makes point-clamped external taps unoccluded.
  // The radius plus its nearest-filter half-texel footprint must leave an
  // interior. Outside-UV receiver centers remain lit; depth comparison is
  // unchanged for centers inside the map. See shadow-filter-baseline.md.
  // I.1 makes caster/receiver agreement explicit; GPU.1 retains the map on
  // device between its depth and color consumers (both MCP phases).
  std::array<std::array<float, 4>, 4> shadow_clip_rows{};
  std::array<float, 4> shadow_parameters{};
  std::array<WorldLocalLight, kMaximumWorldLocalLights> local_lights{};
  std::array<std::uint32_t, 4>
      local_light_meta{};  // count, three reserved zeros
};
enum class WorldBlend : std::uint8_t { opaque, source_alpha };
enum class WorldTextureFilter : std::uint8_t {
  unresolved,
  nearest,
  linear,
  anisotropic
};
enum class WorldTextureAddress : std::uint8_t { unresolved, repeat, clamp };
enum class WorldMipFilter : std::uint8_t { unresolved, none, nearest, linear };
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
// Mip filtering selects among resident levels; it never generates levels or
// implies a mipmapped texture allocation. Unresolved filters reject submission.
struct WorldSurfaceState {
  WorldTextureFilter min_filter{}, mag_filter{};
  WorldTextureAddress address_u{}, address_v{};
  WorldCull cull{};
  WorldDepthCompare depth_compare{};
  bool depth_write{};
  WorldBlend blend{};
  std::uint32_t maximum_anisotropy{1};
  WorldMipFilter mip_filter{};
};
// Source palette rows perform dot products with a local position/vector.
// set_world_draws borrows these rows only for the call and retains its own
// copy. The source programs address at most 32 matrices; static draws pass
// none.
using WorldJointMatrix = std::array<std::array<float, 4>, 4>;
// Fixed draws keep their submitted constants. World and sky draws opt in to
// camera publication; sky has its authored camera-centered transform and fog.
enum class WorldViewSpace : std::uint8_t { fixed, world, sky };
// Background precedes ordinary surfaces. Opaque and masked phases establish
// depth; subsequent translucent and material-controlled phases only test it.
// Blend state remains explicit because the final phase permits material SRS
// overrides. Equal-phase commands preserve caller submission order.
enum class WorldColorPhase : std::uint8_t {
  background,
  opaque,
  masked,
  translucent,
  material_blended
};
struct WorldViewParameters {
  std::array<std::array<float, 4>, 4> clip_rows{};
  std::array<std::array<float, 4>, 3> world_rows{};
  std::array<float, 4> fog_color{}, fog_distance{}, fog_height{}, fog_density{};
  std::array<std::array<float, 4>, 4> shadow_clip_rows{};
};
struct WorldViewUpdate {
  WorldViewParameters world, sky;
  // Shared scene light state is independent of receiver coefficients. Omitted
  // state preserves draw-authored values for fixed diagnostic submissions.
  struct Lighting {
    std::array<float, 4> direction{}, diffuse{}, specular{};
    float shadow_radius{};
    bool shadow_casters_enabled{};
  };
  std::optional<Lighting> lighting{};
  // Scene pass controls are independent of material and light energy. Source
  // geometry uses integer sample centers, while isolated native diagnostics
  // omit this policy. Preparation replays opaque then masked owners before
  // either color family, preserving target color writes and source depth.
  // I.1/CDSA.32/MEM.9: complete fixed publication, no per-frame allocation.
  struct ScenePasses {
    bool opaque_preparation{}, masked_preparation{};
    bool integer_pixel_centers{};
    std::array<float, 3> clear_color{};
  };
  struct ShadowPass {
    bool setup_enabled{true};
    WorldCull cull{WorldCull::none};
    float depth_bias{}, slope_bias{};
  };
  std::optional<ScenePasses> scene_passes{};
  std::optional<ShadowPass> shadow_pass{};
};
struct WorldTextureOverride {
  std::size_t slot{};
  WorldTextureHandle texture;
};
struct WorldColorDraw {
  WorldModelHandle model;
  std::size_t draw{};
  std::uint32_t effective_shader_flags{};
  WorldColorParameters parameters;
  WorldSurfaceState state;
  WorldLightingPolicy lighting{WorldLightingPolicy::supplied};
  std::span<const WorldJointMatrix> joint_matrices;
  // Scene ownership selects casters; the renderer does not infer ownership
  // from material names. Shadow-enabled receivers may omit casting.
  bool casts_shadow{};
  // Admit the separately retained position stream. It starts at authored rest
  // and may receive CPU-deformed updates; absent opt-in rejects shape draws.
  bool use_shape_positions{};
  WorldViewSpace view_space{WorldViewSpace::fixed};
  bool visible{true};
  WorldColorPhase color_phase{WorldColorPhase::opaque};

  // Scene-local instance identity distinguishes placements sharing one resident
  // model. Stable for this submitted instance; zero is the ordinary single
  // instance. It owns no resource and must not be reused while updates are
  // live. I.1/MEM.9 (redesign design/implementation): explicit fixed-size
  // identity.
  std::uint64_t instance{};
  // Receiver-selected environment. Null uses material/global defaults; zero
  // explicitly selects opaque black. An authored material LENV binding wins.
  std::optional<WorldTextureHandle> reflection_texture{};
  // Instance-local runtime texture-table changes, affecting every semantic
  // that references a slot. Borrowed for admission only; renderer copies.
  // Slots must be distinct, in range, and name resident compatible textures.
  std::span<const WorldTextureOverride> texture_overrides{};
  // Source owner opacity is independent of the material chunk. Absent retains
  // diagnostic constants/Main admission. Present finite values replace only
  // indirect-color alpha: <=0 omits color, >=1 selects Main, (0,1) selects
  // Trans and its truncated opacity*128 fixed alpha reference. Caster
  // membership is separately published. No inferred fade clock is owned here.
  // I.1/E.4/MEM.9: explicit value, atomic validation, retained storage.
  std::optional<float> owner_opacity{};
  // Optional rigid model-to-world rows, copied at admission and composed with
  // the current view and shadow projection. Must be finite affine rows and
  // have no joint palette: skinned palettes already carry owner placement.
  // I.1/MEM.9 (both phases): explicit value ownership, no frame allocation.
  std::optional<WorldJointMatrix> model_transform{};
};
// Update only retained UV constants. Every matching model/draw instance
// receives the values; duplicate updates apply in order. The full borrowed
// batch must resolve to submitted draws and contain finite values before any
// write. O(updates * submitted draws), no allocation or geometry/sampler
// recreation. Both-phase I.1/GPU.1 keep identity and small mutable data
// explicit.
struct WorldTextureOffsetUpdate {
  WorldModelHandle model;
  std::size_t draw{};
  std::array<std::array<float, 4>, 2> texture_offset{};
};
// Replace palettes of selected submitted instances matching model/draw. The
// full batch must resolve, retain each published palette size, and contain only
// finite rows before any write. Duplicate updates apply in order. Complexity
// O(updates * log(draws) + copied matrices), no steady-state allocation. The
// owner must retire prior GPU work before updates, as for scene submission.
struct WorldPoseUpdate {
  WorldModelHandle model;
  std::size_t draw{};
  std::span<const WorldJointMatrix> joint_matrices;
  // Omission broadcasts; an explicit identity must resolve before any write.
  std::optional<std::uint64_t> instance{};
  // A rigid draw accepts an empty palette only with this finite affine
  // model-to-world transform. Omission preserves the retained transform.
  // Nonempty palettes and model_transform together are rejected.
  std::optional<WorldJointMatrix> model_transform{};
};
// Publish sampled world-space irradiance and up to four source direct lights
// together, without replacing geometry/materials. A zero direct count clears
// the previous selection; complete packets retain no stale active slots.
// Validate the entire finite batch and every retained destination before
// writes. Duplicate destinations apply in order. O(updates * log(draws) +
// matches), no allocation. Both-phase I.1/SL.con.3 and MEM.9 preserve explicit
// ownership, atomic validation and prepared storage; see
// character-light-sampling.md.
struct WorldLightingUpdate {
  WorldModelHandle model;
  std::size_t draw{};
  std::array<std::array<float, 4>, WorldColorParameters::kIrradianceVectors>
      irradiance_coefficients{};
  std::array<WorldLocalLight, kMaximumWorldLocalLights> local_lights{};
  std::uint32_t local_light_count{};
  // Omitted identity deliberately broadcasts to matching model/draw instances.
  // A specified identity must resolve before the whole batch can publish. This
  // prevents one placed object's SH from overwriting every copy of its model.
  std::optional<std::uint64_t> instance{};
  std::optional<WorldTextureHandle> reflection_texture{};
};
// Change source-selected draw visibility without rebuilding retained geometry,
// palettes or materials. Validate every model/draw destination before mutation;
// duplicate updates apply in order to all matching instances. Both shadow and
// color passes honor the value. O(updates * log(draws) + matches), no
// allocation. I.1 (#ri-explicit)/E.4 (#re-design-invariants) define atomic
// batch behavior; GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md) retains
// resident assets.
struct WorldVisibilityUpdate {
  WorldModelHandle model;
  std::size_t draw{};
  bool visible{true};
  // Source owner selection publishes shadow membership independently from
  // ordinary visibility. Omission preserves it during animation group updates.
  std::optional<bool> casts_shadow{};
  std::optional<std::uint64_t> instance{};
  // Omission preserves the prior owner opacity, including the absent
  // diagnostic policy. Finite values replace it atomically with this batch.
  std::optional<float> owner_opacity{};
};
// Complete CPU-deformed position stream; borrowed only for synchronous upload.
struct WorldShapePositionUpdate {
  WorldModelHandle model;
  std::size_t buffer{};
  std::span<const std::array<float, 3>> positions;
  // Select admitted instance storage; omission broadcasts to live instances.
  std::optional<std::uint64_t> instance{};
};
class WorldObservation {
 public:
  WorldObservation() = default;
  virtual ~WorldObservation() = default;
  WorldObservation(const WorldObservation&) = delete;
  WorldObservation& operator=(const WorldObservation&) = delete;
  WorldObservation(WorldObservation&&) = delete;
  WorldObservation& operator=(WorldObservation&&) = delete;
  virtual WorldGeometryStatus geometry_status(WorldModelHandle model) const = 0;
};
// The resource owner alone may admit/bind/retire model and texture identities.
class WorldResources : public virtual WorldObservation {
 public:
  // Copy once at upload; callers may release the source definition afterward.
  virtual WorldModelHandle upload_model(
      const formats::ModelDefinition& model) = 0;
  // Copy the complete prepared chain; preserve format and every level/face.
  virtual WorldTextureHandle upload_world_texture(
      const formats::WorldTextureImage& image) = 0;
  virtual void release_world_texture(WorldTextureHandle texture) noexcept = 0;
  // Runtime table order is distinct from a model's informational texture list.
  // Zero handles explicitly mark unresident slots. Selected draws require their
  // own bindings; whole-table readiness remains false until all slots reside.
  virtual void bind_model_textures(
      WorldModelHandle model, std::span<const WorldTextureHandle> textures) = 0;
  virtual void release_model(WorldModelHandle model) noexcept = 0;
};
// Scene publication borrows resident identities; it cannot create or retire
// them.
class WorldPresentation : public virtual WorldObservation {
 public:
  // Explicit ordered selection; unsupported required materials reject
  // atomically.
  // Consume authored tone/bloom policy explicitly. Geometry-only fixtures must
  // declare their observation behavior; a host cannot silently drop the call.
  virtual void set_world_output_parameters(const WorldOutputParameters&) = 0;
  virtual void set_world_draws(std::span<const WorldColorDraw> draws) = 0;
  // Discrete component replacement; prior GPU work must have retired. Replace
  // every submitted previous-model draw at its first position, preserving all
  // unrelated order, latest UV/palettes, view and output state. Nonempty inputs
  // must name one distinct resident model not already submitted. Validate and
  // allocate before atomic publication; failure retains the old scene.
  // An empty replacement explicitly removes that submitted model, retaining
  // unrelated draws/view and resident assets; an unknown previous model
  // rejects. No geometry/texture upload or release; required fallback textures
  // must already be resident. O(scene + palettes + scene log scene), setup
  // allocation only.
  virtual void replace_world_model_draws(
      WorldModelHandle previous,
      std::span<const WorldColorDraw> replacement) = 0;
  // Admit one instance identity across one or more fresh model pairs
  // atomically. Every (model, instance) must be absent; ordinals are unique per
  // model. Immutable assets are shared; copied palettes and shape storage
  // belong to the new instance. Preserve existing state/view. Nonempty, unique
  // draw identities. Required fallback textures must already be resident.
  // Admission allocates O(scene + palettes + texture bindings + shape bytes);
  // steady-state publication does not.
  virtual void add_world_instance_draws(
      std::span<const WorldColorDraw> draws) = 0;
  // Remove exactly one live pair; stale pairs reject. Does not release assets.
  virtual void remove_world_instance_draws(WorldModelHandle model,
                                           std::uint64_t instance) = 0;
  virtual void update_world_texture_offsets(
      std::span<const WorldTextureOffsetUpdate> updates) = 0;
  virtual void update_world_lighting(
      std::span<const WorldLightingUpdate> updates) = 0;
  virtual void update_world_poses(std::span<const WorldPoseUpdate> updates) = 0;
  // Caller retires prior GPU work. Validate the entire batch (resident model,
  // exact float3 extent, finite values) before copying retained instance
  // storage. Both color and shadow passes consume the same snapshot; no
  // allocation or skinning occurs here. Duplicate destinations apply in
  // supplied order.
  virtual void update_shape_positions(
      std::span<const WorldShapePositionUpdate> updates) = 0;
  virtual void update_world_visibility(
      std::span<const WorldVisibilityUpdate> updates) = 0;
  // Validate both spaces before publishing either. Only the listed view/fog/
  // shadow rows change; per-draw shadow enable/bias/filter and material/pose
  // state stay intact. Successful publication allocates nothing. Replacing or
  // clearing the draw list resets the override to its submitted constants.
  virtual void update_world_view(const WorldViewUpdate& update) = 0;
  virtual void clear_world_draws() noexcept = 0;

 private:
  // One scene lifetime authority, without duplicating native draw membership.
  friend class RenderScene;
  bool scene_claimed_{};
};
// Actual host/backend implements both capabilities; game borrowers use one.
class WorldOutput : public WorldResources, public WorldPresentation {};
}  // namespace bw::render
