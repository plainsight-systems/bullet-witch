// Prepare the original grounded slot-one receiver without creating an AI or
// interpreting absent actions as idle. Header owns both-phase I.11/R.14/MEM.9
// contracts: all parsing/mapping at load, immutable keys, caller-owned scratch.
#include "npc_initial_motion.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::game {
namespace {
bool is_shared_owner(std::string_view owner) {
  return owner == "hm0s" || owner == "hw0s" || owner == "hm2s";
}
bool is_weapon_owner(std::string_view owner) {
  return owner == "wg1" || owner == "wg2" || owner == "wg5" || owner == "wg7";
}
const formats::SkeletonDefinition& skeleton(
    const formats::ModelDefinition& model) {
  if (!model.skeleton()) {
    throw std::invalid_argument("NPC initial motion requires a skeleton");
  }
  return *model.skeleton();
}
const formats::MotionDefinition& initial_definition(
    const content::CharacterSupport& support, std::string_view owner,
    const std::shared_ptr<memory::Domain>& domain) {
  memory::String prefix("model.", memory::Allocator<char>(domain));
  prefix += owner;
  prefix += ".motion.";
  // Shared-human publications retain the three-letter model basename; the
  // fourth owner letter belongs to the shared bank, not to the clip label.
  memory::String basename{memory::Allocator<char>(domain)};
  if (is_weapon_owner(owner)) {
    // Activation selects slot zero before asking for its lowered slot. These
    // weapons have no lowered clip; the source retains defaultmotion.
    basename = "defaultmotion";
  } else {
    basename = owner.substr(0, 3);
    basename += "_stand_l";
  }
  memory::String compressed("compress.", memory::Allocator<char>(domain));
  compressed += basename;
  memory::String uncompressed("nocompress.", memory::Allocator<char>(domain));
  uncompressed += basename;
  const formats::MotionDefinition* found = nullptr;
  for (const auto& motion : support.motions) {
    const std::string_view name = motion.publication.name;
    if (!name.starts_with(prefix)) continue;
    const auto suffix = name.substr(prefix.size());
    if (suffix != uncompressed && suffix != compressed) continue;
    if (found != nullptr) {
      throw std::invalid_argument("Ambiguous NPC initial motion publication");
    }
    found = &motion.definition;
  }
  if (found == nullptr) {
    throw std::invalid_argument("Missing NPC original initial motion");
  }
  return *found;
}
void validate_direct_bones(const formats::ModelDefinition& target,
                           const formats::ModelDefinition& owner) {
  if (skeleton(target).bones().size() != skeleton(owner).bones().size() ||
      target.bone_names().size() != owner.bone_names().size() ||
      target.bone_names().size() != skeleton(target).bones().size()) {
    throw std::invalid_argument(
        "Direct NPC motion has incompatible bone count");
  }
  for (const auto& name : target.bone_names()) {
    const auto found = std::ranges::find(owner.bone_names(), name.name,
                                         &formats::ModelBoneName::name);
    if (found == owner.bone_names().end() || found->bone != name.bone) {
      throw std::invalid_argument(
          "Direct NPC motion has incompatible bone order");
    }
  }
}
}  // namespace

NpcInitialBodySettings NpcInitialBodySettings::parse(
    const formats::CustomizationDefinition& customization,
    std::string_view model_family) {
  constexpr std::array supported{
      std::string_view("ea0"), std::string_view("ea1"),
      std::string_view("ea2"), std::string_view("hm0"),
      std::string_view("hm1"), std::string_view("hm2"),
      std::string_view("hw0"), std::string_view("hw1"),
      std::string_view("hw2")};
  if (std::ranges::find(supported, model_family) == supported.end()) {
    throw std::invalid_argument("Untraced NPC initial body family");
  }
  const std::array path{std::string_view("witch"), std::string_view("actor"),
                        model_family, std::string_view("body")};
  const resources::detail::Reader reader{customization.lookup(path)};
  constexpr std::uint64_t kStandingHeight = 0x24, kAlternateHeight = 0x28,
                          kRadius = 0x2c;
  // The native body lookup preserves EA/HM/HW family identity; sharing the
  // animation bank does not alias the physical dimensions.
  NpcInitialBodySettings result{
      .radius = std::bit_cast<float>(reader.u32(kRadius)),
      .standing_height = std::bit_cast<float>(reader.u32(kStandingHeight)),
      .alternate_height = std::bit_cast<float>(reader.u32(kAlternateHeight))};
  if (!std::isfinite(result.radius) || !std::isfinite(result.standing_height) ||
      !std::isfinite(result.alternate_height) || result.radius <= 0 ||
      result.standing_height < 2 * result.radius ||
      result.alternate_height < 2 * result.radius) {
    throw std::invalid_argument("Invalid authored NPC body dimensions");
  }
  return result;
}

NpcInitialMotion NpcInitialMotion::prepare(
    const formats::ModelDefinition& target,
    const formats::ModelDefinition& owner,
    const content::CharacterSupport& support, std::string_view motion_owner,
    const std::shared_ptr<memory::Domain>& domain) {
  const bool shared = is_shared_owner(motion_owner);
  if (!shared && motion_owner != "ea0" && !is_weapon_owner(motion_owner)) {
    throw std::invalid_argument("Untraced NPC initial motion owner");
  }
  const auto& definition = initial_definition(support, motion_owner, domain);
  std::optional<SharedMotionPose> mapping;
  memory::Vector<formats::SkeletonTransform> direct_bind{
      memory::Allocator<formats::SkeletonTransform>(domain)};
  if (shared) {
    mapping.emplace(
        SharedMotionPose::prepare(skeleton(target), target.bone_names(),
                                  skeleton(owner), owner.bone_names(), domain));
  } else {
    validate_direct_bones(target, owner);
    direct_bind.reserve(skeleton(target).bones().size());
    for (const auto& bone : skeleton(target).bones()) {
      direct_bind.push_back(bone.relative);
    }
  }
  // Native initial slots explicitly request looping playback. Direct bindings
  // retain each target's bind values for absent tracks; shared humans sample
  // the source owner before applying their prepared map.
  auto clip = animation::PreparedClip::prepare(
      definition, skeleton(shared ? owner : target), domain, true);
  return {std::move(clip), std::move(mapping), std::move(direct_bind),
          is_weapon_owner(motion_owner) ? 0U : 1U};
}

void NpcInitialMotion::sample(
    double sampled_seconds,
    std::span<formats::SkeletonTransform> destination) const {
  if (mapping_) {
    mapping_->sample(clip_, sampled_seconds, destination);
    return;
  }
  if (destination.size() != clip_.bones() || !std::isfinite(sampled_seconds) ||
      sampled_seconds < 0 || sampled_seconds > clip_.duration_seconds()) {
    throw std::invalid_argument("Invalid NPC initial motion sample");
  }
  for (std::size_t bone = 0; bone < destination.size(); ++bone) {
    destination[bone] = (clip_.channel_mask(bone) & 1U) != 0
                            ? clip_.local_transform(bone, sampled_seconds)
                            : direct_bind_.at(bone);
  }
}
}  // namespace bw::game
