// Alicia-specific immutable chain topology and six body contacts. The engine
// owns integration; source compatibility stays here. Header contracts cite
// both MCP phases. Table recovered from the hash-bound source image at
// 0x965cd8/0x965c00; provenance and decoding are in
// costume-secondary-motion.md.
#include "alicia_secondary_motion.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace bw::game {
namespace {
using animation::SecondaryNodeDefinition;
using animation::SecondaryNodeKind;
constexpr std::array<SecondaryNodeDefinition, 111> kNodes{{
    {SecondaryNodeKind::frame, 6, -1, -1, 9, -1, -1, 3, 0.009999999776482582F},
    {SecondaryNodeKind::frame, 7, -1, -1, 10, -1, -1, 3, 0.009999999776482582F},
    {SecondaryNodeKind::frame, 8, -1, -1, 11, -1, -1, 3, 0.009999999776482582F},
    {SecondaryNodeKind::frame, 9, -1, -1, 12, -1, -1, 3, 0.009999999776482582F},
    {SecondaryNodeKind::frame, 10, -1, -1, 13, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, 11, -1, -1, 14, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, 12, -1, -1, 15, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, 13, -1, -1, 16, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, 14, -1, -1, 17, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 0, 18, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 1, 19, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 2, 20, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 3, 21, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 4, 22, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 5, 23, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 6, 24, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 7, 25, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 8, 26, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 109, 9, 27, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 110, 10, 28, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 111, 11, 29, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 112, 12, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 113, 13, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 114, 14, 30, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 115, 15, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 116, 16, 31, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 117, 17, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 18, 32, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 19, 33, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 20, 34, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 23, 35, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 25, 36, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 118, 27, 37, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 119, 28, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 120, 29, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 121, 30, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 122, 31, 38, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 32, 39, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::midpoint, -1, -1, 36, 40, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 123, 37, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, -1, 124, 38, -1, -1, -1, 3,
     0.009999999776482582F},
    {SecondaryNodeKind::frame, 0, 85, -1, 51, -1, 42, 12, 0.05000000074505806F},
    {SecondaryNodeKind::frame, 15, -1, -1, 52, 41, 43, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, 1, 86, -1, 53, 42, 44, 12, 0.05000000074505806F},
    {SecondaryNodeKind::frame, 16, -1, -1, 54, 43, 45, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, 2, 87, -1, 55, 44, -1, 12, 0.05000000074505806F},
    {SecondaryNodeKind::frame, 3, 88, -1, 56, -1, 47, 48, 0.05000000074505806F},
    {SecondaryNodeKind::frame, 17, -1, -1, 57, 46, 48, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, 4, 89, -1, 58, 47, 49, 48, 0.05000000074505806F},
    {SecondaryNodeKind::frame, 18, -1, -1, 59, 48, 50, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, 5, 90, -1, 60, 49, -1, 48, 0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 41, 61, -1, 52, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 42, 62, 51, 53, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 43, 63, 52, 54, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 44, 64, 53, 55, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 45, 65, 54, -1, 12,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 46, 66, -1, 57, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 47, 67, 56, 58, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 48, 68, 57, 59, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 49, 69, 58, 60, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 50, 70, 59, -1, 48,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 91, 51, 71, -1, 62, 8, 0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 52, 72, 61, 63, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 92, 53, 73, 62, 64, 8, 0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 54, 74, 63, 65, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 93, 55, 75, 64, -1, 8, 0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 94, 56, 76, -1, 67, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 57, 77, 66, 68, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 95, 58, 78, 67, 69, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 59, 79, 68, 70, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 96, 60, 80, 69, -1, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 61, 81, -1, 72, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 62, 82, 81, 73, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 63, 83, 72, 74, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 64, 84, 73, 75, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 65, 85, 74, -1, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 66, 86, -1, 77, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 67, 87, 76, 78, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 68, 88, 77, 79, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 69, 89, 78, 90, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 70, 90, 79, -1, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 97, 71, 91, -1, 82, 8, 0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 72, 92, 81, 83, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 98, 73, 93, 82, 84, 8, 0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 74, 94, 83, 85, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 99, 75, 95, 84, -1, 8, 0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 100, 76, 96, -1, 87, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 77, 97, 86, 88, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 101, 78, 98, 87, 89, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 79, 99, 88, 90, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 102, 80, 100, 89, -1, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 81, 101, -1, 92, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 82, 102, 91, 93, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 83, 103, 92, 94, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 84, 104, 93, 95, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 85, 105, 94, -1, 8,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 86, 106, -1, 97, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 87, 107, 96, 98, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 88, 108, 97, 99, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 89, 109, 98, 100, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::midpoint, -1, -1, 90, 110, 99, -1, 32,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 103, 91, -1, -1, 102, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 92, -1, 101, 103, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 104, 93, -1, 102, 104, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 94, -1, 103, 105, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 105, 95, -1, 104, -1, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 106, 96, -1, -1, 107, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 97, -1, 106, 108, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 107, 98, -1, 107, 109, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::adjacent_midpoint, -1, -1, 99, -1, 108, 110, 0,
     0.05000000074505806F},
    {SecondaryNodeKind::frame, -1, 108, 100, -1, 109, -1, 0,
     0.05000000074505806F},
}};
constexpr std::array<animation::SecondaryCapsuleDefinition, 6> kCapsules{{
    {9,
     {0.05000000074505806F, 0.02800000086426735F, 0.0F},
     {1.0F, 0.0F, 0.0F},
     0.05999999865889549F,
     0.07199999690055847F},
    {7,
     {0.019999999552965164F, 0.009999999776482582F, 0.0F},
     {0.0F, 0.0F, 1.0F},
     0.10000000149011612F,
     0.11999999731779099F},
    {20,
     {0.2199999988079071F, 0.0F, -0.05000000074505806F},
     {1.0F, 0.0F, 0.0F},
     0.1599999964237213F,
     0.1599999964237213F},
    {21,
     {0.20000000298023224F, 0.0F, -0.05000000074505806F},
     {1.0F, 0.0F, 0.0F},
     0.18000000715255737F,
     0.14000000059604645F},
    {23,
     {0.2199999988079071F, 0.0F, 0.05000000074505806F},
     {1.0F, 0.0F, 0.0F},
     0.1599999964237213F,
     0.1599999964237213F},
    {24,
     {0.20000000298023224F, 0.0F, 0.05000000074505806F},
     {1.0F, 0.0F, 0.0F},
     0.18000000715255737F,
     0.14000000059604645F},
}};
constexpr std::array<std::string_view, 40> kSecondaryNames{{
    "zcp_0", "zcp_1", "zcp_2", "zcp_3", "zcp_4", "zcp_5", "zr_00", "zr_01",
    "zr_02", "zr_03", "zr_04", "zr_05", "zr_10", "zr_11", "zr_12", "zr_13",
    "zr_14", "zr_15", "zr_20", "zr_21", "zr_22", "zr_23", "zr_24", "zr_25",
    "zr_60", "zr_61", "zr_62", "zr_63", "zr_64", "zr_65", "zr_66", "zr_67",
    "zr_68", "zr_70", "zr_71", "zr_72", "zr_75", "zr_77", "zr_80", "zr_87",
}};
std::size_t bone_count(const formats::ModelDefinition& model) {
  if (!model.skeleton()) {
    throw std::invalid_argument("Alicia secondary motion requires a skeleton");
  }
  return model.skeleton()->bones().size();
}
void require_name(const formats::ModelDefinition& model, std::size_t bone,
                  std::string_view expected) {
  const auto found = std::ranges::find(model.bone_names(), bone,
                                       &formats::ModelBoneName::bone);
  if (found == model.bone_names().end() || found->name != expected) {
    throw std::invalid_argument("Unsupported Alicia secondary bone layout");
  }
}
}  // namespace
AliciaSecondaryMotion::AliciaSecondaryMotion(
    const formats::ModelDefinition& model, std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain)
    : control_points_(formats::ControlPointDefinition::parse(
          bytes, bone_count(model), domain)),
      frames_(control_points_.frames().size(), animation::kIdentityMatrix,
              memory::Allocator<animation::Matrix>(domain)),
      motion_(kNodes, kCapsules, bone_count(model), frames_.size(), domain) {
  constexpr std::size_t kSecondaryStart = 85;
  for (std::size_t index = 0; index < kSecondaryNames.size(); ++index) {
    require_name(model, kSecondaryStart + index, kSecondaryNames.at(index));
  }
  constexpr std::array<std::string_view, 6> kBodyNames{
      "Head", "Spine3", "LeftUpLeg", "LeftLeg", "RightUpLeg", "RightLeg"};
  for (std::size_t index = 0; index < kCapsules.size(); ++index) {
    require_name(model, kCapsules.at(index).bone, kBodyNames.at(index));
  }
  for (const auto& node : kNodes) {
    if (node.control_point >= 0 &&
        !control_points_.frames()
             .at(static_cast<std::size_t>(node.control_point))
             .present) {
      throw std::invalid_argument(
          "Alicia costume lacks secondary control point");
    }
  }
}
void AliciaSecondaryMotion::reset(const animation::SkeletonPose& bind_pose) {
  animation::evaluate_control_points(control_points_,
                                     bind_pose.skin_transforms(), frames_);
  motion_.reset(frames_, bind_pose.bone_transforms());
  elapsed_ = 0;
  first_ = true;
}
std::span<const animation::BoneWorldOverride> AliciaSecondaryMotion::advance(
    float dt, float ground, const animation::SkeletonPose& pose) {
  if (!std::isfinite(dt) || dt < 0 || !std::isfinite(elapsed_ + dt)) {
    throw std::invalid_argument("Invalid Alicia secondary clock");
  }
  // Pose publication without consumed game time must not integrate velocity.
  // The source inverse-time fallback belongs to an actual simulation update.
  if (dt == 0) return motion_.overrides();
  animation::evaluate_control_points(control_points_, pose.skin_transforms(),
                                     frames_);
  motion_.advance(dt, elapsed_, ground, frames_, pose.bone_transforms(),
                  first_);
  elapsed_ += dt;
  first_ = false;
  return motion_.overrides();
}
}  // namespace bw::game
