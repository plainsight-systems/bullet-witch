// Localized-text repair: design I.1 and implementation E.4, with MEM.9
// retrieved in both phases. Policy publishes content IDs; the surface owns
// prepared purchased text/art. No English fallback or frame-time decoding.
// Main-menu feedback implementation: original supplied-time tracks and copied
// presentation values; prepared localized text and resident textures. Core E.4
// (#re-design-invariants), performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) read during implementation require
// checked snapshots and fixed frame storage. Source proof:
// frontend-feedback.md. DDS resources retain original texture metadata through
// the shared decoder; native construction consumes the immutable launch mip
// policy. Raw icon PNGs remain a distinct base-only upload. Both-phase E.6/I.11
// and COPY.5 preserve borrowed archive views, candidate cleanup and
// renderer-owned identities. Review status reuses the prepared font and fixed
// sprite array. I.1
// (#ri-explicit) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), consulted in both
// phases, keep this a bounded sprite publication with no menu background,
// game-state mutation or new texture ownership. Memory migration: native owners
// propagate the ResourceService CPU domain to final pixel/glyph storage and
// retained presentation state/strings. Owning allocators retain the domain
// through copies and partial-construction cleanup; defaults are legacy
// parser/test compatibility, never exhaustion fallback. PNG writes directly to
// its final owned destination. Fixed draw storage adds no per-frame
// allocations; native texture lifetime stays with SpriteOutput. Design R.1
// (#rr-raii), implementation E.6 (#re-raii), both-phase MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain owners and
// fixed presentation capacity. Pinned source/external libpng exclusions are
// recorded in research/engine/subsystems/memory-image-presentation.md. Resource
// memory boundary: diagnostics explicitly copy allocator-aware archive names
// into the standard exception string. This failure-only library storage is
// outside resource-domain coverage. E.17 (#re-not-always), MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) preserve
// the existing error boundary; both phases consulted. No menu behavior change.
// Implementation of the menu asset and draw contract in
// startup_menu_surface.hpp. Resource leases retain archive bytes through
// construction. Textures are kept resident until end; temporary PNG decode
// storage is released after each upload. Fixed arrays bound images and glyph
// commands. Original main-menu art/positions are recovered; child page list
// geometry remains explicitly reconstructed. Implementation MCP SL.con.3
// (#rsl-bounds) checks metadata/command capacities; GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) uploads only at activation.
#include "frontend_menu_surface.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/image.hpp"
#include "bw/content/formats/text_table.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "bw/math/scalar.hpp"
#include "game/ui/common/input_icon.hpp"
#include "game/ui/common/localized_text.hpp"
#include "game/ui/common/sprite_text.hpp"
namespace bw::game {
namespace {
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
struct TextPosition {
  float x{}, y{}, size{};
};
constexpr unsigned kMainBackground = 334, kStageBackground = 306;
constexpr resources::ResourceTag kMenuTag = 12;
constexpr std::size_t kCostumeCount = 6;
constexpr unsigned kCostumeImageBase = 10000, kCampaignImageBase = 11000,
                   kConceptImageBase = 12000, kWhiteImage = 13000,
                   kIconAtlas = 13001;
constexpr unsigned kCustomBackground = 46, kDefaultCostumeImage = 445;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
constexpr std::array<std::string_view, 56> kGroups{"sprite.titlemenu",
                                                   "sprite.font",
                                                   "sprite.stageselect",
                                                   "sprite.custom",
                                                   "sprite.cos01",
                                                   "sprite.cos02",
                                                   "sprite.cos03",
                                                   "sprite.cos04",
                                                   "sprite.cos05",
                                                   "sprite.text.costxt01",
                                                   "sprite.text.costxt02",
                                                   "sprite.text.costxt03",
                                                   "sprite.text.costxt04",
                                                   "sprite.text.costxt05",
                                                   "sprite.system",
                                                   "sprite.text.sys",
                                                   "sprite.text.sts",
                                                   "sprite.text.onm",
                                                   "sprite.stageselect.ssnor",
                                                   "sprite.stageselect.sscon",
                                                   "sprite.text.cmtxt00",
                                                   "sprite.text.cmtxt01",
                                                   "sprite.text.cmtxt02",
                                                   "sprite.text.cmtxt03",
                                                   "sprite.text.cmtxt04",
                                                   "sprite.text.cmtxt05",
                                                   "sprite.text.cmtxt16",
                                                   "sprite.text.cmtxt17",
                                                   "sprite.text.cmtxt18",
                                                   "sprite.text.cmtxt19",
                                                   "sprite.text.cmtxt20",
                                                   "sprite.text.cmtxt21",
                                                   "sprite.text.cmtxt32",
                                                   "sprite.text.cmtxt33",
                                                   "sprite.text.cmtxt34",
                                                   "sprite.text.cmtxt35",
                                                   "sprite.text.cmtxt36",
                                                   "sprite.text.cmtxt37",
                                                   "sprite.cm00",
                                                   "sprite.cm01",
                                                   "sprite.cm02",
                                                   "sprite.cm03",
                                                   "sprite.cm04",
                                                   "sprite.cm05",
                                                   "sprite.cm16",
                                                   "sprite.cm17",
                                                   "sprite.cm18",
                                                   "sprite.cm19",
                                                   "sprite.cm20",
                                                   "sprite.cm21",
                                                   "sprite.cm32",
                                                   "sprite.cm33",
                                                   "sprite.cm34",
                                                   "sprite.cm35",
                                                   "sprite.cm36",
                                                   "sprite.cm37"};
unsigned suffix_id(std::string_view name) {
  constexpr unsigned kDecimal = 10;
  return static_cast<unsigned>(name[name.size() - 2] - '0') * kDecimal +
         static_cast<unsigned>(name.back() - '0');
}
// Resolve the original namespace/key/language path from a validated table.
// No prose fallback is permitted: missing authored content is a load failure.
struct TextIdentity {
  std::string_view name_space, key;
};
std::string_view original_text(const formats::TextTable& table,
                               TextIdentity identity) {
  constexpr unsigned kEnglish = 200;
  for (std::size_t i = 0; i < table.records(0); ++i) {
    const auto group = table.node(0, i);
    if (group.text != identity.name_space) continue;
    for (std::size_t j = 0; j < group.children.count; ++j) {
      const auto row = table.node(1, group.children.first + j);
      if (row.text != identity.key) continue;
      for (std::size_t k = 0; k < row.children.count; ++k) {
        const auto variant = table.node(2, row.children.first + k);
        for (std::size_t l = 0; l < variant.children.count; ++l) {
          const auto text = table.node(3, variant.children.first + l);
          if (text.identifier == kEnglish) return text.text;
        }
      }
    }
  }
  throw std::runtime_error("Original frontend text key missing");
}
constexpr std::array<unsigned, main_menu_row_count> kMainImages{354, 353, 357,
                                                                356, 358, 355};
constexpr std::array<render::Rect, main_menu_row_count> kMainPositions{
    {{502, 135, 0, 0},
     {568, 204, 0, 0},
     {507, 267, 0, 0},
     {483, 334, 0, 0},
     {475, 399, 0, 0},
     {457, 457, 0, 0}}};
struct MainTrack {
  unsigned image{};
  render::Rect from, to;
  float begin{}, end{}, shape{};
};
constexpr std::array<MainTrack, 8> kMainTracks{
    {{351, {-730, 0}, {0, 0}, 0, 1},
     {352, {308, 692}, {308, 32}, .5F, 1.3F},
     {349, {-590, 264}, {143, 264}, 0, .7F},
     {350, {-565, 96}, {168, 96}, .2F, 1.6F},
     {403, {2140, 402}, {869, 402}, .3F, 1.2F, .6F},
     {401, {1396, 468}, {116, 468}, 0, .8F},
     {402, {1366, 624}, {86, 624}, 0, 1.1F, .6F},
     {400, {1094, 910}, {794, 610}, 0, 1.1F}}};
constexpr std::array<render::Rect, 6> kMainEntryPositions{{{172, -300},
                                                           {818, -304},
                                                           {910, 1207},
                                                           {523, 1134},
                                                           {100, 1099},
                                                           {100, 1139}}};
constexpr std::array<float, 6> kMainEntryBegin{.1F, .2F, 0, .1F, .6F, .6F};
constexpr std::array<float, 6> kMainEntryEnd{1.3F, 1.1F, 1.5F, 1, 1.4F, 1.4F};
// Child descriptor positions are compatibility facts; interpolation remains
// the shared original quintic/tangent algorithm, not per-scene tweaked motion.
constexpr std::array<MainTrack, 13> kStageTracks{
    {{400, {1094, 910}, {794, 610}, 0, .9F},
     {314, {588, -700}, {588, -8}, 0, 1.1F, .6F},
     {316, {157, 1030}, {157, 338}, .2F, 1, .6F},
     {317, {0, 1112}, {0, 420}, .2F, .7F, .6F},
     {315, {0, 940}, {0, 248}, .2F, .6F, .6F},
     {310, {1100, -700}, {1100, -8}, .1F, 1, .4F},
     {309, {1063, -463}, {1063, 229}, 0, .9F, .4F},
     {311, {1096, -146}, {1096, 546}, 0, .8F, .4F},
     {312, {1096, -458}, {1096, 234}, 0, .8F, .4F},
     {313, {1096, -458}, {1096, 234}, 0, .8F, .4F},
     {322, {-700, 0}, {-4, 0}, .3F, .9F, .6F},
     {319, {-466, -449}, {234, -449}, .2F, 1.1F, .4F},
     {364, {640, -700}, {640, -8}, 0, .8F, .4F}}};
constexpr std::array<MainTrack, 9> kCostumeTracks{
    {{50, {880, -720}, {880, -6}, .1F, 1, .5F},
     {49, {1640, 150}, {926, 150}, .2F, 1.1F, .5F},
     {400, {1094, 910}, {794, 610}, 0, .9F},
     {51, {88, -400}, {88, -6}, .3F, .8F, .5F},
     {52, {88, 928}, {88, 534}, .3F, .8F, .5F},
     {60, {-640, 0}, {-6, 0}, .2F, 1, .5F},
     {53, {95, 720}, {95, 0}, .2F, 1.1F, .4F},
     {48, {598, -692}, {598, 28}, .4F, 1.1F, 1},
     {55, {122, 1030}, {122, -34}, .3F, 1.3F, .7F}}};
constexpr std::array<MainTrack, 5> kDifficultyTracks{
    {{338, {-1280, 211}, {0, 211}, 0, 1.2F},
     {342, {-1280, 276}, {0, 276}, 0, 1.2F},
     {340, {1404, 345}, {124, 345}, 0, 1.2F},
     {348, {-1280, 407}, {0, 407}, 0, 1.2F},
     {346, {1628, 470}, {348, 470}, 0, 1.2F}}};
constexpr std::array<unsigned, 5> kDifficultyImages{337, 341, 339, 347, 345};

float main_track_factor(float elapsed, float begin, float end, float shape) {
  const auto fraction = std::clamp((elapsed - begin) / (end - begin), 0.F, 1.F);
  return shape == 0
             ? math::quintic_ease(fraction)
             : math::eased_tangent_interpolate(
                   math::EasedTangentEndpoints<float>{0, 1, shape, shape},
                   fraction);
}

unsigned sprite_id(std::span<const std::uint8_t> bytes, std::size_t index) {
  constexpr std::size_t word_bytes = 2;
  constexpr unsigned shift = 8;
  if (index >= bytes.size() / word_bytes) {
    throw std::runtime_error("Menu sprite metadata is incomplete");
  }
  return static_cast<unsigned>(bytes[index * word_bytes]) |
         (static_cast<unsigned>(bytes[index * word_bytes + 1]) << shift);
}
}  // namespace
struct FrontendMenuSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  memory::Allocator<std::byte> allocator;
  resources::ArchiveBytes menu_text, input_icons;
  static constexpr std::size_t kDescriptionCapacity = 128;
  std::array<std::array<char, kDescriptionCapacity>, main_menu_row_count>
      descriptions{};
  resources::ResourceService& resources;
  render::SpriteOutput& renderer;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, render::kMaximumTextures> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  static constexpr std::size_t kTextCapacity = 512, kCampaignCount = 6,
                               kConceptCapacity = 64;
  using Text = std::array<char, kTextCapacity>;
  std::array<Text, kCostumeCount> costume_labels{};
  std::array<Text, kCampaignCount> stage_labels{};
  std::array<Text, kConceptCapacity> concept_labels{};
  std::array<Text, menu_difficulty_count> difficulty_descriptions{};
  std::array<Text, 4> stage_detail_labels{};
  std::array<Text,
             static_cast<std::size_t>(FrontendDialog::profile_enable_autosave) +
                 1>
      dialog_labels{};
  std::array<Text, 4> common_labels{};
  render::TextureHandle font_texture;
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool requested{}, constructed{}, pause_only{};
  bool includes(std::string_view group) const noexcept {
    return !pause_only ||
           (group != "sprite.titlemenu" &&
            !group.starts_with("sprite.stageselect") &&
            group != "sprite.text.sts" && !group.starts_with("sprite.cm") &&
            !group.starts_with("sprite.text.cmtxt"));
  }
  void begin(bool pause) {
    end();
    pause_only = pause;
    requested = true;
    for (const auto group : kGroups) {
      if (!includes(group)) continue;
      try {
        resources.request(group, kMenuTag);
      } catch (const std::exception& error) {
        throw std::runtime_error(std::string(group) + ": " + error.what());
      }
    }
  }
  State(resources::ResourceService& service, render::SpriteOutput& output,
        resources::ArchiveBytes text, resources::ArchiveBytes icons)
      : allocator(storage_for(service)),
        menu_text(std::move(text)),
        input_icons(std::move(icons)),
        resources(service),
        renderer(output) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    // Clear retained draws before releasing their texture handles and archive
    // leases.
    renderer.clear_sprites();
    while (image_count > 0) {
      renderer.release_texture(images.at(--image_count).texture);
    }
    for (auto& lease : leases) lease.reset();
    font.reset();
    resources.release(kMenuTag);
    requested = false;
    constructed = false;
    count = 0;
  }
  void construct() {
    // Build device assets once from the published groups; draws reuse those
    // handles.
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      if (!includes(kGroups.at(group))) continue;
      auto acquired = resources.acquire(kGroups.at(group));
      if (!acquired) {
        throw std::runtime_error("Missing menu resource: " +
                                 std::string(kGroups.at(group)));
      }
      auto& lease = leases.at(group).emplace(std::move(*acquired));
      const auto name = kGroups.at(group);
      const bool is_font = name == "sprite.font";
      const bool is_costume = name.starts_with("sprite.cos");
      const bool is_label = name.starts_with("sprite.text.costxt");
      constexpr std::size_t first_costume_group = 4, first_label_group = 9;
      if (name.starts_with("sprite.text.")) {
        const auto table = formats::TextTable::parse(lease.bytes());
        const auto name_space =
            name.substr(std::string_view("sprite.text.").size());
        if (is_label) {
          localized_text::prepare_text(
              original_text(table, {name_space, "0000"}),
              costume_labels.at(group - first_label_group + 1));
        } else if (name_space == "onm") {
          localized_text::prepare_text(original_text(table, {"onm", "0030"}),
                                       costume_labels.at(0));
          localized_text::prepare_text(
              original_text(table, {"onm", "0008"}),
              dialog_labels.at(
                  static_cast<std::size_t>(FrontendDialog::change_costume)));
        } else if (name_space == "sys") {
          constexpr std::array<TextIdentity, 15> kDialogKeys{{{"sys", "0000"},
                                                              {"mem", "0060"},
                                                              {"sys", "0010"},
                                                              {"sys", "0000"},
                                                              {"mem", "0005"},
                                                              {"mem", "0047"},
                                                              {"mem", "0048"},
                                                              {"mem", "0008"},
                                                              {"mem", "0040"},
                                                              {"mem", "0021"},
                                                              {"mem", "0020"},
                                                              {"mem", "0043"},
                                                              {"mem", "0056"},
                                                              {"mem", "0042"},
                                                              {"mem", "0022"}}};
          for (std::size_t i = 1; i < kDialogKeys.size(); ++i) {
            if (i == static_cast<std::size_t>(FrontendDialog::change_costume)) {
              continue;
            }
            localized_text::prepare_text(
                original_text(table, kDialogKeys.at(i)), dialog_labels.at(i));
          }
          constexpr std::array<std::string_view, 4> kCommonKeys{"0001", "0002",
                                                                "0027", "0028"};
          for (std::size_t i = 0; i < kCommonKeys.size(); ++i) {
            localized_text::prepare_text(
                original_text(table, {"sys", kCommonKeys.at(i)}),
                common_labels.at(i));
          }
          constexpr std::array<std::string_view, menu_difficulty_count>
              kDifficultyKeys{"0120", "0121", "0122", "0123", "0124"};
          for (std::size_t i = 0; i < kDifficultyKeys.size(); ++i) {
            localized_text::prepare_text(
                original_text(table, {"sys", kDifficultyKeys.at(i)}),
                difficulty_descriptions.at(i));
          }
        } else if (name_space == "sts") {
          constexpr std::array<std::string_view, kCampaignCount> keys{
              "0001", "0002", "0003", "0004", "0005", "0006"};
          for (std::size_t i = 0; i < keys.size(); ++i) {
            localized_text::prepare_text(
                original_text(table, {"sts", keys.at(i)}), stage_labels.at(i));
          }
          constexpr std::array<std::string_view, 4> kDetailKeys{"0081", "0082",
                                                                "0083", "0084"};
          for (std::size_t i = 0; i < kDetailKeys.size(); ++i) {
            localized_text::prepare_text(
                original_text(table, {"sts", kDetailKeys.at(i)}),
                stage_detail_labels.at(i));
          }
        } else if (name_space.starts_with("cmtxt")) {
          const auto id = suffix_id(name_space);
          localized_text::prepare_text(
              original_text(table, {name_space, "0000"}),
              concept_labels.at(id));
        } else {
          throw std::runtime_error("Unmapped frontend localization group");
        }
        continue;
      }
      if (is_font) {
        font.emplace(formats::BitmapFont::parse(lease.bytes(), allocator));
      }
      const auto& archive = lease.archive();
      std::size_t image_index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        if (image_count == images.size()) {
          throw std::runtime_error("Menu texture capacity exceeded");
        }
        auto id = is_font
                      ? 0
                      : (is_costume ? kCostumeImageBase +
                                          static_cast<unsigned>(
                                              group - first_costume_group + 1)
                                    : sprite_id(lease.bytes(), image_index));
        constexpr unsigned kFallbackConcept = 64;
        if (name == "sprite.stageselect.ssnor") {
          id = kCampaignImageBase + static_cast<unsigned>(image_index);
        }
        if (name == "sprite.stageselect.sscon") {
          id = kConceptImageBase + kFallbackConcept;
        }
        if (name.starts_with("sprite.cm")) {
          id = kConceptImageBase + suffix_id(name);
        }
        constexpr unsigned kSystemFooter = 400, kDialogEdge = 326,
                           kDialogButton = 327;
        if (name == "sprite.system" && id != kSystemFooter &&
            id != kDialogEdge && id != kDialogButton) {
          ++image_index;
          continue;
        }
        formats::WorldTextureImage decoded;
        try {
          decoded = formats::decode_world_texture(
              {archive.payload(record), archive.metadata(record)}, allocator);
        } catch (const std::exception& error) {
          throw std::runtime_error(std::string(kGroups.at(group)) + "/" +
                                   std::string(archive.records()[record].name) +
                                   ": " + error.what());
        }
        if (is_font && !font) {
          throw std::logic_error("Missing font metrics");
        }
        if (is_font && font &&
            (image_index != 0 || decoded.width != font.value().atlas_width() ||
             decoded.height != font.value().atlas_height())) {
          throw std::runtime_error("Menu font atlas disagrees with metrics");
        }
        const auto texture = renderer.upload_texture(decoded);
        images.at(image_count++) = {id, texture,
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
        if (is_font) font_texture = texture;
        ++image_index;
      }
      if (image_index == 0) {
        throw std::runtime_error("Menu resource has no images");
      }
    }
    if (!pause_only) {
      const resources::detail::Reader reader{menu_text};
      constexpr std::size_t kWord = 4, kLanguages = 6, kEnglish = 1,
                            kFirstDescription = 96;
      const auto rows = reader.u32(0);
      if (rows < kFirstDescription + descriptions.size()) {
        throw std::runtime_error("Main menu description rows missing");
      }
      const auto pool =
          kWord + static_cast<std::size_t>(rows) * kLanguages * kWord;
      reader.range(kWord, pool - kWord);
      for (std::size_t i = 0; i < descriptions.size(); ++i) {
        const auto offset = reader.u32(
            kWord + ((kFirstDescription + i) * kLanguages + kEnglish) * kWord);
        if (offset < pool || offset >= menu_text.size()) {
          throw std::runtime_error("Main menu description escapes text pool");
        }
        localized_text::prepare_text(
            reader.text_view({pool, menu_text.size() - pool},
                             static_cast<std::uint32_t>(offset - pool)),
            descriptions.at(i));
      }
    }
    const auto icons = decode_input_icon_atlas(
        input_icons, memory::Allocator<std::uint8_t>(allocator));
    images.at(image_count++) = {
        kIconAtlas,
        renderer.upload_texture(icons.width, icons.height, icons.rgba),
        static_cast<float>(icons.width), static_cast<float>(icons.height)};
    constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    images.at(image_count++) = {kWhiteImage,
                                renderer.upload_texture(1, 1, white), 1, 1};
    constructed = true;
  }
  void append(render::Sprite command) {
    if (count == commands.size()) {
      throw std::runtime_error("Menu draw capacity exceeded");
    }
    commands.at(count++) = command;
  }
  void image(unsigned id, render::Rect position, render::Color tint = {},
             float scale = 1) {
    for (std::size_t index = 0; index < image_count; ++index) {
      const auto& entry = images.at(index);
      if (entry.id != id) continue;
      if (position.width == 0) position.width = entry.width;
      if (position.height == 0) position.height = entry.height;
      // Native sprite transforms scale around their rectangle center unless
      // an explicit pivot is authored (the fixed UI prompts handle that).
      constexpr float kHalf = .5F;
      position.x += position.width * (1 - scale) * kHalf;
      position.y += position.height * (1 - scale) * kHalf;
      position.width *= scale;
      position.height *= scale;
      append({entry.texture, position, {0, 0, 1, 1}, tint});
      return;
    }
    throw std::runtime_error("Missing menu image identity: " +
                             std::to_string(id));
  }
  void text(std::string_view value, TextPosition position, render::Color tint,
            bool centered = false) {
    constexpr float kHalf = .5F;

    if (!font) throw std::logic_error("Missing menu font");
    std::size_t begin = 0;
    for (;;) {
      const auto end = value.find('\n', begin);
      const auto line = value.substr(
          begin, end == std::string_view::npos ? end : end - begin);
      float width = 0;
      if (centered) {
        for (const auto character : line) {
          const auto glyph = font->glyph(static_cast<unsigned char>(character));
          if (!glyph) throw std::runtime_error("Original menu glyph missing");
          width += static_cast<float>(glyph->advance) * position.size /
                   static_cast<float>(font->pixel_size());
        }
      }
      count += append_sprite_text(
          *font, font_texture, line,
          {position.x - width * kHalf, position.y, position.size}, tint,
          std::span(commands).subspan(count));
      if (end == std::string_view::npos) break;
      begin = end + 1;
      position.y += position.size;
    }
  }
  const Image& find_image(unsigned id) const {
    for (std::size_t i = 0; i < image_count; ++i) {
      if (images.at(i).id == id) return images.at(i);
    }
    throw std::runtime_error("Missing menu image identity: " +
                             std::to_string(id));
  }
  render::Rect track_position(const MainTrack& track,
                              const ChildMenuFeedback& view,
                              float duration) const {
    const auto elapsed = view.motion == ChildMenuMotion::active ? duration
                         : view.motion == ChildMenuMotion::leaving
                             ? duration - view.elapsed
                             : view.elapsed;
    const auto factor =
        main_track_factor(elapsed, track.begin, track.end, track.shape);
    return {math::linear_interpolate(track.from.x, track.to.x, factor),
            math::linear_interpolate(track.from.y, track.to.y, factor)};
  }
  void prompt(const MenuPrompt& prompt, float x, float y, float alpha = 1) {
    constexpr float kSize = static_cast<float>(kInputIconPixels) / 3;
    for (unsigned i = 0; i < prompt.count; ++i) {
      append({find_image(kIconAtlas).texture,
              {x + static_cast<float>(i) * prompt.icon_pitch, y, kSize, kSize},
              input_icon_uv(prompt.icons.at(i)),
              {1, 1, 1, alpha}});
    }
  }
  void footer(const MenuPresentation& view) {
    constexpr float kConfirmX = 32;
    constexpr float kBackX = 232;
    constexpr float kIconY = 26;

    // Fixed UI mode2: copied prompts reflect controller admission, not gameplay
    // remaps. Source text advance and pivot-derived icon pitch are shared.
    constexpr float kTextInset = 6, kTextY = 39, kSize = 22;
    prompt(view.prompts.confirm, kConfirmX, kIconY);
    prompt(view.prompts.back, kBackX, kIconY);
    text(common_labels.at(2).data(),
         {kConfirmX + kTextInset + view.prompts.confirm.text_advance, kTextY,
          kSize},
         kText);
    text(common_labels.at(3).data(),
         {kBackX + kTextInset + view.prompts.back.text_advance, kTextY, kSize},
         kText);
  }
  void dialog(const MenuPresentation& view) {
    constexpr float kCanvasWidth = 1280;
    constexpr float kPanelAlphaByte = 178.F;
    constexpr float kColorMaximum = 255;
    constexpr unsigned kDialogEdge = 326;
    constexpr unsigned kDialogButton = 327;
    constexpr float kHalf = .5F;
    constexpr float kInactiveAlpha = .8F;
    constexpr float kCenterX = 640;
    constexpr float kFontSize = 48;
    constexpr float kAcknowledgeX = 1076;
    constexpr float kAcknowledgeInset = 64;

    if (view.dialog == FrontendDialog::none) return;
    const std::string_view label =
        dialog_labels.at(static_cast<std::size_t>(view.dialog)).data();
    const bool choices = view.dialog == FrontendDialog::change_costume ||
                         view.dialog == FrontendDialog::save_costume ||
                         view.dialog == FrontendDialog::continue_game ||
                         view.dialog == FrontendDialog::profile_overwrite ||
                         view.dialog == FrontendDialog::profile_enable_autosave;
    const auto lines =
        1 + static_cast<unsigned>(std::ranges::count(label, '\n'));
    const auto geometry =
        menu_dialog_geometry(view.dialog_view.progress, lines, choices);
    if (geometry.bottom > geometry.top) {
      image(kWhiteImage,
            {0, geometry.top, kCanvasWidth, geometry.bottom - geometry.top},
            {0, 0, 0, kPanelAlphaByte / kColorMaximum * geometry.opacity});
    }
    const auto& edge = find_image(kDialogEdge);
    image(kDialogEdge, {0, geometry.top - edge.height * kHalf},
          {1, 1, 1, geometry.opacity});
    image(kDialogEdge, {0, geometry.bottom - edge.height * kHalf},
          {1, 1, 1, geometry.opacity});
    auto tint = kText;
    tint.alpha = geometry.decoration;
    text(label, {kCenterX, geometry.text_y, kFontSize}, tint, true);
    if (!view.dialog_interactive) return;
    if (!choices) {
      const auto settled = menu_dialog_geometry(1, lines, false);
      prompt(view.prompts.confirm, kAcknowledgeX,
             settled.bottom - kAcknowledgeInset, geometry.decoration);
      return;
    }
    const auto& button = find_image(kDialogButton);
    for (unsigned choice = 0; choice < 2; ++choice) {
      const auto x = choice == 0 ? 448.F : 832.F;
      const bool selected = (choice == 0) == view.dialog_view.yes;
      auto color = selected
                       ? kText
                       : render::Color{kText.red * kHalf, kText.green * kHalf,
                                       kText.blue * kHalf, kInactiveAlpha};
      auto button_color = selected ? render::Color{} : color;
      button_color.alpha *= geometry.decoration;
      image(
          kDialogButton,
          {x - button.width * kHalf, geometry.choice_y - button.height * kHalf},
          button_color);
      color.alpha *= geometry.decoration;
      text(common_labels.at(choice).data(), {x, geometry.choice_y, kFontSize},
           color, true);
    }
  }
  void stage(const StageMenuFeedback& view) {
    constexpr unsigned kDecorationCount = 10;
    constexpr unsigned kCommentImage = 364;
    constexpr std::size_t kHeadingCapacity = 128;
    constexpr float kHeadingX = 56;
    constexpr float kHeadingY = 270;
    constexpr float kHeadingSize = 38;
    constexpr float kTitleX = 72;
    constexpr float kTitleY = 314;
    constexpr float kLabelX = 60;
    constexpr float kRankLabelY = 394;
    constexpr float kLabelSize = 28;
    constexpr float kRankX = 368;
    constexpr float kRankY = 436;
    constexpr float kRankScale = .6F;
    constexpr float kScoreLabelY = 482;
    constexpr unsigned kDigitImage = 282;
    constexpr float kScoreX = 420;
    constexpr float kScoreY = 504;
    constexpr int kVisibleCards = 5;
    constexpr unsigned kCardImage = 319;
    constexpr unsigned kCardOverlay = 318;
    constexpr unsigned kArrowUp = 321;
    constexpr unsigned kArrowDown = 320;
    constexpr float kArrowX = 556;
    constexpr float kArrowUpY = 252;
    constexpr float kArrowDownY = 423;
    constexpr unsigned kConceptTitle = 323;
    constexpr unsigned kCampaignTitle = 322;

    if (!view.visible || view.count == 0 || view.selected >= view.count) {
      throw std::invalid_argument("Invalid stage presentation");
    }
    constexpr float kDuration = 1.1F, kPitch = 298, kPreviewInset = 70;
    constexpr float kHalf = .5F;
    image(kStageBackground, {0, 0});
    for (std::size_t i = 0; i < kDecorationCount; ++i) {
      image(kStageTracks.at(i).image,
            track_position(kStageTracks.at(i), view.motion, kDuration));
    }
    const auto comment =
        track_position(kStageTracks.at(12), view.motion, kDuration);
    image(kCommentImage, comment);
    const auto id = view.identities.at(view.selected);
    const auto ordinal = view.is_concept ? (id < 16   ? id + 1
                                            : id < 32 ? id - 9
                                                      : id - 19)
                                         : id;
    std::array<char, kHeadingCapacity> heading{};
    const std::string_view prefix =
        stage_detail_labels.at(view.is_concept ? 1 : 0).data();
    if (prefix.size() + 2 >= heading.size()) {
      throw std::length_error("Stage heading too long");
    }
    std::ranges::copy(prefix, heading.begin());
    heading.at(prefix.size()) = ' ';
    auto remaining = std::span(heading).subspan(prefix.size() + 1);
    const auto converted =
        std::to_chars(remaining.data(), &remaining.back(), ordinal);
    if (converted.ec != std::errc{}) {
      throw std::length_error("Stage ordinal too long");
    }
    *converted.ptr = '\0';
    text(heading.data(),
         {comment.x + kHeadingX, comment.y + kHeadingY, kHeadingSize}, kText);
    text(view.is_concept ? concept_labels.at(id).data()
                         : stage_labels.at(id - 1).data(),
         {comment.x + kTitleX, comment.y + kTitleY, kHeadingSize}, kText);
    text(stage_detail_labels.at(2).data(),
         {comment.x + kLabelX, comment.y + kRankLabelY, kLabelSize}, kText);
    constexpr std::array<unsigned, 7> kRanks{287, 286, 285, 284, 283, 288, 289};
    const auto& rank = find_image(kRanks.at(view.best_rank));
    image(kRanks.at(view.best_rank),
          {comment.x + kRankX - rank.width * kHalf,
           comment.y + kRankY - rank.height * kHalf},
          {}, kRankScale);
    text(stage_detail_labels.at(3).data(),
         {comment.x + kLabelX, comment.y + kScoreLabelY, kLabelSize}, kText);
    // The original score renderer takes decimal digits from least significant
    // to most and lays out the authored 50x80 digit cells right-to-left.
    const auto& digits = find_image(kDigitImage);
    auto score = view.best_score;
    float score_x = comment.x + kScoreX;
    for (;;) {
      constexpr float kWidth = 50, kHeight = 80, kDigitScale = .8F;
      constexpr unsigned kDecimalBase = 10;
      const auto digit = score % kDecimalBase;
      score_x -= kWidth * kDigitScale;
      append({digits.texture,
              {score_x + kWidth * (1 - kDigitScale) * kHalf,
               comment.y + kScoreY + kHeight * (1 - kDigitScale) * kHalf,
               kWidth * kDigitScale, kHeight * kDigitScale},
              {static_cast<float>(digit) * kWidth / digits.width, 0,
               kWidth / digits.width, kHeight / digits.height},
              {}});
      score /= kDecimalBase;
      if (score == 0) break;
    }
    const auto card =
        track_position(kStageTracks.at(11), view.motion, kDuration);
    const auto first = -2 - static_cast<int>(view.motion.scroll / kPitch);
    const auto remainder = std::fmod(view.motion.scroll, kPitch);
    for (int i = 0; i < kVisibleCards; ++i) {
      const auto index = first + i;
      const auto y = card.y + static_cast<float>(i) * kPitch + remainder;
      image(kCardImage, {card.x, y});
      if (index >= 0 && static_cast<std::size_t>(index) < view.count) {
        const auto identity =
            view.identities.at(static_cast<std::size_t>(index));
        const auto distance = std::min(149.F, std::abs(card.y + 596 - y));
        const auto alpha = (1 - distance / 149.F) * kRankScale + .4F;
        image((view.is_concept ? kConceptImageBase : kCampaignImageBase) +
                  identity,
              {card.x, y + kPreviewInset}, {1, 1, 1, alpha});
      }
      image(kCardOverlay, {card.x, y + kPreviewInset});
    }
    const auto wobble = math::periodic_sine_interpolate(
        math::PeriodicSineEndpoints<float>{-3, 3}, view.motion.pulse);
    if (view.selected != 0) image(kArrowUp, {kArrowX, kArrowUpY + wobble});
    if (view.selected + 1 < view.count) {
      image(kArrowDown, {kArrowX, kArrowDownY - wobble});
    }
    auto title = track_position(kStageTracks.at(kDecorationCount), view.motion,
                                kDuration);
    image(view.is_concept ? kConceptTitle : kCampaignTitle, title);
  }
  void costume(const MenuPresentation& view) {
    constexpr float kPreviewInset = 36;
    constexpr unsigned kRowImage = 55;
    constexpr unsigned kRowHighlight = 54;
    constexpr unsigned kSelectionImage = 56;
    constexpr unsigned kCurrentCostumeImage = 451;
    constexpr float kShadowX = 231;
    constexpr float kShadowY = 84;
    constexpr float kLabelX = 223;
    constexpr float kLabelY = 76;
    constexpr float kLabelSize = 48;
    constexpr unsigned kScrollTop = 57;
    constexpr unsigned kScrollBottom = 58;
    constexpr unsigned kScrollMiddle = 59;
    constexpr float kScrollX = 448;

    constexpr float kDuration = 1.3F, kPitch = 172,
                    kSelectionHalfHeight = 2.1F * kPitch;
    image(kCustomBackground, {0, 0});
    for (const auto index : {0U, 1U, 2U, 6U, 7U}) {
      image(kCostumeTracks.at(index).image,
            track_position(kCostumeTracks.at(index), view.child, kDuration));
    }
    const auto panel =
        track_position(kCostumeTracks.at(7), view.child, kDuration);
    const auto selected = view.rows.at(view.selected).compatibility_id;
    if (selected >= kCostumeCount) {
      throw std::runtime_error("Unsupported native costume identity");
    }
    image(selected == 0 ? kDefaultCostumeImage : kCostumeImageBase + selected,
          {panel.x + kPreviewInset, panel.y + kPreviewInset});
    const auto list =
        track_position(kCostumeTracks.at(8), view.child, kDuration);
    const float center = list.y + 2 * kPitch;
    const auto shimmer = math::periodic_sine_interpolate(
        math::PeriodicSineEndpoints<float>{0, -.2F}, view.child.pulse * .5F);
    for (int i = -2; i < static_cast<int>(view.count) + 2; ++i) {
      const auto y =
          list.y + static_cast<float>(i + 2) * kPitch + view.child.scroll;
      if (std::abs(center - y) >= kSelectionHalfHeight) continue;
      image(kRowImage, {list.x, y});
      if (i < 0 || static_cast<std::size_t>(i) >= view.count) continue;
      const auto row = static_cast<std::size_t>(i);
      const auto id = view.rows.at(row).compatibility_id;
      // Authored label draws a four-pixel black offset before the pale glyphs.
      text(costume_labels.at(id).data(),
           {list.x + kShadowX, y + kShadowY, kLabelSize}, {0, 0, 0, 1}, true);
      text(costume_labels.at(id).data(),
           {list.x + kLabelX, y + kLabelY, kLabelSize}, kText, true);
      const auto shade = std::clamp(1 + shimmer, 0.F, 1.F);
      image(kRowHighlight, {list.x, y},
            {shade, shade, shade, view.child.highlight.at(row)});
      if (row == view.selected) image(kSelectionImage, {list.x, y});
      if (id == view.child.current_costume) {
        image(kCurrentCostumeImage, {list.x, y});
      }
    }
    // Depth2 occluders mask rows beyond the original scrolling aperture.
    for (const auto index : {3U, 4U, 5U}) {
      image(kCostumeTracks.at(index).image,
            track_position(kCostumeTracks.at(index), view.child, kDuration));
    }
    if (view.count > 1) {
      const auto scroll =
          track_position(kCostumeTracks.at(6), view.child, kDuration);
      constexpr float kHeight = 409, kCap = 40, kThumb = 78;
      const auto fraction = static_cast<float>(view.selected) /
                            static_cast<float>(view.count - 1);
      const auto top = scroll.y + 132;
      image(kScrollTop, {scroll.x + kScrollX, top});
      image(kScrollBottom, {scroll.x + kScrollX, top + kHeight - kCap});
      image(kScrollMiddle,
            {scroll.x + kScrollX,
             top + kCap + (kHeight - 2 * kCap - kThumb) * fraction});
    }
    footer(view);
  }
  void difficulty(const MenuPresentation& view) {
    constexpr float kDescriptionX = 315;
    constexpr float kDescriptionY = 575;
    constexpr float kDescriptionSize = 32;
    constexpr unsigned kCursorImage = 343;

    constexpr float kDuration = 1.8F;
    if (view.stage.visible) {
      stage(view.stage);
    } else {
      image(kMainBackground, {0, 0});
      constexpr std::array<render::Rect, 4> kCampaignPositions{
          {{-116, 0}, {8, 272}, {55, 414}, {68, 96}}};
      for (std::size_t i = 0; i < kCampaignPositions.size(); ++i) {
        image(kMainTracks.at(i).image, kCampaignPositions.at(i));
      }
    }
    for (std::size_t i = 0; i < view.count; ++i) {
      const auto id = view.rows.at(i).compatibility_id;
      image(kDifficultyTracks.at(id).image,
            track_position(kDifficultyTracks.at(id), view.child, kDuration));
      const auto elapsed = view.child.motion == ChildMenuMotion::active
                               ? kDuration
                           : view.child.motion == ChildMenuMotion::leaving
                               ? kDuration - view.child.elapsed
                               : view.child.elapsed;
      const auto scale_y = main_track_factor(elapsed, 1.2F, kDuration, 0);
      if (scale_y > 0) {
        const auto anchor = difficulty_row_positions.at(id);
        render::Rect position{anchor.at(0), anchor.at(1)};
        position.width = find_image(kDifficultyImages.at(id)).width;
        const auto original_height =
            find_image(kDifficultyImages.at(id)).height;
        constexpr float kHalf = .5F;
        position.height = original_height * scale_y;
        position.y += original_height * (1 - scale_y) * kHalf;
        const auto shade = view.child.highlight.at(i);
        image(kDifficultyImages.at(id), position, {shade, shade, shade, 1});
      }
    }
    for (std::size_t i = 4; i < kMainTracks.size(); ++i) {
      image(kMainTracks.at(i).image,
            track_position(kMainTracks.at(i), view.child, kDuration));
    }
    const auto id = view.rows.at(view.selected).compatibility_id;
    auto tint = kText;
    tint.alpha = std::clamp(view.child.description_alpha, 0.F, 1.F);
    text(difficulty_descriptions.at(id).data(),
         {kDescriptionX, kDescriptionY, kDescriptionSize}, tint);
    if (view.child.selector_opacity > 0 && view.child.selector_scale > 0) {
      const render::Rect position{view.child.selector_position.at(0),
                                  view.child.selector_position.at(1)};
      image(kCursorImage, position, {1, 1, 1, view.child.selector_opacity},
            view.child.selector_scale);
      if (view.child.selector_echo > 0) {
        constexpr unsigned kConfirmationImage = 344;
        image(kConfirmationImage, position, {1, 1, 1, view.child.selector_echo},
              view.child.selector_scale);
      }
    }
    footer(view);
  }
};
FrontendMenuSurface::FrontendMenuSurface(resources::ResourceService& resources,
                                         render::SpriteOutput& renderer,
                                         resources::ArchiveBytes menu_text,
                                         resources::ArchiveBytes input_icons)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         renderer, std::move(menu_text),
                                         std::move(input_icons))) {}
FrontendMenuSurface::~FrontendMenuSurface() = default;
void FrontendMenuSurface::begin() { state_->begin(false); }
void FrontendMenuSurface::begin_pause() { state_->begin(true); }
bool FrontendMenuSurface::ready() {
  auto& state = *state_;
  if (!state.requested) throw std::logic_error("Menu assets not requested");
  if (state.constructed) return true;
  const auto report = state.resources.pump(kMenuTag);
  if (report.state == resources::LoadState::failed) {
    throw std::runtime_error(report.diagnostic);
  }
  if (report.state != resources::LoadState::ready) return false;
  state.construct();
  // Observe construction completion on the next update, as with the title
  // surface.
  return false;
}
void FrontendMenuSurface::show(const MenuPresentation& presentation) {
  auto& state = *state_;
  if (!state.constructed || !state.font) {
    throw std::logic_error("Menu cannot draw before construction");
  }
  if (state.pause_only && presentation.page != MenuPage::options &&
      presentation.page != MenuPage::costume &&
      presentation.page != MenuPage::confirm) {
    throw std::invalid_argument("Pause surface cannot present front-end pages");
  }
  if (presentation.count > presentation.rows.size() ||
      (presentation.count != 0 &&
       presentation.selected >= presentation.count)) {
    throw std::invalid_argument("Invalid menu row selection");
  }
  if (!std::isfinite(presentation.opacity) || presentation.opacity < 0 ||
      presentation.opacity > 1) {
    throw std::invalid_argument("Invalid menu fade opacity");
  }
  if (presentation.page == MenuPage::main) {
    const auto& feedback = presentation.main;
    if (!std::isfinite(feedback.confirmation_elapsed) ||
        feedback.confirmation_elapsed < 0 ||
        feedback.confirmation_elapsed > 1 || !std::isfinite(feedback.elapsed) ||
        feedback.elapsed < 0 || !std::isfinite(feedback.description_alpha)) {
      throw std::invalid_argument("Invalid main menu animation snapshot");
    }
    for (std::size_t i = 0; i < feedback.row_y.size(); ++i) {
      if (!std::isfinite(feedback.row_y.at(i)) ||
          !std::isfinite(feedback.row_scale.at(i)) ||
          feedback.row_scale.at(i) <= 0) {
        throw std::invalid_argument("Invalid main menu selector snapshot");
      }
    }
  }
  if (!valid_menu_prompt(presentation.prompts.confirm) ||
      !valid_menu_prompt(presentation.prompts.back)) {
    throw std::invalid_argument("Invalid frontend input prompts");
  }
  state.count = 0;
  if (presentation.storage_background) {
    constexpr render::Rect kStorageBackdrop{0, 0, 1280, 720};
    state.image(kWhiteImage, kStorageBackdrop, {0, 0, 0, 1});
  } else if (presentation.page == MenuPage::main) {
    if (presentation.count != kMainImages.size()) {
      throw std::invalid_argument("Original main menu has six entries");
    }
    state.image(kMainBackground, {0, 0, 0, 0});
    const auto& feedback = presentation.main;
    const bool entering = feedback.motion == MainMenuMotion::entering;
    const bool leaving = feedback.motion == MainMenuMotion::leaving;
    const bool returning = feedback.motion == MainMenuMotion::returning;
    constexpr std::array<render::Rect, 4> kCampaignPositions{
        {{-116, 0}, {8, 272}, {55, 414}, {68, 96}}};
    constexpr std::array<float, 4> kCampaignBegin{.6F, 0, 0, .5F};
    constexpr std::array<float, 4> kCampaignEnd{1.9F, 1.3F, 1, 1.6F};
    for (std::size_t index = 0; index < kMainTracks.size(); ++index) {
      const auto& track = kMainTracks.at(index);
      auto factor = entering ? main_track_factor(feedback.elapsed, track.begin,
                                                 track.end, track.shape)
                             : 1.F;
      auto from = track.from, to = track.to;
      if (leaving && presentation.selected < 2) {
        if (index < kCampaignPositions.size()) {
          from = track.to;
          to = kCampaignPositions.at(index);
          factor = main_track_factor(feedback.elapsed, kCampaignBegin.at(index),
                                     kCampaignEnd.at(index), 0);
        } else {
          factor = main_track_factor(track.end - feedback.elapsed, track.begin,
                                     track.end, track.shape);
        }
      } else if ((leaving || returning) &&
                 (presentation.selected == 2 || presentation.selected == 4)) {
        const auto elapsed =
            returning ? 1.F - feedback.elapsed : feedback.elapsed;
        factor = main_track_factor(track.end - elapsed * 2, track.begin,
                                   track.end, track.shape);
      }
      state.image(track.image,
                  {math::linear_interpolate(from.x, to.x, factor),
                   math::linear_interpolate(from.y, to.y, factor), 0, 0});
    }
    for (std::size_t i = 0; i < kMainImages.size(); ++i) {
      auto position = kMainPositions.at(i);
      float scale = feedback.row_scale.at(i), alpha = 1;
      if (entering) {
        const auto factor = main_track_factor(
            feedback.elapsed, kMainEntryBegin.at(i), kMainEntryEnd.at(i), 0);
        position.x = math::linear_interpolate(kMainEntryPositions.at(i).x,
                                              position.x, factor);
        position.y = math::linear_interpolate(kMainEntryPositions.at(i).y,
                                              position.y, factor);
        constexpr float kEntryScale = 6;
        scale *= math::linear_interpolate(kEntryScale, 1.F, factor);
      } else if ((leaving || returning) &&
                 (i != presentation.selected ||
                  (presentation.selected == 2 || presentation.selected == 4))) {
        constexpr float kExitDuration = .9F, kExitScale = .1F;
        const auto elapsed =
            returning ? kExitDuration - feedback.elapsed
                      : feedback.elapsed -
                            (i == presentation.selected ? kExitDuration : 0.F);
        const auto factor = main_track_factor(elapsed, 0, kExitDuration, 1);
        scale *= math::linear_interpolate(1.F, kExitScale, factor);
        alpha = std::clamp(1 - factor, 0.F, 1.F);
      }
      position.y += feedback.row_y.at(i);
      constexpr float kInactiveTint = .4F, kDisabledAdd = -.11F;
      const auto tint = (i == presentation.selected ? 1.F : kInactiveTint) +
                        (presentation.rows.at(i).enabled ? 0.F : kDisabledAdd);
      if (alpha > 0 && scale > 0) {
        state.image(kMainImages.at(i), position, {tint, tint, tint, alpha},
                    scale);
        // The native selected-row echo is a separate ordered draw: its
        // authored descriptor widens only X and fades over 0.3 seconds.
        constexpr float kEchoDuration = .3F, kEchoAlpha = 96.F / 255,
                        kEchoWidth = 3.F;
        if (i == presentation.selected &&
            feedback.confirmation_elapsed < kEchoDuration) {
          const auto factor = feedback.confirmation_elapsed / kEchoDuration;
          state.image(kMainImages.at(i), position,
                      {1, 1, 1, kEchoAlpha * (1 - factor)}, scale);
          auto& echo = state.commands.at(state.count - 1).destination;
          const auto width = echo.width;
          echo.width *= math::linear_interpolate(1.F, kEchoWidth, factor);
          constexpr float kHalf = .5F;
          echo.x += (width - echo.width) * kHalf;
        }
        if (!presentation.rows.at(i).enabled) {
          constexpr std::array<float, 6> kDisabledX{0, 9, 8, 0, 8, 8};
          constexpr std::array<float, 6> kDisabledY{0, -10, -7, 0, -9, -6};
          constexpr std::array<float, 6> kDisabledRotation{0, .03F,  -.035F,
                                                           0, -.02F, -.01F};
          auto marker = position;
          marker.x += kDisabledX.at(i);
          marker.y += kDisabledY.at(i);
          constexpr unsigned kDisabledMarker = 359;
          state.image(kDisabledMarker, marker, {1, 1, 1, alpha}, scale);
          state.commands.at(state.count - 1).rotation_radians =
              kDisabledRotation.at(i);
        }
      }
    }
    constexpr render::Color kDescriptionColor{230.F / 255, 228.F / 255,
                                              213.F / 255, 1};
    auto description_color = kDescriptionColor;
    description_color.alpha = std::clamp(feedback.description_alpha, 0.F, 1.F);
    constexpr TextPosition kDescriptionPosition{315, 575, 32};
    state.text(state.descriptions.at(presentation.selected).data(),
               kDescriptionPosition, description_color);
    state.footer(presentation);
  } else if (presentation.page == MenuPage::costume &&
             presentation.count != 0) {
    state.costume(presentation);
  } else if (presentation.page == MenuPage::stage ||
             presentation.page == MenuPage::extra) {
    state.stage(presentation.stage);
    state.footer(presentation);
  } else if (presentation.page == MenuPage::difficulty) {
    state.difficulty(presentation);
  } else if (presentation.page == MenuPage::confirm) {
    // Native Continue overlays the campaign-positioned main artwork.
    state.image(kMainBackground, {0, 0});
    constexpr std::array<render::Rect, 4> kCampaignPositions{
        {{-116, 0}, {8, 272}, {55, 414}, {68, 96}}};
    for (std::size_t i = 0; i < kCampaignPositions.size(); ++i) {
      state.image(kMainTracks.at(i).image, kCampaignPositions.at(i));
    }
  } else if (presentation.page != MenuPage::options &&
             presentation.page != MenuPage::complete) {
    throw std::invalid_argument("Unsupported native frontend page");
  }
  state.dialog(presentation);
  for (auto& command : std::span(state.commands).first(state.count)) {
    command.color.alpha *= presentation.opacity;
  }
  state.renderer.set_sprites(std::span(state.commands).first(state.count));
}
// Status is a separate sprite publication over the retained live world.
void FrontendMenuSurface::show_status(std::string_view text) {
  auto& state = *state_;
  if (!state.constructed || !state.font) {
    throw std::logic_error("Menu cannot draw status before construction");
  }
  constexpr std::size_t kStatusLimit = 120;
  constexpr TextPosition kStatusPosition{20, 680, 18};
  constexpr render::Color kStatusColor{1, 1, 1, 1};
  state.count = 0;
  state.text(text.substr(0, kStatusLimit), kStatusPosition, kStatusColor);
  state.renderer.set_sprites(std::span(state.commands).first(state.count));
}
void FrontendMenuSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
