// Exercise authored masks and ordered runtime texture aliases in process.
// Synthetic data only. Production ownership follows I.1/MEM.9 as documented
// in npc_appearance.hpp; fixture backing uses the established test exception.
#include "game/actors/npc/npc_appearance.hpp"

#include <memory_resource>
#include <stdexcept>

#include "../../../fixtures/model_fixture.hpp"
namespace {
class AppearanceBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* value, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(value, bytes, alignment);
  }
};
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
bw::formats::ModelDefinition model(
    std::uint32_t group, const std::shared_ptr<bw::memory::Domain>& domain) {
  model_fixture::Options options;
  options.group = group;
  const auto bytes = model_fixture::archive(options);
  bw::resources::ArchiveBytes owned(
      bytes.begin(), bytes.end(), bw::memory::Allocator<std::uint8_t>(domain));
  return bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(std::move(owned)), domain);
}
}  // namespace
void npc_appearance_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<AppearanceBacking>());
  const auto definition = model(7, domain);
  const bw::game::ResidentModelView resident{"hm0", definition, {1}, {}};
  bw::content::PlacementArchetype actor;
  actor.model = "hm0";
  const auto prepare = [&](const auto& value) {
    return bw::game::PreparedNpcAppearance::prepare(value, resident, {}, {},
                                                    domain);
  };
  require(prepare(actor).draws()[0].selected,
          "Absent mask retains native default bit7");
  actor.customization_present = true;
  require(!prepare(actor).draws()[0].selected,
          "Present empty mask retains only group0");
  actor.customization = "--7-7--";
  require(prepare(actor).draws()[0].selected,
          "Source repeated and empty tokens are idempotent");
  actor.model = "vh0";
  actor.customization = "0";
  require(prepare(actor).draws()[0].selected,
          "Excluded vehicle customization retains default");
  actor.model = "PW0";
  require(prepare(actor).draws()[0].selected,
          "Source class prefix is case-insensitive even though palette names "
          "are not");
  actor.model = "hm0";
  actor.secondary_customization = "9-2-4-1";
  const std::array slots{
      bw::game::ResidentTextureSlot{"Skin_psd.dds", {10}},
      bw::game::ResidentTextureSlot{"AO_alias", {10}},
      bw::game::ResidentTextureSlot{"skin_case_sensitive", {11}},
      bw::game::ResidentTextureSlot{"Other_psd.dds", {12}}};
  const std::array palettes{
      bw::game::NpcPaletteBinding{"hm0", "Skin", 9, {99}},
      bw::game::NpcPaletteBinding{"hm0", "Skin", 2, {20}},
      bw::game::NpcPaletteBinding{"hm0", "Other", 4, {40}, true},
      bw::game::NpcPaletteBinding{"other", "Skin", 1, {30}}};
  const auto appearance = bw::game::PreparedNpcAppearance::prepare(
      actor, resident, palettes, slots, domain);
  const auto changes = appearance.texture_overrides();
  require(changes.size() == 2 && changes[0].slot == 0 && changes[1].slot == 1 &&
              changes[0].texture.value == 99 && changes[1].texture.value == 99,
          "Ascending bits override prefix and handle aliases across material "
          "semantics");
  require(slots[0].texture.value == 10 && slots[1].texture.value == 10,
          "Preparation leaves resident table immutable");
  require(bw::game::PreparedNpcAppearance::default_weapon(resident, domain)
              .draws()[0]
              .selected,
          "Weapon does not inherit body customization");
  const auto high_model = model(200, domain);
  const bw::game::ResidentModelView high{"hm0", high_model, {2}, {}};
  require(bw::game::PreparedNpcAppearance::prepare(actor, high, {}, {}, domain)
              .draws()[0]
              .selected,
          "Body factory leaves model high128 mask enabled");
  actor.customization = "bad";
  bool rejected = false;
  try {
    static_cast<void>(prepare(actor));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Malformed index is explicit, not silently group0");
}
