// Public skeletal contracts with authored in-memory data: vector/quaternion
// sampling, preserved constants, hierarchy/inverse bind, clock boundaries and
// failure rollback. No file/process/native acceptance. Fixture storage uses
// the test process heap; production preparation/pose storage is accounted by
// the injected domain. Both-phase I.11/CDSA.32/CACHE.4 and implementation
// SL.con.3 follow the same ownership and validation contracts as pose.hpp.
#include <cmath>
#include <limits>
#include <memory_resource>
#include <stdexcept>

#include "../../../fixtures/model_fixture.hpp"
#include "bw/animation/clip_catalog.hpp"
#include "bw/animation/playback_ring.hpp"
#include "bw/animation/pose.hpp"

namespace {
using bw::resources::Bytes;
using model_fixture::word;
class PoseBacking final : public bw::memory::CpuBacking {
 public:
  std::size_t allocations{};
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      ++allocations;
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
bool near(float left, float right) { return std::abs(left - right) < 0.0001F; }
template <class Function>
void rejects(Function function) {
  bool rejected = false;
  try {
    function();
  } catch (const std::exception&) {
    rejected = true;
  }
  require(rejected, "Invalid skeletal input must reject");
}
void scalar(Bytes& bytes, std::size_t offset, float value) {
  word(bytes, offset, std::bit_cast<std::uint32_t>(value));
}
Bytes skeleton_bytes() {
  Bytes bytes(216);
  word(bytes, 0, 0x424c425f);
  word(bytes, 4, 2);
  // Child precedes its parent in storage, exercising prepared topology.
  word(bytes, 12, 1);
  word(bytes, 116, 0xffffffff);
  for (const auto bone : {8U, 112U}) {
    for (const auto transform : {24U, 64U}) {
      scalar(bytes, bone + transform + 12, 1);
      for (std::size_t axis = 0; axis < 3; ++axis) {
        scalar(bytes, bone + transform + 28 + axis * 4, 1);
      }
    }
  }
  scalar(bytes, 8 + 24 + 16, 2);
  scalar(bytes, 8 + 64 + 16, 2);
  return bytes;
}
Bytes clip_bytes(bool repeating) {
  Bytes bytes(224);
  word(bytes, 0, 0x00414237);
  scalar(bytes, 8, 10);
  scalar(bytes, 12, 10);
  word(bytes, 16, repeating ? 1 : 0);
  word(bytes, 20, 2);
  word(bytes, 24, 0xffffffff);  // Child absent: retain bind translation.
  word(bytes, 28, 0);
  word(bytes, 40, 48);
  scalar(bytes, 48 + 28, 1);  // Quaternion w constant.
  for (const auto axis : {0U, 1U, 2U}) scalar(bytes, 48 + 4 + axis * 4, 1);
  word(bytes, 48 + 52, 104);  // Translation channel; source order is S/R/T.
  word(bytes, 104, 0x46000000);
  word(bytes, 104 + 8, 2);
  word(bytes, 104 + 12, 12);
  word(bytes, 104 + 24, 136);
  word(bytes, 104 + 28, 144);
  scalar(bytes, 140, 10);
  scalar(bytes, 144 + 12, 10);
  word(bytes, 48 + 48, 168);  // Quaternion channel.
  word(bytes, 168, 0x48000000);
  word(bytes, 176, 2);
  word(bytes, 180, 8);
  word(bytes, 192, 136);
  word(bytes, 196, 200);
  // Identity -> negative identity: shortest path must remain identity.
  bytes.at(207) = 0x3c;
  bytes.at(215) = 0xbc;
  return bytes;
}
Bytes whole_clip_bytes() {
  Bytes bytes(168);
  word(bytes, 0, 0x4241425f);
  scalar(bytes, 8, 10);
  scalar(bytes, 12, 10);
  word(bytes, 20, 2);
  word(bytes, 24, 0xffffffff);
  word(bytes, 28, 0);
  word(bytes, 40, 48);
  word(bytes, 48, 0x46000000);
  word(bytes, 56, 2);
  word(bytes, 68, 80);
  word(bytes, 72, 88);
  scalar(bytes, 84, 10);
  // Whole transforms store Q/S/T, unlike the separate-track constant S/Q/T.
  scalar(bytes, 100, 1);
  scalar(bytes, 128 + 8, 1);  // Half-turn around Z at the final key.
  for (const auto sample : {88U, 128U}) {
    scalar(bytes, sample + 16, 1);
    scalar(bytes, sample + 20, 1);
    scalar(bytes, sample + 24, 1);
  }
  scalar(bytes, 128 + 28, 4);
  return bytes;
}
struct LayerFixture {
  std::uint32_t child_mask{3}, root_mask{1};
  float child_x{2}, root_x{5}, child_scale{1};
  std::array<float, 4> child_rotation{0, 0, 0, 1};
  bool absent_root{};
};
Bytes layer_clip_bytes(const LayerFixture& fixture) {
  Bytes bytes(160);
  word(bytes, 0, 0x00414237);
  scalar(bytes, 8, 60);
  scalar(bytes, 12, 60);
  word(bytes, 20, 2);
  word(bytes, 24, 0);
  word(bytes, 28, fixture.absent_root ? 0xffffffffU : 1);
  word(bytes, 32, fixture.child_mask);
  word(bytes, 36, fixture.root_mask);
  word(bytes, 40, 48);
  word(bytes, 44, 104);
  for (const auto record : {48U, 104U}) {
    for (const auto axis : {0U, 1U, 2U}) {
      scalar(bytes, record + 4 + axis * 4, 1);
    }
    scalar(bytes, record + 28, 1);
  }
  for (std::size_t axis = 0; axis < 4; ++axis) {
    scalar(bytes, 64 + axis * 4, fixture.child_rotation.at(axis));
  }
  scalar(bytes, 80, fixture.child_x);
  scalar(bytes, 136, fixture.root_x);
  scalar(bytes, 52, fixture.child_scale);
  return bytes;
}
void layered_pose_tests(const std::shared_ptr<PoseBacking>& backing,
                        const std::shared_ptr<bw::memory::Domain>& domain) {
  using bw::animation::PoseContribution;
  using bw::animation::PoseLayer;
  const auto skeleton_data = skeleton_bytes();
  const auto skeleton =
      bw::formats::SkeletonDefinition::parse(skeleton_data, domain);
  const auto prepare = [&](const LayerFixture& fixture) {
    const auto bytes = layer_clip_bytes(fixture);
    return bw::animation::PreparedClip::prepare(
        bw::formats::MotionDefinition::parse(bytes, domain), skeleton, domain);
  };
  const auto base = prepare({});
  const auto upper_from =
      prepare({.child_mask = 2, .root_mask = 0, .child_x = 6});
  const auto upper_to =
      prepare({.child_mask = 2, .root_mask = 0, .child_x = 10});
  const auto excluded = prepare({.child_mask = 0, .root_mask = 0});
  const float half_sqrt = std::sqrt(.5F);
  const auto delta_x = prepare({.child_mask = 4,
                                .root_mask = 4,
                                .child_x = 1,
                                .child_scale = 2,
                                .child_rotation = {half_sqrt, 0, 0, half_sqrt},
                                .absent_root = true});
  const auto delta_y = prepare({.child_mask = 4,
                                .root_mask = 4,
                                .child_x = 2,
                                .child_scale = 3,
                                .child_rotation = {0, half_sqrt, 0, half_sqrt},
                                .absent_root = true});
  bw::animation::SkeletonPose pose(skeleton, domain);
  const std::array base_chain{PoseContribution{&base, 0, .2F}};
  const std::array upper_chain{PoseContribution{&upper_from, 0},
                               PoseContribution{&upper_to, 0, .25F}};
  const std::array ordinary{PoseLayer{0, base_chain},
                            PoseLayer{1, upper_chain}};
  const auto allocations = backing->allocations;
  pose.evaluate_layers({.ordinary = ordinary, .additive = {}});
  require(
      near(pose.local_transforms()[0].translation[0], 7) &&
          near(pose.local_transforms()[1].translation[0], 5) &&
          near(pose.bone_transforms()[0][3][0], 12),
      "Authored channel mask and oldest-to-newest chain preserve lower body");
  const std::array additive_chain{PoseContribution{&delta_x, 0, 0},
                                  PoseContribution{&delta_y, 0, .2F}};
  const std::array additive{PoseLayer{2, additive_chain}};
  pose.evaluate_layers({.ordinary = ordinary, .additive = additive});
  const auto& child = pose.local_transforms()[0];
  require(near(child.translation[0], 10) && near(child.scale[0], 6) &&
              near(child.rotation[0], .5F) && near(child.rotation[1], .5F) &&
              near(child.rotation[2], -.5F) && near(child.rotation[3], .5F) &&
              near(pose.local_transforms()[1].translation[0], 5),
          "Additive delta product is ordered, ignores weights, skips absent "
          "tracks");
  const std::array masked_chain{PoseContribution{&excluded, 0},
                                PoseContribution{&upper_to, 0, .25F}};
  const std::array masked{PoseLayer{0, base_chain}, PoseLayer{1, masked_chain}};
  pose.evaluate_layers({.ordinary = masked, .additive = {}});
  require(
      near(pose.local_transforms()[0].translation[0], 4.5F) &&
          near(pose.local_transforms()[0].scale[0], 1.25F),
      "First admitted newer contributor retains original weighted-add branch");
  require(backing->allocations == allocations,
          "Layer evaluation does not allocate after preparation");
  const auto previous = pose.bone_transforms()[0];
  const std::array unordered{ordinary[1], ordinary[0]};
  rejects(
      [&] { pose.evaluate_layers({.ordinary = unordered, .additive = {}}); });
  const std::array invalid_chain{PoseContribution{nullptr, 0}};
  const std::array invalid{PoseLayer{0, invalid_chain}};
  rejects([&] { pose.evaluate_layers({.ordinary = invalid, .additive = {}}); });
  require(pose.bone_transforms()[0] == previous,
          "Layer rejection retains the entire published hierarchy");

  // The hierarchy root is stored after its child in this fixture. Removing
  // its full transform must preserve the child's authored local animation.
  auto consumed_world = bw::animation::kIdentityMatrix;
  consumed_world[0] = {0, 0, -1, 0};
  consumed_world[2] = {1, 0, 0, 0};
  consumed_world[3][0] = 100;
  pose.evaluate_layers({.ordinary = ordinary, .additive = additive},
                       consumed_world, bw::animation::RootPosePolicy::identity);
  require(pose.bone_transforms()[1] == consumed_world &&
              near(pose.local_transforms()[0].translation[0], 10) &&
              near(pose.bone_transforms()[0][3][2], -10) &&
              backing->allocations == allocations,
          "Consumed root transform appears once while child layers survive");
  bw::animation::PoseReleaseState release(2, domain);
  const auto release_allocations = backing->allocations;
  auto actor_world = bw::animation::kIdentityMatrix;
  actor_world[3][0] = 100;
  const std::array basis_override{
      bw::animation::BoneLocalBasisOverride{.bone = 0}};
  pose.evaluate_layers({.ordinary = ordinary, .additive = additive},
                       actor_world, bw::animation::RootPosePolicy::animated,
                       basis_override, {&release, 0, 3});
  require(
      near(release.weight(), 1) &&
          near(pose.local_transforms()[0].translation[0], 10),
      "Higher channel snapshots composed local pose before basis overrides");
  const std::array base_only{PoseLayer{0, base_chain}};
  actor_world[3][0] = 200;
  pose.evaluate_layers({.ordinary = base_only, .additive = {}}, actor_world,
                       bw::animation::RootPosePolicy::animated, {},
                       {&release, 1.0 / 6.0, 3});
  require(near(release.weight(), .5F) &&
              near(pose.local_transforms()[0].translation[0], 6) &&
              near(pose.local_transforms()[0].scale[0], 3.5F) &&
              near(pose.local_transforms()[1].translation[0], 5) &&
              near(pose.bone_transforms()[0][3][0], 211) &&
              std::abs(pose.local_transforms()[0].rotation[0]) > .1F,
          "Release decays first and blends only saved mask in local space");
  const auto released_pose = pose.bone_transforms()[0];
  const std::array invalid_basis{
      bw::animation::BoneLocalBasisOverride{.bone = 0},
      bw::animation::BoneLocalBasisOverride{.bone = 0}};
  rejects([&] {
    pose.evaluate_layers({.ordinary = ordinary, .additive = {}}, actor_world,
                         bw::animation::RootPosePolicy::animated, invalid_basis,
                         {&release, 0, 3});
  });
  require(
      near(release.weight(), .5F) && pose.bone_transforms()[0] == released_pose,
      "Failed active snapshot refresh retains pose and release state");
  pose.evaluate_layers({.ordinary = base_only, .additive = {}}, actor_world,
                       bw::animation::RootPosePolicy::animated, {},
                       {&release, 1, 3});
  require(release.weight() == 0 &&
              near(pose.local_transforms()[0].translation[0], 2),
          "Release reaches current pose without sampling a retired clip");
  pose.evaluate_layers({.ordinary = ordinary, .additive = {}}, actor_world,
                       bw::animation::RootPosePolicy::animated, {},
                       {&release, 0, 3});
  release.clear();
  pose.evaluate_layers({.ordinary = base_only, .additive = {}}, actor_world,
                       bw::animation::RootPosePolicy::animated, {},
                       {&release, 0, 3});
  require(release.weight() == 0 &&
              near(pose.local_transforms()[0].translation[0], 2) &&
              backing->allocations == release_allocations,
          "Explicit release reset and all evaluations use prepared storage");

  bw::animation::PoseReleaseState appearance_release(2, domain);
  bw::animation::PoseReleaseState incompatible_release(1, domain);
  pose.evaluate_layers({.ordinary = ordinary, .additive = {}}, actor_world,
                       bw::animation::RootPosePolicy::animated, {},
                       {&release, 0, 3});
  const auto copy_allocations = backing->allocations;
  appearance_release.copy_from(release);
  release.clear();
  rejects([&] { appearance_release.copy_from(incompatible_release); });
  require(appearance_release.weight() == 1,
          "Rejected snapshot copy retains independent appearance state");
  pose.evaluate_layers({.ordinary = base_only, .additive = {}}, actor_world,
                       bw::animation::RootPosePolicy::animated, {},
                       {&appearance_release, 1.0 / 6.0, 3});
  require(near(pose.local_transforms()[0].translation[0], 4.5F) &&
              near(appearance_release.weight(), .5F) && release.weight() == 0 &&
              backing->allocations == copy_allocations,
          "Appearance snapshot copies published locals into retained storage");
}
void playback_ring_tests(const std::shared_ptr<PoseBacking>& backing,
                         const std::shared_ptr<bw::memory::Domain>& domain) {
  using bw::animation::PlaybackRing;
  const auto skeleton_storage = skeleton_bytes();
  const auto skeleton =
      bw::formats::SkeletonDefinition::parse(skeleton_storage, domain);
  const auto clip = bw::animation::PreparedClip::prepare(
      bw::formats::MotionDefinition::parse(clip_bytes(false), domain), skeleton,
      domain);
  const auto loop = bw::animation::PreparedClip::prepare(
      bw::formats::MotionDefinition::parse(clip_bytes(true), domain), skeleton,
      domain);
  PlaybackRing ring(4, domain);
  const auto allocations = backing->allocations;
  const auto first = ring.request({.clip = &loop, .rate = 2});
  ring.advance(.125);
  const auto second =
      ring.request({.clip = &clip, .binding = 1, .blend_seconds = .5});
  ring.advance(.125);
  require(ring.state(first.slot).seconds == .5 &&
              ring.state(second.slot).seconds == .125 &&
              near(ring.state(second.slot).weight, .25F),
          "Outgoing and incoming rates remain independent of blend time");
  const auto alias =
      ring.request({.clip = &clip, .binding = 9, .channel = 2, .rate = -2});
  require(!alias.started && alias.slot == second.slot &&
              ring.state(second.slot).binding == 9 &&
              ring.state(second.slot).seconds == .125 &&
              ring.state(second.slot).rate == 1 &&
              ring.state(second.slot).channel == 0,
          "Resource alias without fresh changes identity without restart");
  const auto third = ring.request({.clip = &loop,
                                   .binding = 2,
                                   .channel = 1,
                                   .blend_seconds = 1,
                                   .fresh = true});
  const auto fourth = ring.request(
      {.clip = &clip, .binding = 3, .blend_seconds = 1, .fresh = true});
  require(ring.layers().size() == 2 &&
              ring.layers()[0].contributions.size() == 3 &&
              ring.layers()[1].contributions.size() == 1,
          "Four retained controllers publish ascending channels in age order");
  const auto fifth = ring.request(
      {.clip = &loop, .binding = 4, .blend_seconds = 1, .fresh = true});
  require(fifth.slot == first.slot && fifth.replaced &&
              fifth.replaced->generation ==
                  ring.state(second.slot).generation - 1 &&
              ring.layers()[0].contributions.size() == 3 &&
              ring.layers()[0].contributions.front().clip == &clip &&
              ring.layers()[0].contributions.back().clip == &loop &&
              ring.state(third.slot).linked && ring.state(fourth.slot).linked,
          "Ring reuse reports exact overwritten controller and retains others");
  ring.advance(1);
  require(
      ring.layers().size() == 1 && ring.layers()[0].contributions.size() == 1 &&
          ring.state(fifth.slot).linked && !ring.state(second.slot).linked &&
          !ring.state(third.slot).linked && !ring.state(fourth.slot).linked,
      "Newest full-weight controller obscures older owner entries");
  ring.clear();
  const auto reverse =
      ring.request({.clip = &loop, .start_seconds = .125, .rate = -2});
  ring.advance(.125);
  require(ring.state(reverse.slot).previous_seconds == .125 &&
              ring.state(reverse.slot).seconds == .875 &&
              ring.state(reverse.slot).previous_unwrapped_seconds == .125 &&
              ring.state(reverse.slot).unwrapped_seconds == -.125 &&
              ring.state(reverse.slot).completed,
          "Reverse clock exposes signed unwrapped interval across zero");
  ring.set_rate(reverse.slot, 1);
  ring.advance(.125);
  require(ring.state(reverse.slot).seconds == 0 &&
              ring.state(reverse.slot).unwrapped_seconds == 0 &&
              ring.state(reverse.slot).completed,
          "Repeating exact end wraps to zero without losing interval");
  ring.clear();
  const auto terminal =
      ring.request({.clip = &clip, .remove_on_completion = true});
  ring.advance(1);
  require(ring.state(terminal.slot).linked &&
              ring.state(terminal.slot).completed &&
              ring.layers()[0].contributions[0].seconds == 1,
          "Terminal pose survives until post-consumption retirement");
  ring.finish_frame();
  require(!ring.state(terminal.slot).linked && ring.layers().empty(),
          "Auto-removal runs only in finish_frame");
  ring.clear();
  const auto held = ring.request({.clip = &clip});
  ring.advance(2);
  ring.finish_frame();
  require(ring.state(held.slot).linked && ring.state(held.slot).seconds == 1 &&
              ring.state(held.slot).unwrapped_seconds == 1,
          "Nonrepeating hold clamps without invented idle transition");
  require(backing->allocations == allocations,
          "Playback requests, wrapping and retirement allocate no storage");
  const auto before = ring.state(held.slot);
  rejects([&] { ring.request({.clip = nullptr}); });
  rejects([&] { ring.request({.clip = &clip, .channel = 32}); });
  rejects([&] { ring.advance(std::numeric_limits<double>::infinity()); });
  rejects([&] {
    ring.set_rate(held.slot, std::numeric_limits<float>::quiet_NaN());
  });
  require(ring.state(held.slot).generation == before.generation &&
              ring.state(held.slot).seconds == before.seconds &&
              ring.layers()[0].contributions[0].clip == &clip,
          "Rejected requests and ticks preserve published playback");
  rejects([&] { PlaybackRing invalid(0, domain); });
  rejects([&] { PlaybackRing invalid(33, domain); });
}
}  // namespace
void skeletal_pose_tests() {
  auto backing = std::make_shared<PoseBacking>();
  auto domain = bw::memory::Domain::create(backing);
  layered_pose_tests(backing, domain);
  playback_ring_tests(backing, domain);
  {
    auto bytes = skeleton_bytes();
    const auto skeleton = bw::formats::SkeletonDefinition::parse(bytes, domain);
    bw::animation::SkeletonPose pose(skeleton, domain);
    require(near(pose.bone_transforms()[0][3][0], 2) &&
                near(pose.skin_transforms()[0][3][0], 0),
            "Parent-first bind pose composes inverse bind separately");
    auto motion_bytes = clip_bytes(false);
    const auto motion =
        bw::formats::MotionDefinition::parse(motion_bytes, domain);
    const auto clip =
        bw::animation::PreparedClip::prepare(motion, skeleton, domain);
    const auto allocations = backing->allocations;
    auto actor = bw::animation::kIdentityMatrix;
    actor[3][1] = 7;
    pose.evaluate(clip, 0.5, actor);
    require(near(pose.local_transforms()[1].translation[0], 5) &&
                near(pose.local_transforms()[1].rotation[3], 1) &&
                near(pose.bone_transforms()[0][3][0], 7) &&
                near(pose.bone_transforms()[0][3][1], 7) &&
                near(pose.skin_transforms()[0][3][0], 5),
            "Channel interpolation, missing track, attachment and skin poses");
    pose.evaluate(clip, 2, actor);
    require(near(pose.local_transforms()[1].translation[0], 10),
            "Nonrepeating clip clamps at its final frame");
    require(backing->allocations == allocations,
            "Pose evaluation reuses allocated storage");
    const auto previous = pose.bone_transforms()[0];
    rejects([&] { pose.evaluate(clip, -1); });
    actor[0][0] = std::numeric_limits<float>::infinity();
    rejects([&] { pose.evaluate(clip, 0, actor); });
    require(pose.bone_transforms()[0] == previous,
            "Rejected evaluation preserves published pose");
    motion_bytes = clip_bytes(true);
    const auto repeating = bw::animation::PreparedClip::prepare(
        bw::formats::MotionDefinition::parse(motion_bytes, domain), skeleton,
        domain);
    pose.evaluate(repeating, 1);
    require(near(pose.local_transforms()[1].translation[0], 0),
            "Repeating clip wraps at exact duration");
    require(
        !repeating.has_constant_local_transform(1) &&
            repeating.has_constant_local_transform(0),
        "Reference caching distinguishes varying and constant local tracks");
    require(near(repeating.end_transform(1).translation[0], 10),
            "Reference endpoint does not wrap a repeating clip");
    rejects([&] { (void)repeating.end_transform(repeating.bones()); });
    require(near(repeating.root_translation_delta(0.9, 1.1)[0], 2) &&
                near(repeating.root_translation_delta(0.25, 3.25)[0], 30) &&
                near(clip.root_translation_delta(0.9, 2)[0], 1),
            "Root displacement crosses cycles without wrapping backwards");
    const auto forward_root =
        repeating.root_relative_transform({0.9, 0.1, 0.2});
    const auto reverse_root =
        repeating.root_relative_transform({0.1, 0.9, -0.2});
    require(near(forward_root[3][0], 2) && near(reverse_root[3][0], -2) &&
                near(repeating.local_transform(1, 1).translation[0], 10),
            "Source relative root compensates one observed signed wrap");
    rejects([&] { (void)repeating.local_transform(1, 1.1); });
    rejects([&] {
      (void)repeating.root_relative_transform(
          {0, 0, std::numeric_limits<double>::infinity()});
    });
    pose.evaluate(repeating, 0.5, bw::animation::kIdentityMatrix, true);
    require(near(pose.local_transforms()[1].translation[0], 0) &&
                near(pose.local_transforms()[0].translation[0], 2),
            "Consumed root translation is locked without removing child pose");
    // Prepared data owns its values; mutating discarded source cannot change
    // it.
    scalar(motion_bytes, 144 + 12, 50);
    pose.evaluate(repeating, 0.5);
    require(near(pose.local_transforms()[1].translation[0], 5),
            "Prepared clip does not borrow mutable archive data");
    scalar(motion_bytes, 140, 0);
    rejects([&] {
      (void)bw::animation::PreparedClip::prepare(
          bw::formats::MotionDefinition::parse(motion_bytes, domain), skeleton,
          domain);
    });
    const auto whole_bytes = whole_clip_bytes();
    const auto whole = bw::animation::PreparedClip::prepare(
        bw::formats::MotionDefinition::parse(whole_bytes, domain), skeleton,
        domain);
    pose.evaluate(whole, 0.5);
    require(near(pose.local_transforms()[1].translation[0], 2) &&
                near(pose.local_transforms()[1].rotation[2], std::sqrt(0.5F)) &&
                near(pose.bone_transforms()[0][3][0], 2) &&
                near(pose.bone_transforms()[0][3][1], 2),
            "Whole-transform rotation turns the child's translated local axis");
    const auto turning_root = whole.root_relative_transform({0.5, 1, 0.5});
    require(near(turning_root[0][0], 0) && near(turning_root[0][1], 1) &&
                near(turning_root[3][0], 0) && near(turning_root[3][1], -2),
            "Relative root translation uses the previous rotated basis");
    pose.blend(0.5F, clip, 0, whole, 1);
    require(near(pose.local_transforms()[1].translation[0], 2) &&
                near(pose.local_transforms()[1].rotation[2], std::sqrt(0.5F)),
            "Transition blends local transforms before hierarchy composition");
    // Resetting a directional reference affects displayed children, while
    // leaving the immutable authored clip available for root extraction.
    const std::array basis{bw::animation::BoneLocalBasisOverride{.bone = 1}};
    pose.blend(0.5F, clip, 0, whole, 1, bw::animation::kIdentityMatrix, false,
               basis);
    require(
        near(pose.local_transforms()[1].translation[0], 2) &&
            near(pose.local_transforms()[1].rotation[3], 1) &&
            near(pose.bone_transforms()[0][3][0], 4) &&
            near(pose.bone_transforms()[0][3][1], 0) &&
            near(whole.end_transform(1).rotation[2], 1),
        "Local reference override follows blending without altering clip data");
    auto target = bw::animation::kIdentityMatrix;
    target[3] = {10, 7, 0, 1};
    const std::array world_overrides{
        bw::animation::BoneWorldOverride{.bone = 1, .world = target}};
    const auto modifier_allocations = backing->allocations;
    pose.apply_world_overrides(world_overrides);
    require(
        near(pose.bone_transforms()[0][3][0], 12) &&
            near(pose.bone_transforms()[0][3][1], 7) &&
            near(pose.skin_transforms()[0][3][0], 10),
        "World override preserves child local offset and recomputes skinning");
    const auto modified = pose.bone_transforms()[0];
    const std::array duplicate{world_overrides[0], world_overrides[0]};
    rejects([&] { pose.apply_world_overrides(duplicate); });
    auto invalid = world_overrides;
    invalid[0].world[3][0] = std::numeric_limits<float>::infinity();
    rejects([&] { pose.apply_world_overrides(invalid); });
    const std::array duplicate_basis{basis[0], basis[0]};
    rejects([&] {
      pose.evaluate(whole, 0, bw::animation::kIdentityMatrix, false,
                    duplicate_basis);
    });
    require(pose.bone_transforms()[0] == modified &&
                backing->allocations == modifier_allocations,
            "Rejected modifier batches retain valid output without allocation");
    const auto before_invalid_blend = pose.bone_transforms()[0];
    rejects([&] { pose.blend(2, clip, 0, whole, 0); });
    require(pose.bone_transforms()[0] == before_invalid_blend,
            "Invalid blend preserves the published pose");
    // Aliases mutate shared repeat policy in registration order, while each
    // publication is prepared once and remains independently addressable.
    const auto catalog_bytes = clip_bytes(false);
    const auto catalog_motion =
        bw::formats::MotionDefinition::parse(catalog_bytes, domain);
    const std::array<bw::animation::NamedClipSource, 2> catalog_sources{
        {{"motion", catalog_motion}, {"unbound", catalog_motion}}};
    const std::array<bw::animation::ClipAliasRequest, 3> catalog_aliases{
        {{2, "motion", false}, {1, "motion", true}, {3, "missing", false}}};
    const bw::animation::PreparedClipCatalog catalog(
        catalog_sources, skeleton, 4, catalog_aliases, domain);
    require(catalog.size() == 2 &&
                catalog.alias(1).clip == catalog.alias(2).clip &&
                !catalog.alias(2).requested_repeat && !catalog.raw_repeat(0) &&
                catalog.clip(0).repeating() && !catalog.clip(1).repeating() &&
                !catalog.alias(3).clip && catalog.find("unbound") == 1 &&
                !catalog.find("missing"),
            "Aliases preserve raw/requested/shared policy and absent targets");
    const auto lookup_allocations = backing->allocations;
    (void)catalog.find("motion");
    const auto catalog_clip = catalog.alias(1).clip;
    if (!catalog_clip) {
      throw std::runtime_error("Expected resolved catalog alias");
    }
    (void)catalog.clip(*catalog_clip);
    require(backing->allocations == lookup_allocations,
            "Prepared catalog queries do not allocate");
    const std::array<bw::animation::ClipAliasRequest, 2> duplicate_slots{
        {{0, "motion", true}, {0, "unbound", false}}};
    rejects([&] {
      (void)bw::animation::PreparedClipCatalog(catalog_sources, skeleton, 4,
                                               duplicate_slots, domain);
    });
    auto duplicate_names = catalog_sources;
    duplicate_names[1].name = "motion";
    rejects([&] {
      (void)bw::animation::PreparedClipCatalog(duplicate_names, skeleton, 4,
                                               catalog_aliases, domain);
    });
    // Shared clip keys use the new appearance's missing-track bind transform.
    auto replacement_bytes = skeleton_bytes();
    scalar(replacement_bytes, 8 + 24 + 16, 4);
    scalar(replacement_bytes, 8 + 64 + 16, 4);
    const auto replacement_skeleton =
        bw::formats::SkeletonDefinition::parse(replacement_bytes, domain);
    bw::animation::SkeletonPose replacement(replacement_skeleton, domain);
    replacement.evaluate(catalog.clip(0), 0.5);
    require(near(replacement.local_transforms()[0].translation[0], 4) &&
                near(replacement.local_transforms()[1].translation[0], 5),
            "Appearance defaults remain local while motion keys are shared");
    replacement.blend(0.5F, catalog.clip(0), 0.5, catalog.clip(1), 0.5);
    require(near(replacement.local_transforms()[0].translation[0], 4),
            "Blending shared clips also preserves appearance bind defaults");
    // A root, one corrected joint and a terminal marker witness the original
    // component-space conjugation and subsequent secondary recomposition.
    Bytes aim_bytes(8 + 3 * 104);
    word(aim_bytes, 0, 0x424c425f);
    word(aim_bytes, 4, 3);
    for (std::size_t bone = 0; bone < 3; ++bone) {
      const auto base = 8 + bone * 104;
      word(aim_bytes, base + 4,
           bone == 0 ? 0xffffffff : static_cast<std::uint32_t>(bone - 1));
      for (const auto transform : {24U, 64U}) {
        scalar(aim_bytes, base + transform + 12, 1);
        for (std::size_t axis = 0; axis < 3; ++axis) {
          scalar(aim_bytes, base + transform + 28 + axis * 4, 1);
        }
      }
    }
    scalar(aim_bytes, 8 + 104 + 24 + 20, 1);
    scalar(aim_bytes, 8 + 208 + 24 + 24, 1);
    const auto aim_skeleton =
        bw::formats::SkeletonDefinition::parse(aim_bytes, domain);
    bw::animation::SkeletonPose aim_pose(aim_skeleton, domain);
    const std::array<std::size_t, 2> aim_bones{1, 2};
    const auto aim_allocations = backing->allocations;
    aim_pose.evaluate_layers({}, bw::animation::kIdentityMatrix,
                             bw::animation::RootPosePolicy::identity, {}, {},
                             {aim_bones, {1, 0, 0}, 1});
    require(near(aim_pose.bone_transforms()[2][2][0], 1) &&
                near(aim_pose.bone_transforms()[2][3][0], 1) &&
                near(aim_pose.bone_transforms()[2][3][1], 1),
            "Aim rotates the terminal forward and child offset through local "
            "hierarchy");
    bw::animation::BoneWorldOverride root_override;
    root_override.world[3][1] = 2;
    aim_pose.apply_world_overrides(std::span(&root_override, 1));
    require(
        near(aim_pose.bone_transforms()[2][2][0], 1) &&
            near(aim_pose.bone_transforms()[2][3][1], 3),
        "Secondary publication retains aimed local matrices for descendants");
    require(backing->allocations == aim_allocations,
            "Aim hierarchy and override publish without allocation");
    const auto retained_aim = aim_pose.bone_transforms()[2];
    rejects([&] {
      aim_pose.evaluate_layers({}, bw::animation::kIdentityMatrix,
                               bw::animation::RootPosePolicy::identity, {}, {},
                               {aim_bones, {0, 0, 0}, 1});
    });
    require(aim_pose.bone_transforms()[2] == retained_aim,
            "Invalid aim batch preserves published pose");
    // Source407c90 retains marker magnitude and4091d0 retains quaternion
    // magnitude. Half-scale +Z aimed at +X yields terminal forward(.1,0,-.075),
    // rather than the normalized-input result(.5,0,0).
    scalar(aim_bytes, 8 + 208 + 24 + 36, .5F);
    const auto scaled_skeleton =
        bw::formats::SkeletonDefinition::parse(aim_bytes, domain);
    bw::animation::SkeletonPose scaled_pose(scaled_skeleton, domain);
    scaled_pose.evaluate_layers({}, bw::animation::kIdentityMatrix,
                                bw::animation::RootPosePolicy::identity, {}, {},
                                {aim_bones, {1, 0, 0}, 1});
    require(near(scaled_pose.bone_transforms()[2][2][0], .1F) &&
                near(scaled_pose.bone_transforms()[2][2][2], -.075F),
            "Scaled aim marker preserves original nonunit quaternion result");
    motion_bytes = clip_bytes(false);
    scalar(motion_bytes, 12, 0);
    rejects([&] {
      (void)bw::animation::PreparedClip::prepare(
          bw::formats::MotionDefinition::parse(motion_bytes, domain), skeleton,
          domain);
    });
  }
  require(domain->snapshot().live_bytes == 0,
          "Clip and pose destruction retires domain storage");
}
