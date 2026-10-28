// Load-time preparation keeps compatibility offsets out of particle updates.
// Implementation Core SL.con.3 (#rsl-bounds) checks all referenced tables and
// texture extents; performance CDSA.32 (guidelines/cpu-dsa/
// CDSA.32-transform-static-data-into-the-consumer-layout.md) copies immutable
// consumer data once. Both corpora reread for this implementation phase.
#include "bw/effects/sprite_program.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bw::effects {
namespace {
constexpr std::int32_t kUniformVelocityCurve = 0x10000;
constexpr std::uint32_t kSmallPool = 9, kMediumPool = 32, kLargePool = 128,
                        kLargestPool = 320;
namespace part_field {
constexpr std::size_t kDefinition = 0x4;
constexpr std::size_t kDuration = 0x8;
constexpr std::size_t kLife = 0x10;
constexpr std::size_t kLifeJitter = 0x14;
constexpr std::size_t kDelay = 0x18;
constexpr std::size_t kCount = 0x1c;
constexpr std::size_t kCountJitter = 0x20;
constexpr std::size_t kCapacity = 0x24;
constexpr std::size_t kPeriod = 0x28;
constexpr std::size_t kPeriodJitter = 0x2c;
constexpr std::size_t kCatchUp = 0x30;
constexpr std::size_t kDeterministicSpread = 0x34;
constexpr std::size_t kPosition = 0x40;
constexpr std::size_t kInnerRadius = 0x50;
constexpr std::size_t kOuterRadius = 0x54;
constexpr std::size_t kShapeAxes = 0x58;
constexpr std::size_t kWidth = 0x80;
constexpr std::size_t kHeight = 0x84;
constexpr std::size_t kSizeJitter = 0x88;
constexpr std::size_t kSpeed = 0x8c;
constexpr std::size_t kSpeedJitter = 0x90;
constexpr std::size_t kPitch = 0x94;
constexpr std::size_t kYaw = 0x98;
constexpr std::size_t kPitchJitter = 0x9c;
constexpr std::size_t kYawJitter = 0xa0;
constexpr std::size_t kAcceleration = 0xc0;
constexpr std::size_t kRotation = 0xf0;
constexpr std::size_t kBasisPlane = 0xfc;
constexpr std::size_t kRotationJitter = 0x100;
constexpr std::size_t kAngularVelocity = 0x110;
constexpr std::size_t kAngularJitter = 0x120;
constexpr std::size_t kRadialDirection = 0x130;
constexpr std::size_t kAlignVelocity = 0x134;
constexpr std::size_t kParticleSort = 0x138;
constexpr std::size_t kFadeNear = 0x13c, kFadeFar = 0x140;
constexpr std::size_t kDistanceFade = 0x194;
constexpr std::size_t kEmitterTransform = 0x144;
constexpr std::size_t kEmitterRotation = 0x150;
constexpr std::size_t kEmitterRate = 0x160;
constexpr std::size_t kEmitterPitchRate = 0x164;
constexpr std::size_t kEmitterYawRate = 0x168;
constexpr std::size_t kWind = 0x170;
constexpr std::size_t kContactOffset = 0x178;
constexpr std::size_t kContactAttenuation = 0x17c;
constexpr std::size_t kContactAngle = 0x180;
constexpr std::size_t kContactChild = 0x184;
constexpr std::size_t kContactLife = 0x188;
constexpr std::size_t kRotateBasis = 0x198;
constexpr std::size_t kModulationBegin = 0x1a0;
constexpr std::size_t kModulationEnd = 0x1c8;
constexpr std::size_t kTextures = 0x1cc;
constexpr std::size_t kSequence = 0x1ec;
constexpr std::size_t kChild = 0x1f0;
}  // namespace part_field
namespace definition_field {
constexpr std::size_t kKind = 0x0;
constexpr std::size_t kBlend = 0x4;
constexpr std::size_t kOrientation = 0x8;
constexpr std::size_t kWorldSpace = 0xc;
constexpr std::size_t kIgnoreTint = 0x20;
constexpr std::size_t kRegionWidth = 0x2c;
constexpr std::size_t kRegionHeight = 0x30;
constexpr std::size_t kUvStepX = 0x34;
constexpr std::size_t kUvStepY = 0x38;
constexpr std::size_t kUvPeriod = 0x3c;
constexpr std::size_t kUvRandomFrames = 0x40;
constexpr std::size_t kUvFlag44 = 0x44;
constexpr std::size_t kUvFlag48 = 0x48;
constexpr std::size_t kEmission = 0x50;
constexpr std::size_t kShader = 0x5c;
constexpr std::size_t kIntensity = 0x68;
constexpr std::size_t kRandomColor = 0x6c;
constexpr std::size_t kChild = 0x88;
constexpr std::size_t kColorCurve = 0xa4;
constexpr std::size_t kAlphaCurve = 0xa8;
constexpr std::size_t kIntensityCurve = 0xac;
constexpr std::size_t kWidthCurve = 0xb8;
constexpr std::size_t kHeightCurve = 0xbc;
constexpr std::size_t kVelocityX = 0xc0;
constexpr std::size_t kVelocityY = 0xc4;
constexpr std::size_t kVelocityZ = 0xc8;
constexpr std::size_t kAngularCurve = 0xcc;
constexpr std::size_t kSampleSize = 0xd0;
}  // namespace definition_field
namespace source_field {
constexpr std::size_t kLifetime = 0x44;
constexpr std::size_t kFade = 0x48;
constexpr std::size_t kLoop = 0x4c;
constexpr std::size_t kSortBias = 0x50;
constexpr std::size_t kTranslationOnly = 0x54;
constexpr std::size_t kLight = 0x60;
constexpr std::size_t kTimeCurve = 0x7c;
constexpr std::size_t kRadialForce = 0x84;
}  // namespace source_field
void require(bool condition, const char* message) {
  if (!condition) {
    throw resources::Error(resources::ErrorCode::unsupported, message);
  }
}
float number(const formats::EffectRecordView& record, std::size_t offset) {
  const auto value = record.scalar(offset);
  require(std::isfinite(value), "Nonfinite sprite program field");
  return value;
}
SpriteVector vector(const formats::EffectRecordView& record,
                    std::size_t offset) {
  return {number(record, offset), number(record, offset + 4),
          number(record, offset + 2 * sizeof(float))};
}
std::optional<formats::EffectScalarCurve> curve(
    const formats::EffectPackView& pack, std::int32_t index) {
  if (index < 0) return std::nullopt;
  return pack.scalar_curve(static_cast<std::size_t>(index));
}
SpritePart prepare_part(const formats::EffectPackView& pack, std::size_t index,
                        std::span<const SpriteTexture> textures) {
  const auto part = pack.part(index);
  const auto definition = pack.definition(
      static_cast<std::size_t>(part.signed_word(part_field::kDefinition)));
  require(definition.word(definition_field::kKind) == 0 &&
              definition.word(definition_field::kEmission) == 0 &&
              definition.word(definition_field::kShader) == 0,
          "Unsupported sprite kind, emission mode or shader");
  require(definition.signed_word(definition_field::kChild) < 0 &&
              part.signed_word(part_field::kContactChild) < 0 &&
              part.signed_word(part_field::kSequence) < 0 &&
              part.signed_word(part_field::kChild) < 0,
          "Nested sprite programs require sequence execution");
  require(part.word(part_field::kCatchUp) == 0 &&
              part.word(part_field::kDeterministicSpread) == 0,
          "Unsupported sprite catch-up or deterministic spread");
  for (std::size_t offset = part_field::kModulationBegin;
       offset <= part_field::kModulationEnd; offset += 4) {
    require(part.signed_word(offset) < 0,
            "Emitter modulation requires additional program preparation");
  }
  require(definition.word(definition_field::kUvStepX) == 0 &&
              definition.word(definition_field::kUvStepY) == 0 &&
              definition.word(definition_field::kUvRandomFrames) == 0 &&
              definition.word(definition_field::kUvFlag44) == 0 &&
              definition.word(definition_field::kUvFlag48) == 0,
          "Animated sprite UV modes require additional preparation");
  require(part.signed_word(part_field::kEmitterTransform) == 0 ||
              (part.word(part_field::kEmitterRate) == 0 &&
               part.word(part_field::kEmitterPitchRate) == 0 &&
               part.word(part_field::kEmitterYawRate) == 0),
          "Rotating emitter transform requires modulation execution");
  require(part.scalar(part_field::kContactLife) < 0,
          "Collision lifetime replacement requires source execution");
  const auto orientation =
      definition.signed_word(definition_field::kOrientation);
  require(orientation == -1 || orientation == 0 || orientation == 1 ||
              orientation == 2 || orientation == 4,
          "Unsupported sprite orientation");
  const auto blend = definition.word(definition_field::kBlend);
  require(blend == 1 || blend == 2, "Unsupported sprite blend");
  SpritePart result;
  result.orientation = static_cast<SpriteOrientation>(orientation);
  result.blend = blend == 1 ? SpriteBlend::alpha : SpriteBlend::additive;
  result.world_space = definition.word(definition_field::kWorldSpace) != 0;
  result.ignore_tint = definition.word(definition_field::kIgnoreTint) != 0;
  require(part.word(part_field::kParticleSort) == 0,
          "Particle sorting requires an additional source path");
  result.distance_fade = part.word(part_field::kDistanceFade) != 0;
  if (result.distance_fade) {
    const auto near = number(part, part_field::kFadeNear);
    const auto far = number(part, part_field::kFadeFar);
    require(near >= 0 && far > near, "Invalid sprite distance fade range");
    result.fade_near_squared = near * near;
    result.fade_squared_reciprocal = 1 / (far * far - near * near);
  }
  result.duration = number(part, part_field::kDuration);
  result.delay = number(part, part_field::kDelay);
  result.period = number(part, part_field::kPeriod);
  result.period_jitter = number(part, part_field::kPeriodJitter);
  result.count = part.signed_word(part_field::kCount);
  result.count_jitter = part.signed_word(part_field::kCountJitter);
  const auto capacity = part.signed_word(part_field::kCapacity);
  require(capacity >= 0 &&
              capacity <= static_cast<std::int32_t>(kLargestPool) &&
              result.count >= 0 && result.count_jitter >= 0 &&
              result.count_jitter <= result.count &&
              result.count <= std::numeric_limits<std::int32_t>::max() / 2,
          "Invalid sprite capacity or burst count");
  result.capacity =
      capacity <= static_cast<std::int32_t>(kSmallPool)    ? kSmallPool
      : capacity <= static_cast<std::int32_t>(kMediumPool) ? kMediumPool
      : capacity <= static_cast<std::int32_t>(kLargePool)  ? kLargePool
                                                           : kLargestPool;
  result.life = number(part, part_field::kLife);
  result.life_jitter = number(part, part_field::kLifeJitter);
  require(result.life > result.life_jitter && result.life_jitter >= 0 &&
              result.delay >= 0 && result.period_jitter >= 0,
          "Unsupported sprite lifetime or negative jitter");
  result.width = number(part, part_field::kWidth);
  result.height = number(part, part_field::kHeight);
  result.size_jitter_percent = number(part, part_field::kSizeJitter);
  result.position = vector(part, part_field::kPosition);
  result.inner_radius = number(part, part_field::kInnerRadius);
  result.outer_radius = number(part, part_field::kOuterRadius);
  result.shape_axes = vector(part, part_field::kShapeAxes);
  result.speed = number(part, part_field::kSpeed);
  result.speed_jitter = number(part, part_field::kSpeedJitter);
  result.direction = {number(part, part_field::kPitch),
                      number(part, part_field::kYaw)};
  result.direction_jitter = {number(part, part_field::kPitchJitter),
                             number(part, part_field::kYawJitter)};
  result.acceleration = vector(part, part_field::kAcceleration);
  result.rotation = vector(part, part_field::kRotation);
  const auto plane = number(part, part_field::kBasisPlane);
  require(plane >= 0 && plane <= 2, "Invalid sprite basis plane");
  result.basis_plane = static_cast<std::int32_t>(plane);
  result.rotation_jitter = vector(part, part_field::kRotationJitter);
  result.angular_velocity = vector(part, part_field::kAngularVelocity);
  result.angular_jitter = vector(part, part_field::kAngularJitter);
  result.radial_direction = part.signed_word(part_field::kRadialDirection);
  require(result.radial_direction >= 0 && result.radial_direction <= 2,
          "Unsupported sprite radial direction");
  result.align_velocity = part.word(part_field::kAlignVelocity) != 0;
  result.emitter_transform_mode =
      part.signed_word(part_field::kEmitterTransform);
  require(
      result.emitter_transform_mode == 0 || result.emitter_transform_mode == 2,
      "Unsupported sprite emitter transform mode");
  result.emitter_rotation = vector(part, part_field::kEmitterRotation);
  result.wind_strength = number(part, part_field::kWind);
  result.collision_offset = number(part, part_field::kContactOffset);
  result.collision_attenuation = number(part, part_field::kContactAttenuation);
  result.collision_angle = number(part, part_field::kContactAngle);
  result.rotate_basis = part.word(part_field::kRotateBasis) != 0;
  result.intensity = number(definition, definition_field::kIntensity);
  result.random_color = definition.word(definition_field::kRandomColor) != 0;
  result.sample_size = definition.word(definition_field::kSampleSize) != 0;
  const auto color_index =
      definition.signed_word(definition_field::kColorCurve);
  if (color_index >= 0) {
    result.color_curve =
        pack.vector_curve(static_cast<std::size_t>(color_index));
    for (const auto& value : *result.color_curve) {
      require(math::all_finite(value), "Nonfinite sprite color curve");
    }
  }
  result.alpha_curve =
      curve(pack, definition.signed_word(definition_field::kAlphaCurve));
  result.intensity_curve =
      curve(pack, definition.signed_word(definition_field::kIntensityCurve));
  result.width_curve =
      curve(pack, definition.signed_word(definition_field::kWidthCurve));
  result.height_curve =
      curve(pack, definition.signed_word(definition_field::kHeightCurve));
  result.angular_curve =
      curve(pack, definition.signed_word(definition_field::kAngularCurve));
  const auto velocity_index =
      definition.signed_word(definition_field::kVelocityX);
  result.uniform_velocity_curve = velocity_index >= kUniformVelocityCurve;
  if (result.uniform_velocity_curve) {
    result.velocity_curves[0] =
        curve(pack, velocity_index - kUniformVelocityCurve);
  } else if (velocity_index >= -1) {
    result.velocity_curves[0] = curve(pack, velocity_index);
    result.velocity_curves[1] =
        curve(pack, definition.signed_word(definition_field::kVelocityY));
    result.velocity_curves[2] =
        curve(pack, definition.signed_word(definition_field::kVelocityZ));
  }
  for (std::size_t slot = 0; slot < result.textures.size(); ++slot) {
    const auto texture_index =
        part.signed_word(part_field::kTextures + slot * 4);
    if (texture_index < 0) break;
    const auto found =
        std::ranges::find(textures, static_cast<std::uint32_t>(texture_index),
                          &SpriteTexture::index);
    require(found != textures.end(), "Missing original sprite texture");
    require(texture_index <= std::numeric_limits<std::uint16_t>::max(),
            "Sprite texture identity exceeds original field");
    require(found->width >= 2 && found->height >= 2,
            "Sprite texture cannot contain the authored border");
    auto& region = result.textures.at(result.texture_count++);
    region.texture = *found;
    if (definition.scalar(definition_field::kUvPeriod) > 0) {
      const auto authored_width =
          number(definition, definition_field::kRegionWidth);
      const auto authored_height =
          number(definition, definition_field::kRegionHeight);
      require(authored_width <= static_cast<float>(found->width) &&
                  authored_height <= static_cast<float>(found->height),
              "Sprite region exceeds supplied texture");
      const auto width = authored_width < 1
                             ? found->width
                             : static_cast<std::uint32_t>(authored_width);
      const auto height = authored_height < 1
                              ? found->height
                              : static_cast<std::uint32_t>(authored_height);
      require(width >= 2 && height >= 2 && width <= found->width &&
                  height <= found->height,
              "Invalid sprite texture region");
      const auto tw = static_cast<float>(found->width);
      const auto th = static_cast<float>(found->height);
      region.uv = {1 / tw, 1 / th, static_cast<float>(width - 2) / tw + 1 / tw,
                   static_cast<float>(height - 2) / th + 1 / th};
    }
  }
  require(result.texture_count > 0, "Sprite program has no texture");
  return result;
}
}  // namespace

SpriteProgram::SpriteProgram(const formats::EffectPackView& pack,
                             std::size_t entry, std::uint16_t pack_slot,
                             std::span<const SpriteTexture> textures)
    : pack_slot_(pack_slot) {
  const auto effect = pack.effect(entry);
  const auto source = pack.effect_record(entry);
  require(source.word(source_field::kLoop) == 0 &&
              source.signed_word(source_field::kTimeCurve) < 0 &&
              source.word(source_field::kRadialForce) == 0 &&
              source.scalar(source_field::kLight) == 0,
          "Unsupported sprite instance looping or modulation");
  id_ = effect.id;
  lifetime_ = number(source, source_field::kLifetime);
  fade_seconds_ = number(source, source_field::kFade);
  sort_bias_ = number(source, source_field::kSortBias);
  translation_only_ = source.word(source_field::kTranslationOnly) != 0;
  require(lifetime_ > 0 && fade_seconds_ >= 0,
          "Invalid sprite instance lifetime");
  for (const auto index : effect.parts) {
    if (index >= 0) {
      parts_.at(part_count_++) =
          prepare_part(pack, static_cast<std::size_t>(index), textures);
    }
  }
}
}  // namespace bw::effects
