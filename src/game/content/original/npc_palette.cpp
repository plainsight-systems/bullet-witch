// Build original NPC texture bindings from compact compatibility rules. Keep
// historical spelling and repeated entries: indexes are game-facing behavior.
// Missing original logical resources are skipped exactly as the native lookup;
// present resources require actual resident handles. No synthesized textures.
// Ownership, costs and both-phase guideline application are in npc_palette.hpp.
#include "npc_palette.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <stdexcept>
namespace bw::game {
namespace {
struct Rule {
  std::string_view family, material, variations;
};
constexpr auto kRules = std::to_array<Rule>({
    {"hm0", "HM0_bodyA_c1_m1",
     "HM0_bodyA_c1_m1 HM0_bodyA_c2_m1 HM0_bodyA_c3_m1 "
     "HM0_bodyA_c1_zombie_m1 HM0_bodyA_c2_zombie_m1 "
     "HM0_bodyA_c3_zombie_m1 HM0_body_Burnt_m1"},
    {"hm0", "HM0_bodyB_c1_m1",
     "HM0_bodyB_c1_m1 HM0_bodyB_c2_m1 HM0_bodyB_c3_m1 "
     "HM0_bodyB_c1_zombie_m1 HM0_bodyB_c2_zombie_m1 "
     "HM0_bodyB_c3_zombie_m1"},
    {"hm0", "HM0_pants_c1_m1",
     "HM0_pants_c1_m1 HM0_pants_c2_m1 HM0_pants_c3_m1 HM0_pantsB_c2_m1 "
     "HM0_pantsB_c3_m1 HM0_pants_Burnt_m1"},
    {"hm0", "HM0_shoes_c1_m1",
     "HM0_shoes_c1_m1 HM0_shoes_c2_m1 HM0_shoes_c3_m1 HM0_shoesB_c2_m1 "
     "HM0_shoesB_c3_m1 HM0_shoes_Burnt_m1"},
    {"hm0", "HM0_skinA_m1",
     "HM0_skinA_m1 HM0_skinB_m1 HM0_skinC_m1 HM0_skinD_m1 HM0_skinE_m1 "
     "HM0_skinF_m1 HM0_skin_zombie_m1 HM0_skin_Burnt_m1"},
    {"hm0", "HM0_faceA_m1", "HM0_faceA_m1 HM0_face_Burnt_m1"},
    {"hm0", "HM0_m1hair_m1", "HM0_m1hair_m1 HM0_m1hair_Burnt_m1"},
    {"hm0", "HM0_m1hairS_m1", "HM0_m1hairS_m1 HM0_m2hairS_m1 HM0_m3hairS_m1"},
    {"hm1", "HM1_bodyA_c1_m1",
     "HM1_bodyA_c1_m1 HM1_bodyA_c2_m1 HM1_bodyA_c3_m1 "
     "HM1_bodyA_c1_zombie_m1 HM1_bodyA_c2_zombie_m1 "
     "HM1_bodyA_c3_zombie_m1 HM1_body_Burnt_m1"},
    {"hm1", "HM1_bodyB_c1_m1",
     "HM1_bodyB_c1_m1 HM1_bodyB_c2_m1 HM1_bodyB_c3_m1 "
     "HM1_bodyB_c1_zombie_m1 HM1_bodyB_c2_zombie_m1 "
     "HM1_bodyB_c3_zombie_m1"},
    {"hm1", "HM1_footA_c1_m1",
     "HM1_footA_c1_m1 HM1_footA_c2_m1 HM1_footA_c3_m1 HM1_footB_c2_m1 "
     "HM1_footB_c3_m1 HM1_Foot_Burnt_m1"},
    {"hm1", "HM1_shoesA_c1_m1",
     "HM1_shoesA_c1_m1 HM1_shoesA_c2_m1 HM1_shoesA_c3_m1 "
     "HM1_shoesB_c2_m1 HM1_shoesB_c3_m1 HM1_shoes_Burnt_m1"},
    {"hm1", "HM1_skinA_m1",
     "HM1_skinA_m1 HM1_skinB_m1 HM1_skinC_m1 HM0_skin_zombie_m1 "
     "HM0_skin_Burnt_m1"},
    {"hm1", "HM1_faceA_m1", "HM1_faceA_m1 HM1_faceBurnt_m1"},
    {"hm1", "HM0_m1hairS_m1", "HM0_m1hairS_m1 HM0_m2hairS_m1 HM0_m3hairS_m1"},
    {"hm2", "HM2_bodyA_c1_m1",
     "HM2_bodyA_c1_m1 HM2_bodyA_c2_m1 HM2_bodyA_c3_m1 "
     "HM2_bodyA_c1_zombie_m1 HM2_bodyA_c2_zombie_m1 "
     "HM2_bodyA_c3_zombie_m1 HM2_body_Burnt_m1"},
    {"hm2", "HM2_bodyB_c1_m1",
     "HM2_bodyB_c1_m1 HM2_bodyB_c2_m1 HM2_bodyB_c3_m1 "
     "HM2_bodyB_c1_zombie_m1 HM2_bodyB_c2_zombie_m1 "
     "HM2_bodyB_c3_zombie_m1"},
    {"hm2", "HM2_foot_c1_m1",
     "HM2_foot_c1_m1 HM2_foot_c2_m1 HM2_foot_c3_m1 HM2_footB_c2_m1 "
     "HM2_footB_c3_m1 HM2_foot_Burnt_m1"},
    {"hm2", "HM2_boots_c1_m1",
     "HM2_boots_c1_m1 HM2_boots_c2_m1 HM2_boots_c3_m1 HM2_bootsB_c2_m1 "
     "HM2_bootsB_c3_m1 HM2_boots_Burnt_m1"},
    {"hm2", "HM2_skinA_m1",
     "HM2_skinA_m1 HM2_skinB_m1 HM2_skinC_m1 HM2_skin_zombie_m1 "
     "HM2_skin_Burnt_m1"},
    {"hm2", "HM2_faceA_m1", "HM2_faceA_m1 HM2_face_Burnt_m1"},
    {"hm2", "HM2_o1hair_m1", "HM2_o1hair_m1 HM2_o1hair_Burnt_m1"},
    {"hm2", "HM2_o1hairS_m1", "HM2_o1hairS_m1 HM2_o1hairS_m1"},
    {"hw0", "HW0_bodyA_c1_m1",
     "HW0_bodyA_c1_m1 HW0_bodyA_c2_m1 HW0_bodyA_c3_m1 "
     "HW0_bodyA_c1_zombie_m1 HW0_bodyA_c2_zombie_m1 "
     "HW0_bodyA_c3_zombie_m1 HW0_body_Burnt_m1"},
    {"hw0", "HW0_bodyB_c1_m1",
     "HW0_bodyB_c1_m1 HW0_bodyB_c2_m1 HW0_bodyB_c3_m1 "
     "HW0_bodyB_c1_zombie_m1 HW0_bodyB_c2_zombie_m1 "
     "HW0_bodyB_c3_zombie_m1"},
    {"hw0", "HW0_footA_c1_m1",
     "HW0_footA_c1_m1 HW0_footA_c2_m1 HW0_footA_c3_m1 HW0_footB_c2_m1 "
     "HW0_footB_c3_m1 HW0_foot_Burnt_m1"},
    {"hw0", "HW0_shoes_c1_m1",
     "HW0_shoes_c1_m1 HW0_shoes_c2_m1 HW0_shoes_c3_m1 HW0_shoesB_c2_m1 "
     "HW0_shoesB_c3_m1 HW0_shoes_Burnt_m1"},
    {"hw0", "HW0_skinA_m1",
     "HW0_skinA_m1 HW0_skinB_m1 HW0_skinC_m1 HW0_skinD_m1 "
     "HW0_skin_zombie_m1 HW0_skin_Burnt_m1"},
    {"hw0", "HW0_faceA_m1", "HW0_faceA_m1 HW0_face_Burnt_m1"},
    {"hw0", "HW0_w1hair_m1", "HW0_w1hair_m1 HW0_w1hair_Burnt_m1"},
    {"hw0", "HW0_m1hairS_m1", "HW0_m1hairS_m1 HW0_m2hairS_m1 HW0_m3hairS_m1"},
    {"hw1", "HW1_bodyA_c1_m1",
     "HW1_bodyA_c1_m1 HW1_bodyA_c2_m1 HW1_bodyA_c3_m1 "
     "HW1_bodyA_c1_zombie_m1 HW1_bodyA_c2_zombie_m1 "
     "HW1_bodyA_c3_zombie_m1 HW1_body_Burnt_m1"},
    {"hw1", "HW1_footA_c1_m1",
     "HW1_footA_c1_m1 HW1_footA_c2_m1 HW1_footA_c3_m1 "
     "HW1_footA_Burnt_m1"},
    {"hw1", "HW1_shoes_c1_m1",
     "HW1_shoes_c1_m1 HW1_shoes_c2_m1 HW1_shoes_c3_m1 "
     "HW1_shoes_Burnt_m1"},
    {"hw1", "HW1_skinA_m1",
     "HW1_skinA_m1 HW1_skinB_m1 HW0_skin_zombie_m1 HW0_skin_Burnt_m1"},
    {"hw1", "HW1_faceA_m1", "HW1_faceA_m1 HW1_face_Burnt_m1"},
    {"hw1", "HW0_m1hairS_m1", "HW0_m1hairS_m1 HW0_m2hairS_m1 HW0_m3hairS_m1"},
    {"hw2", "HW2_bodyA_c1_m1",
     "HW2_bodyA_c1_m1 HW2_bodyA_c2_m1 HW2_bodyA_c3_m1 "
     "HW2_bodyA_zombie_c1_m1 HW2_bodyA_zombie_c2_m1 "
     "HW2_bodyA_zombie_c3_m1 HW2_body_Burnt_m1"},
    {"hw2", "HW2_skirt_c1_m1",
     "HW2_skirt_c1_m1 HW2_skirt_c2_m1 HW2_skirt_c3_m1 "
     "HW2_skirt_Burnt_m1"},
    {"hw2", "HW2_shoesA_c1_m1",
     "HW2_shoesA_c1_m1 HW2_shoesA_c2_m1 HW2_shoesA_c3_m1 "
     "HW2_shoesA_Burnt_m1"},
    {"hw2", "HW2_shirt_c1_m1",
     "HW2_shirt_c1_m1 HW2_shirt_c2_m1 HW2_shirt_c3_m1 "
     "HW2_shirt_zombie_c1_m1 HW2_shirt_zombie_c2_m1 "
     "HW2_shirt_zombie_c3_m1"},
    {"hw2", "HW2_inner_c1_m1",
     "HW2_inner_c1_m1 HW2_inner_c2_m1 HW2_inner_c3_m1 "
     "HW2_inner_zombie_c1_m1 HW2_inner_zombie_c2_m1 "
     "HW2_inner_zombie_c3_m1"},
    {"hw2", "HW2_pants_c1_m1",
     "HW2_pants_c1_m1 HW2_pants_c2_m1 HW2_pants_c3_m1"},
    {"hw2", "HW2_shoesB_c1_m1",
     "HW2_shoesB_c1_m1 HW2_shoesB_c2_m1 HW2_shoesB_c3_m1"},
    {"hw2", "HW2_skinA_m1",
     "HW2_skinA_m1 HW2_skinB_m1 HW2_skinC_m1 HW2_skin_zombie_m1 "
     "HW2_skin_Burnt_m1"},
    {"hw2", "HW2_scarf_c1_m1", "HW2_scarf_c1_m1 HW2_scarf_Burnt_m1"},
    {"hw2", "HW2_faceA_m1", "HW2_faceA_m1 HW2_face_Burnt_m1"},
    {"hw2", "HW2_o1hair_m1", "HW2_o1hair_m1 HW2_o1hair_Burnt_m1"},
    {"hw2", "HW2_o1hairS_m1", "HW2_o1hairS_m1 HW2_o2hairS_m1"},
    {"hr0", "HR0_hand_m1", "HR0_hand_m1 HR0_hand_Burnt_m1"},
    {"hr0", "HR0_body_m1", "HR0_body_m1 HR0_body_Burnt_m1"},
    {"hr0", "HR0_armer_m1", "HR0_armer_m1 HR0_armer_Burnt_m1"},
    {"hr0", "HR0_protector_m1", "HR0_protector_m1 HR0_protector_Burnt_m1"},
    {"hr0", "HR0_metset_m1", "HR0_metset_m1 HR0_metset_Burnt_m1"},
    {"hr0", "HR0_camelbag_m1", "HR0_camelbag_m1 HR0_camelbag_Burnt_m1"},
    {"hr0", "HR0_Foot_m1", "HR0_Foot_m1 HR0_Foot_Burnt_m1"},
    {"hr0", "HR0_boots_m1", "HR0_boots_m1 HR0_boots_Burnt_m1"},
    {"hr0", "HR0_armer_m1", "HR0_armer_zonbie_m1"},
    {"hr0", "HR0_body_m1", "HR0_body_zombie_m1"},
    {"hr0", "HW0_m1hairS_m1", "HW0_m1hairS_m1 HW0_m2hairS_m1 HW0_m3hairS_m1"},
});
}  // namespace
memory::Vector<NpcPaletteBinding> make_npc_palette(
    const content::StageDefinition& definition,
    std::span<const render::WorldTextureHandle> textures,
    const std::shared_ptr<memory::Domain>& domain) {
  if (textures.size() != definition.textures.size()) {
    throw std::invalid_argument("NPC palette handles do not match definitions");
  }
  memory::Vector<NpcPaletteBinding> result{
      memory::Allocator<NpcPaletteBinding>(domain)};
  std::string_view family;
  std::size_t variation = 0;
  for (const auto& rule : kRules) {
    if (family != rule.family) {
      family = rule.family;
      variation = 0;
    }
    auto names = rule.variations;
    while (!names.empty()) {
      const auto split = names.find(' ');
      const auto name = names.substr(0, split);
      auto expected = memory::String(name, memory::Allocator<char>(domain));
      expected += "_psd.dds";
      const auto selected =
          std::ranges::find_if(definition.textures, [&](const auto& texture) {
            return std::ranges::equal(
                texture.name, expected,
                [](unsigned char left, unsigned char right) {
                  return std::tolower(left) == std::tolower(right);
                });
          });
      if (selected != definition.textures.end()) {
        const auto index =
            static_cast<std::size_t>(selected - definition.textures.begin());
        if (textures[index].value == 0) {
          throw std::runtime_error("NPC palette texture is not resident");
        }
        result.push_back({rule.family, rule.material, variation,
                          textures[index], name == rule.material});
      }
      ++variation;
      if (split == std::string_view::npos) break;
      names.remove_prefix(split + 1);
    }
  }
  return result;
}
}  // namespace bw::game
