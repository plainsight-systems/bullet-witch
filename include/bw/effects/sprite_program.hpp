// Prepared original sprite program. Decode once while loading a resource, then
// share as const data across ordered effect instances. The program owns copied
// curves and texture descriptors; it does not retain payload spans, GPU
// handles, a clock, or game state. Unsupported source features reject
// preparation with resources::Error rather than being silently approximated.
// Bounded fixed storage follows Core I.11 (#ri-raw), SL.con.3 (#rsl-bounds),
// and performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md);
// both corpora queried for this design. Scalar/vector curves are prepared
// consumer data per CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md).
#pragma once

#include "bw/content/formats/effect_pack.hpp"
#include "bw/math/matrix.hpp"

namespace bw::effects {
using SpriteVector = math::Vector3<float>;
using SpriteBasis = math::Matrix<float, 3, 3>;

enum class SpriteBlend : std::uint8_t { alpha, additive };
enum class SpriteOrientation : std::int8_t {
  authored = -1,
  camera = 0,
  cylindrical = 1,
  velocity = 2,
  axial = 4,
};
struct SpriteTexture {
  std::uint32_t index{}, width{}, height{};
};
struct SpriteTextureRegion {
  SpriteTexture texture;
  std::array<float, 4> uv{0, 0, 1, 1};
};
struct SpritePart {
  float duration{}, delay{}, period{}, period_jitter{};
  std::int32_t count{}, count_jitter{};
  std::uint32_t capacity{};
  float life{}, life_jitter{}, width{}, height{}, size_jitter_percent{};
  SpriteVector position{}, shape_axes{};
  float inner_radius{}, outer_radius{}, speed{}, speed_jitter{};
  std::array<float, 2> direction{}, direction_jitter{};
  SpriteVector rotation{}, rotation_jitter{}, angular_velocity{},
      angular_jitter{};
  std::int32_t basis_plane{}, radial_direction{};
  bool align_velocity{}, rotate_basis{}, world_space{};
  std::int32_t emitter_transform_mode{};
  SpriteVector emitter_rotation{};
  SpriteVector acceleration{};
  float wind_strength{}, collision_offset{}, collision_attenuation{},
      collision_angle{};
  SpriteOrientation orientation{};
  SpriteBlend blend{};
  float intensity{};
  bool random_color{}, sample_size{}, uniform_velocity_curve{}, ignore_tint{};
  bool distance_fade{};
  float fade_near_squared{}, fade_squared_reciprocal{};
  std::optional<formats::EffectVectorCurve> color_curve;
  std::optional<formats::EffectScalarCurve> alpha_curve, intensity_curve;
  std::optional<formats::EffectScalarCurve> width_curve, height_curve,
      angular_curve;
  std::array<std::optional<formats::EffectScalarCurve>, 3> velocity_curves;
  static constexpr std::size_t kTextureSlots = 8;
  std::array<SpriteTextureRegion, kTextureSlots> textures{};
  std::size_t texture_count{};
};

class SpriteProgram {
 public:
  // Texture table is local to the supplied pack; matching is by original index.
  // Pack slot is retained in the draw identity and never used as a GPU pointer.
  SpriteProgram(const formats::EffectPackView& pack, std::size_t entry,
                std::uint16_t pack_slot,
                std::span<const SpriteTexture> textures);
  std::uint32_t id() const noexcept { return id_; }
  std::uint16_t pack_slot() const noexcept { return pack_slot_; }
  float lifetime() const noexcept { return lifetime_; }
  float fade_seconds() const noexcept { return fade_seconds_; }
  float sort_bias() const noexcept { return sort_bias_; }
  bool translation_only() const noexcept { return translation_only_; }
  std::span<const SpritePart> parts() const noexcept {
    return {parts_.data(), part_count_};
  }

 private:
  std::uint32_t id_{};
  std::uint16_t pack_slot_{};
  float lifetime_{}, fade_seconds_{}, sort_bias_{};
  bool translation_only_{};
  std::array<SpritePart, formats::kEffectPartSlots> parts_{};
  std::size_t part_count_{};
};
}  // namespace bw::effects
