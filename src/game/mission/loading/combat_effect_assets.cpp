// Source pack admission follows archive record order, independently of model
// texture tables. Resolve/copy CPU owners first, select rooted references, then
// upload through the existing texture constructor. State exists before loading
// so any failure retires earlier uploads. E.6 (#re-raii) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) were reread for
// implementation: one retirement authority and no deferred frame allocation.
#include "combat_effect_assets.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "bw/content/formats/texture_mips.hpp"
#include "bw/content/formats/world_texture.hpp"

namespace bw::game {
namespace {
constexpr std::array<std::string_view, 4> kOriginalPacks{
    "effect.effect_com.effect_pack", "effect.effect_com2.effect_pack",
    "effect.effect_com3.effect_pack", "effect.effect_boss1.effect_pack"};
constexpr std::size_t kTextureCapacity = 511;
constexpr std::size_t kPartTextureOffset = 0x1cc;
struct Pack {
  Pack(std::uint16_t index, content::UserDefinition input,
       const std::shared_ptr<memory::Domain>& domain)
      : slot(index),
        source(std::move(input)),
        textures(memory::Allocator<CombatEffectTexture>(domain)) {}
  std::uint16_t slot;
  content::UserDefinition source;
  memory::Vector<CombatEffectTexture> textures;
  std::array<bool, kTextureCapacity> required{};
};
}  // namespace
struct CombatEffectAssets::State {
  State(render::WorldResources& output,
        const std::shared_ptr<memory::Domain>& domain)
      : renderer(output),
        owners(memory::Allocator<Pack>(domain)),
        publication(memory::Allocator<CombatEffectPack>(domain)) {
    owners.reserve(kOriginalPacks.size());
    publication.reserve(kOriginalPacks.size());
  }
  ~State() {
    for (const auto& pack : owners) {
      for (const auto& texture : pack.textures) {
        renderer.release_world_texture(texture.resident);
      }
    }
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void load(const platform::Files& files, const resources::CatalogSet& catalogs,
            const std::shared_ptr<memory::Domain>& domain,
            std::span<const std::uint32_t> requested, bool texture_mips,
            const content::LoadProgress& progress) {
    if (requested.empty()) {
      throw std::invalid_argument("Combat effect asset request is empty");
    }
    for (std::size_t slot = 0; slot < kOriginalPacks.size(); ++slot) {
      const auto name = kOriginalPacks.at(slot);
      try {
        (void)catalogs.resolve(name);
      } catch (const resources::Error& error) {
        if (error.code != resources::ErrorCode::not_found) throw;
        continue;
      }
      auto source =
          content::load_user_definition(files, catalogs, name, domain);
      if (source.record != 0) {
        throw std::invalid_argument(
            "Original effect pack is not archive record zero");
      }
      owners.emplace_back(static_cast<std::uint16_t>(slot), std::move(source),
                          domain);
      if (progress) progress();
    }
    memory::Vector<formats::EffectPackView> views{
        memory::Allocator<formats::EffectPackView>(domain)};
    views.reserve(owners.size());
    for (const auto& owner : owners) views.emplace_back(owner.source.bytes());
    // The original binder overwrites IDs in pack/entry order. Texture
    // identities are archive record index minus one, not names or model table
    // indices.
    for (const auto id : requested) {
      std::optional<std::size_t> winner, entry;
      for (std::size_t pack = 0; pack < owners.size(); ++pack) {
        if (const auto found = views[pack].find_effect(id)) {
          winner = pack;
          entry = found;
        }
      }
      if (!winner || !entry) {
        throw std::invalid_argument(
            "Required combat effect is not in mounted original packs");
      }
      auto& owner = owners[*winner];
      const auto& view = views[*winner];
      for (const auto part_index : view.effect(*entry).parts) {
        if (part_index < 0) continue;
        const auto part = view.part(static_cast<std::size_t>(part_index));
        for (std::size_t lane = 0; lane < effects::SpritePart::kTextureSlots;
             ++lane) {
          const auto index = part.signed_word(kPartTextureOffset +
                                              lane * sizeof(std::uint32_t));
          if (index < 0) continue;
          const auto texture = static_cast<std::size_t>(index);
          if (texture >= kTextureCapacity ||
              texture + 1 >= owner.source.archive.records().size()) {
            throw std::invalid_argument(
                "Original effect texture index escapes archive");
          }
          owner.required.at(texture) = true;
        }
      }
    }
    for (auto& owner : owners) {
      const auto count =
          static_cast<std::size_t>(std::ranges::count(owner.required, true));
      owner.textures.reserve(count);
      for (std::size_t index = 0; index < owner.required.size(); ++index) {
        if (!owner.required.at(index)) continue;
        const auto record = index + 1;
        const auto& archive = owner.source.archive;
        if (archive.records().at(record).type != "DDS ") {
          throw std::invalid_argument(
              "Combat sprite requires an ordinary 2D texture record");
        }
        auto pixels = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record),
             formats::WorldTextureShape::two_dimensional},
            memory::Allocator<std::uint8_t>(domain));
        if (texture_mips) {
          pixels = formats::generate_texture_mips(
              pixels, memory::Allocator<std::uint8_t>(domain));
        }
        const auto handle = renderer.upload_world_texture(pixels);
        owner.textures.push_back(
            {{static_cast<std::uint32_t>(index), pixels.width, pixels.height},
             handle});
        if (progress) progress();
      }
      publication.push_back({owner.slot, owner.source.bytes(), owner.textures});
    }
  }
  render::WorldResources& renderer;
  memory::Vector<Pack> owners;
  memory::Vector<CombatEffectPack> publication;
};
CombatEffectAssets::CombatEffectAssets(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    render::WorldResources& renderer,
    const std::shared_ptr<memory::Domain>& domain,
    std::span<const std::uint32_t> requested_ids, bool texture_mips,
    const content::LoadProgress& progress)
    : state_(std::allocate_shared<State>(memory::Allocator<State>(domain),
                                         renderer, domain)) {
  state_->load(files, catalogs, domain, requested_ids, texture_mips, progress);
}
CombatEffectAssets::~CombatEffectAssets() = default;
std::span<const CombatEffectPack> CombatEffectAssets::packs() const noexcept {
  return state_->publication;
}
}  // namespace bw::game
