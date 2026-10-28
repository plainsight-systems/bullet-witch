// Decode purchased native tables into bounded character physics records.
// Section mapping and every record read are checked before use. The executable
// is borrowed only during import; no pointers into it escape. Source body
// geometry conversion happens once, before provider/skeleton allocation.
// Design/implementation Core R.2 (#rr-use-ptr) uses bounded spans; performance
// CDSA.32 (transform-static-data-into-the-consumer-layout) copies
// consumer-ready values at load. Fixed native capacity avoids
// allocator/lifetime dependencies.
#include "character_physics_recipe.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <numbers>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::game {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using resources::detail::unsupported;
constexpr std::uint32_t kPrefixTable = 0x964d30;
constexpr std::uint32_t kImageBase = 0x400000;
constexpr std::uint64_t kPePointer = 0x3c, kPeHeaderBytes = 24;
constexpr std::uint64_t kSectionBytes = 40, kRecordBytes = 32;
constexpr std::uint64_t kWordBytes = 4;
constexpr std::uint16_t kDosMagic = 0x5a4d, kMachineX86 = 0x14c;
constexpr std::uint32_t kPeSignature = 0x4550;
constexpr std::uint16_t kPe32 = 0x10b;
constexpr std::uint64_t kMachineField = 4, kSectionCountField = 6,
                        kOptionalSizeField = 20;
constexpr std::uint64_t kImageBaseField = 28;
constexpr std::uint64_t kSectionRva = 12, kSectionFileSize = 16,
                        kSectionFileOffset = 20;
constexpr std::uint64_t kRecipeBones = 4, kRecipeBodyCount = 8,
                        kRecipeJointCount = 12;
constexpr std::uint64_t kRecipeBodies = 16, kRecipeJoints = 20;
constexpr std::uint64_t kJointFirst = 4, kJointSecond = 8, kJointGroup = 12;
constexpr std::uint64_t kJointAxis = 16, kJointDirectionW = 28;
class Image {
 public:
  explicit Image(std::span<const std::uint8_t> bytes) : reader_{bytes} {
    const auto pe = reader_.u32(kPePointer);
    const auto optional = pe + kPeHeaderBytes;
    if (reader_.u16(0) != kDosMagic || reader_.u32(pe) != kPeSignature ||
        reader_.u16(pe + kMachineField) != kMachineX86 ||
        reader_.u16(optional) != kPe32 ||
        reader_.u32(optional + kImageBaseField) != kImageBase) {
      unsupported("Character recipe requires the original Steam PE32 image");
    }
    sections_ = optional + reader_.u16(pe + kOptionalSizeField);
    section_count_ = reader_.u16(pe + kSectionCountField);
    reader_.range(sections_, section_count_ * kSectionBytes);
  }
  Reader at(resources::Range region) const {
    const auto address = region.offset;
    const auto bytes = region.size;
    if (address < kImageBase) {
      malformed("Invalid native table address");
    }
    const auto rva = address - kImageBase;
    for (std::uint64_t i = 0; i < section_count_; ++i) {
      const auto section = sections_ + i * kSectionBytes;
      const auto base = reader_.u32(section + kSectionRva);
      const auto size = reader_.u32(section + kSectionFileSize);
      if (rva >= base && rva - base < size) {
        const auto delta = rva - base;
        if (bytes > size - delta) {
          malformed("Native table crosses section");
        }
        const auto offset = static_cast<std::uint64_t>(
                                reader_.u32(section + kSectionFileOffset)) +
                            delta;
        reader_.range(offset, bytes);
        return {reader_.b.subspan(static_cast<std::size_t>(offset),
                                  static_cast<std::size_t>(bytes))};
      }
    }
    malformed("Native table is not file-backed");
  }
  std::string_view name(std::uint32_t address) const {
    constexpr std::uint64_t kNameBytes = 32;
    return at({address, kNameBytes}).text_view({0, kNameBytes}, 0);
  }

 private:
  Reader reader_;
  std::uint64_t sections_{};
  std::uint16_t section_count_{};
};
bool prefix_matches(std::string_view authored, std::string_view input) {
  if (input.size() < authored.size()) {
    return false;
  }
  const auto uppercase = [](char value) {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - 'a' + 'A')
                                        : value;
  };
  return std::equal(authored.begin(), authored.end(), input.begin(),
                    [&](char first, char second) {
                      return uppercase(first) == uppercase(second);
                    });
}
float scalar(const Reader& reader, std::uint64_t offset) {
  const auto result = std::bit_cast<float>(reader.u32(offset));
  if (!std::isfinite(result)) {
    malformed("Nonfinite character physics value");
  }
  return result;
}
math::Vector3<float> vector(const Reader& reader, std::uint64_t offset) {
  return {scalar(reader, offset), scalar(reader, offset + kWordBytes),
          scalar(reader, offset + 2 * kWordBytes)};
}
CharacterPhysicsBody read_body(const Reader& reader) {
  constexpr std::uint64_t kMass = 4, kGeometry = 8, kOffset = 20;
  CharacterPhysicsBody body;
  body.mass = scalar(reader, kMass);
  if (body.mass <= 0) {
    unsupported("Character body needs positive authored mass");
  }
  constexpr float kLinearDamping = .3F, kAngularDamping = .5F,
                  kSkinWidth = .01F;
  body.linear_damping = kLinearDamping;
  body.angular_damping = kAngularDamping;
  body.skin_width = kSkinWidth;
  body.local_center = vector(reader, kOffset);
  const auto kind = reader.u32(0);
  if (kind == 0) {
    body.geometry = CharacterPhysicsBody::Geometry::sphere;
    body.radius = scalar(reader, kGeometry);
    if (body.radius <= 0) {
      malformed("Invalid character sphere radius");
    }
  } else if (kind == 1 || kind == 2) {
    body.geometry = CharacterPhysicsBody::Geometry::box;
    const auto geometry = vector(reader, kGeometry);
    constexpr float kHalf = 0.5F;
    body.half_extent = kind == 1
                           ? geometry
                           : math::Vector3<float>{geometry[0] * kHalf,
                                                  geometry[1], geometry[1]};
    if (std::ranges::any_of(body.half_extent, [](float x) { return x <= 0; })) {
      malformed("Invalid character box extent");
    }
    body.local_center[0] += body.half_extent[0];
  } else {
    unsupported("Unknown character body geometry");
  }
  return body;
}
}  // namespace
std::string_view CharacterPhysicsBody::bone_name() const {
  return {bone.data(),
          static_cast<std::size_t>(std::find(bone.begin(), bone.end(), '\0') -
                                   bone.begin())};
}
std::optional<CharacterPhysicsRecipe> read_character_physics_recipe(
    std::span<const std::uint8_t> executable, std::string_view model_prefix) {
  const Image image(executable);
  constexpr std::uint64_t kPrefixCount = 16, kPrefixBytes = 8;
  const auto prefixes = image.at({kPrefixTable, kPrefixCount * kPrefixBytes});
  std::uint32_t recipe_address{};
  for (std::uint64_t i = 0; i < kPrefixCount; ++i) {
    const auto name = prefixes.u32(i * kPrefixBytes);
    if (name == 0) {
      break;
    }
    if (prefix_matches(image.name(name), model_prefix)) {
      recipe_address = prefixes.u32(i * kPrefixBytes + kWordBytes);
      break;
    }
  }
  if (recipe_address == 0) {
    return std::nullopt;
  }
  constexpr std::uint64_t kRecipeBytes = 24;
  const auto recipe = image.at({recipe_address, kRecipeBytes});
  // Signatures distinguish the known native layout from another executable.
  if (!image.name(recipe.u32(0)).starts_with("RAGDOLL_")) {
    unsupported("Unknown character physics recipe layout");
  }
  CharacterPhysicsRecipe result;
  result.body_count = recipe.u32(kRecipeBodyCount);
  result.joint_count = recipe.u32(kRecipeJointCount);
  if (result.body_count == 0 ||
      result.body_count > character_physics_capacity ||
      result.joint_count > character_physics_capacity) {
    malformed("Character physics recipe exceeds native capacity");
  }
  const auto bones =
      image.at({recipe.u32(kRecipeBones), result.body_count * kWordBytes});
  const auto bodies =
      image.at({recipe.u32(kRecipeBodies), result.body_count * kRecordBytes});
  const auto joints =
      image.at({recipe.u32(kRecipeJoints), result.joint_count * kRecordBytes});
  for (std::size_t i = 0; i < result.body_count; ++i) {
    auto& body = result.bodies.at(i);
    body = read_body({bodies.b.subspan(i * kRecordBytes, kRecordBytes)});
    const auto bone = image.name(bones.u32(i * kWordBytes));
    if (bone.empty()) {
      malformed("Empty character physics bone");
    }
    std::ranges::copy(bone, body.bone.begin());
  }
  for (std::size_t i = 0; i < result.joint_count; ++i) {
    const Reader record{joints.b.subspan(i * kRecordBytes, kRecordBytes)};
    auto& joint = result.joints.at(i);
    const auto kind = record.u32(0);
    if (kind > 1) {
      unsupported("Unknown character joint kind");
    }
    joint.kind = static_cast<CharacterPhysicsJoint::Kind>(kind);
    joint.first = record.u32(kJointFirst);
    joint.second = record.u32(kJointSecond);
    joint.group = record.u32(kJointGroup);
    joint.axis = vector(record, kJointAxis);
    constexpr float kRadiansPerDegree = std::numbers::pi_v<float> / 180;
    constexpr float kAnkleLimit = 30 * kRadiansPerDegree;
    constexpr float kLimbLimit = 150 * kRadiansPerDegree;
    constexpr float kTwistLimit = 40 * kRadiansPerDegree;
    constexpr float kSwingLimit = 60 * kRadiansPerDegree;
    constexpr std::uint32_t kAnkleGroup = 2;
    constexpr float kLimitResponse = .5F, kTwistStiffness = .5F,
                    kTwistDamping = 20;
    constexpr float kSwingStiffness = 2, kSwingDamping = 40;
    joint.limit_restitution = kLimitResponse;
    joint.limit_hardness = kLimitResponse;
    if (joint.kind == CharacterPhysicsJoint::Kind::hinge) {
      joint.minimum_angle = joint.group == kAnkleGroup ? -kAnkleLimit : 0;
      joint.maximum_angle =
          joint.group == kAnkleGroup ? kAnkleLimit : kLimbLimit;
    } else {
      joint.minimum_angle = -kTwistLimit;
      joint.maximum_angle = kTwistLimit;
      joint.swing_angle = kSwingLimit;
      joint.twist_stiffness = kTwistStiffness;
      joint.twist_damping = kTwistDamping;
      joint.swing_stiffness = kSwingStiffness;
      joint.swing_damping = kSwingDamping;
    }
    if (joint.first >= result.body_count || joint.second >= result.body_count ||
        joint.first == joint.second || math::dot(joint.axis, joint.axis) == 0 ||
        scalar(record, kJointDirectionW) != 0) {
      malformed("Invalid character joint topology or direction");
    }
  }
  return result;
}
ScenePhysicsRecipe read_scene_physics_recipe(
    std::span<const std::uint8_t> executable) {
  const Image image(executable);
  constexpr std::uint32_t kGravity = 0x9658e8, kMaterials = 0x9656f0;
  constexpr std::size_t kMaterialStride = 20, kGravityBytes = 12;
  constexpr std::size_t kRestitutionOffset = 8, kStaticFrictionOffset = 12,
                        kDynamicFrictionOffset = 16;
  constexpr float kDefaultRestitution = .2F, kDefaultStaticFriction = .7F,
                  kDefaultDynamicFriction = .5F;
  const auto table = image.at({kMaterials, 13 * kMaterialStride});
  ScenePhysicsRecipe result;
  result.gravity = vector(image.at({kGravity, kGravityBytes}), 0);
  // Source configures material zero through virtual setters, before the table.
  result.materials[0] = {.restitution = kDefaultRestitution,
                         .static_friction = kDefaultStaticFriction,
                         .dynamic_friction = kDefaultDynamicFriction};
  for (std::size_t i = 1; i < result.materials.size(); ++i) {
    const auto offset = (i - 1) * kMaterialStride;
    const auto index = table.u32(offset);
    auto& material = result.materials.at(i);
    if (index != i) {
      malformed("Unexpected scene material table order");
    }
    material.combine_mode = table.u32(offset + 4);
    material.restitution = scalar(table, offset + kRestitutionOffset);
    material.static_friction = scalar(table, offset + kStaticFrictionOffset);
    material.dynamic_friction = scalar(table, offset + kDynamicFrictionOffset);
    if (material.combine_mode > 3 || material.restitution < 0 ||
        material.restitution > 1 || material.static_friction < 0 ||
        material.dynamic_friction < 0) {
      malformed("Invalid scene material coefficients");
    }
  }
  return result;
}
}  // namespace bw::game
