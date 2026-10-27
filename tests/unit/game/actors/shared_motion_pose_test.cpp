// Shared-motion public contracts using synthetic original-format skeletons
// and clips. No files, process launching or game assets. Exercise target bind
// retention, name mapping, translation/rotation correction, separate caller
// scratch and atomic hierarchy publication. Both-phase Core R.14/I.11 and
// performance MEM.9 match the production contracts; fixture bytes use the
// established test-only heap exception.
#include "game/actors/animation/shared_motion_pose.hpp"

#include <bit>
#include <limits>
#include <memory_resource>
#include <stdexcept>

#include "../../../fixtures/model_fixture.hpp"

namespace {
using bw::resources::Bytes;
using model_fixture::word;
class SharedMotionBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
void scalar(Bytes& bytes, std::size_t offset, float value) {
  word(bytes, offset, std::bit_cast<std::uint32_t>(value));
}
Bytes skeleton(bool target) {
  const std::size_t count = target ? 4 : 3;
  Bytes result(8 + count * 104);
  word(result, 0, 0x424c425f);
  word(result, 4, static_cast<std::uint32_t>(count));
  for (std::size_t bone = 0; bone < count; ++bone) {
    const auto offset = 8 + bone * 104;
    word(result, offset + 4, bone == 0 ? 0xffffffffU : 0);
    for (const auto transform : {24U, 64U}) {
      scalar(result, offset + transform + 12, 1);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        scalar(result, offset + transform + 28 + axis * 4, 1);
      }
      if (bone != 0) {
        scalar(result, offset + transform + 20,
               target ? static_cast<float>(bone + 1) : 1);
      }
      if (target && bone == 2) {
        scalar(result, offset + transform + 8, 1);
        scalar(result, offset + transform + 12, 0);
      }
    }
  }
  return result;
}
Bytes motion() {
  Bytes result(60 + 3 * 56);
  word(result, 0, 0x00414237);
  scalar(result, 8, 60);
  scalar(result, 12, 60);
  word(result, 16, 1);
  word(result, 20, 3);
  for (std::uint32_t bone = 0; bone < 3; ++bone) {
    const auto offset = 60 + bone * 56;
    word(result, 24 + bone * 4, bone);
    word(result, 36 + bone * 4, 1);
    word(result, 48 + bone * 4, offset);
    for (std::size_t axis = 0; axis < 3; ++axis) {
      scalar(result, offset + 4 + axis * 4, 1);
    }
    scalar(result, offset + 28, 1);
    if (bone != 0) scalar(result, offset + 36, 1);
  }
  return result;
}
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Malformed retarget request must reject");
}
}  // namespace

void shared_motion_pose_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<SharedMotionBacking>());
  const auto target_bytes = skeleton(true);
  const auto owner_bytes = skeleton(false);
  const auto motion_bytes = motion();
  const auto target =
      bw::formats::SkeletonDefinition::parse(target_bytes, domain);
  const auto owner =
      bw::formats::SkeletonDefinition::parse(owner_bytes, domain);
  const auto definition =
      bw::formats::MotionDefinition::parse(motion_bytes, domain);
  const auto clip =
      bw::animation::PreparedClip::prepare(definition, owner, domain);
  constexpr std::array<bw::formats::ModelBoneName, 4> target_names{
      {{"Hand", 2}, {"Root", 0}, {"HipsBone", 1}, {"Accessory", 3}}};
  constexpr std::array<bw::formats::ModelBoneName, 3> owner_names{
      {{"Root", 0}, {"HipsBone", 1}, {"Hand", 2}}};
  const auto mapping = bw::game::SharedMotionPose::prepare(
      target, target_names, owner, owner_names, domain);
  std::array<bw::formats::SkeletonTransform, 4> locals{};
  mapping.sample(clip, 0.5, locals, {0, 0.25F, 0});
  require(locals[1].translation[1] == 2.25F,
          "Mapped hips use original height ratio and explicit offset");
  require(locals[2].translation[1] == 3 && locals[2].rotation[2] == 1,
          "Mapped limb preserves target length and orientation");
  require(locals[3].translation == target.bones()[3].relative.translation,
          "Unmapped accessory retains target bind");
  auto other = locals;
  mapping.sample(clip, 0.5, other);
  require(other[1].translation[1] == 2 && locals[1].translation[1] == 2.25F,
          "Shared preparation never aliases mutable actor locals");
  bw::animation::SkeletonPose pose(target, domain);
  pose.evaluate_locals(other);
  const auto before = pose.bone_transforms()[1];
  other[1].translation[0] = std::numeric_limits<float>::infinity();
  rejects([&] { pose.evaluate_locals(other); });
  require(pose.bone_transforms()[1] == before,
          "Rejected local batch retains published pose");
  rejects([&] { mapping.sample(clip, -1, locals); });
  rejects([&] { mapping.sample(clip, 0, std::span(locals).first(3)); });
  auto duplicate = target_names;
  duplicate[0].bone = 0;
  rejects([&] {
    static_cast<void>(bw::game::SharedMotionPose::prepare(
        target, duplicate, owner, owner_names, domain));
  });
}
