// Original post-result power-up art and localization, sampled from explicit
// frontend presentation channels. No Record, input, clock or purchase
// authority. Prepare bounded resources once; validate immutable snapshot; emit
// source layout/atlas digits/text; publish one ordered submission. Native
// sprite tracks use the shared source eased-tangent algorithm. Resource cleanup
// is RAII and partial setup unwinds. Core E.6 (#re-raii), SL.con.3
// (#rsl-bounds), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), implementation phase
// consulted.
#include "power_up_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/image.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "bw/math/scalar.hpp"
#include "game/ui/common/sprite_text.hpp"
namespace bw::game {
namespace {
constexpr resources::ResourceTag kPowerUpTag = 19;
constexpr std::array<std::string_view, 5> kGroups{
    "sprite.powerup", "sprite.system", "sprite.font", "sprite.text.pup",
    "sprite.text.sys"};
constexpr std::size_t kImageCapacity = 46, kTextCount = 45, kTextCapacity = 256;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
constexpr render::Color kDimText{kText.red * .4F, kText.green * .4F,
                                 kText.blue * .4F, 1};
struct Position {
  float x{}, y{};
};
struct Track {
  unsigned image{};
  Position first, last;
  float begin{}, end{}, shape{}, first_scale{1};
  bool tangent_response{true};
};
constexpr std::array<Track, 6> kOuterTracks{
    {{248, {-640, 252}, {-4, 252}, 0, .8F, .8F},
     {377, {2140, 402}, {869, 402}, .3F, 1.2F, .6F},
     {375, {1396, 468}, {116, 468}, 0, .8F, 1, 1, false},
     {376, {1366, 624}, {86, 624}, 0, 1.1F, .6F},
     {400, {1094, 910}, {794, 610}, 0, 1.1F, 1, 1, false},
     {262, {-640, 0}, {-4, -4}, .2F, 1, .6F}}};
constexpr std::array<Track, 4> kCategoryTracks{
    {{263, {-1368, 50}, {152, 370}, 0, .85F, .3F, 6},
     {266, {-1368, 184}, {152, 424}, .2F, 1.15F, .3F, 6},
     {267, {-1368, 978}, {152, 478}, .05F, .95F, .3F, 6},
     {264, {-1468, 882}, {152, 532}, .1F, 1.05F, .3F, 6}}};
constexpr std::array<unsigned, 3> kRows{2, 4, 6};
constexpr std::array<unsigned, 3> kFirstLabel{1, 3, 7};
constexpr std::array<std::array<unsigned, 6>, 3> kIllustrations{
    {{254, 261}, {252, 253, 250, 251}, {256, 255, 258, 260, 259, 257}}};
// Recovered native canvas geometry and compatibility atlas identities.
constexpr unsigned kBackgroundImage = 236, kFirstSystemImage = 324,
                   kLastSystemImage = 327, kFooterImage = 400,
                   kSolidImage = 10000, kWalletImage = 265,
                   kUpgradeDigitsImage = 240, kLevelMarkerImage = 242,
                   kMaximumImage = 241, kRowMarkerImage = 247,
                   kDialogEdgeImage = 326, kDialogButtonImage = 327;
constexpr unsigned kWalletMaximum = 10000, kTwoDigitLimit = 100,
                   kCategoryDescriptionFirst = 13, kStatDescriptionFirst = 19,
                   kLockedWeaponDescriptionFirst = 21,
                   kWeaponDescriptionFirst = 25, kSpellDescriptionFirst = 29;
constexpr float kHalf = .5F, kCategoryOffset = 40, kTextSize = 32,
                kRowSpacing = 61;
constexpr Position kWalletRight{480, 240}, kWalletCell{40, 44},
    kLevelMarkerOffset{22, 311}, kLevelDigitOffset{212, 334},
    kUpgradeCell{60, 70}, kMaximumOffset{400, 334},
    kWeaponIllustrationOffset{-18, 15}, kIllustrationOffset{32, 29},
    kRowMarkerOffset{19, 19}, kRowTextOffset{78, 30},
    kDescriptionOffset{40, 432};
constexpr float kLevelMarkerStep = 37, kCostShortRight = 522,
                kCostLongRight = 582;
constexpr SpriteTextPosition kCategoryDescription{315, 575, 32};
constexpr Track kChildBase{239, {978, -467}, {978, 249}, .1F, .7F, 1},
    kChildDetail{238, {596, -720}, {596, -4}, 0, .67F, .4F},
    kChildIllustration{249, {799, -642}, {767, 45}, 0, .67F, .4F};
constexpr std::array<Track, 3> kChildLists{
    {{243, {180, 917}, {180, 413}, 0, .67F, .6F},
     {244, {180, 853}, {180, 349}, 0, .67F, .6F},
     {245, {180, 795}, {180, 291}, 0, .67F, .6F}}};
constexpr unsigned kFullWidthLead = 0xef, kFullWidthBase = 0xf000;
constexpr float kDialogCenterX = 640, kDialogCenterY = 360, kDialogWidth = 1280,
                kDialogBlackAlpha = 178.F / 255, kInactiveChoiceAlpha = .8F;
constexpr unsigned kIconCount = 149, kIconIdentityBase = 20000;
constexpr std::array<unsigned, 4> kInputIcons{27, 1, 120, 121};
constexpr std::array<unsigned, 2> kPromptLabels{27, 28};
constexpr float kPromptX = 826, kPromptY = 636, kPromptStep = 200,
                kPromptAdvance = 44.8F, kPromptTextGap = 6,
                kPromptTextOffset = 13, kPromptTextSize = 22,
                kPromptScale = 1.F / 3;
enum class DigitAtlas : std::uint8_t { wallet, upgrade };
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
// Offset tables refer to immutable USER payloads, never native pointers.
std::uint32_t word(std::span<const std::uint8_t> bytes, std::size_t offset) {
  constexpr unsigned kByteBits = 8;
  constexpr std::size_t kWordBytes = 4;
  if (offset > bytes.size() || bytes.size() - offset < kWordBytes) {
    throw std::runtime_error("Power-up text table exceeds resource");
  }
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < kWordBytes; ++i) {
    value |= static_cast<std::uint32_t>(bytes[offset + i]) << (i * kByteBits);
  }
  return value;
}
void load_text(std::span<const std::uint8_t> bytes, unsigned id,
               std::span<char> output) {
  constexpr std::string_view kSignature = "STMANAGE";
  constexpr std::size_t kHeaderBytes = 44, kRecordBytes = 20,
                        kLanguageBytes = 16;
  constexpr unsigned kEnglish = 200;
  if (bytes.size() < kHeaderBytes ||
      !std::equal(kSignature.begin(), kSignature.end(), bytes.begin())) {
    throw std::runtime_error("Invalid original power-up text archive");
  }
  const auto rows = static_cast<std::size_t>(word(bytes, 12));
  const auto groups = static_cast<std::size_t>(word(bytes, 16));
  const auto languages = static_cast<std::size_t>(word(bytes, 20));
  const auto pool = static_cast<std::size_t>(word(bytes, 28));
  if (groups < rows || groups > bytes.size() ||
      (groups - rows) % kRecordBytes != 0) {
    throw std::runtime_error("Invalid original power-up record table");
  }
  const auto count = (groups - rows) / kRecordBytes;
  std::optional<std::size_t> record;
  constexpr unsigned kDecimalBase = 10, kIdentifierDigits = 4;
  for (std::size_t i = 0; i < count; ++i) {
    const auto entry = rows + i * kRecordBytes;
    const auto key = pool + word(bytes, entry);
    if (key > bytes.size() || bytes.size() - key <= kIdentifierDigits) {
      throw std::runtime_error("Original power-up identifier exceeds resource");
    }
    unsigned value = 0;
    bool decimal = bytes[key + kIdentifierDigits] == 0;
    for (unsigned digit = 0; digit < kIdentifierDigits; ++digit) {
      const auto character = bytes[key + digit];
      decimal = decimal && character >= '0' && character <= '9';
      value = value * kDecimalBase + character - '0';
    }
    if (decimal && value == id) {
      record = entry;
      break;
    }
  }
  if (!record) throw std::runtime_error("Missing original power-up text");
  const auto group = groups + word(bytes, *record + 8);
  const auto language_count = word(bytes, group + 4);
  const auto first_language = languages + word(bytes, group + 8);
  std::optional<std::size_t> offset;
  for (std::size_t i = 0; i < language_count; ++i) {
    const auto entry = first_language + i * kLanguageBytes;
    if (word(bytes, entry) == kEnglish) {
      offset = pool + word(bytes, entry + 4);
      break;
    }
  }
  if (!offset) {
    throw std::runtime_error("Original power-up English text missing");
  }
  std::size_t used = 0;
  for (auto cursor = *offset; cursor < bytes.size(); ++cursor) {
    auto codepoint = static_cast<unsigned>(bytes[cursor]);
    if (codepoint == 0) {
      output[used] = '\0';
      return;
    }
    constexpr unsigned kAscii = 128, kFullWidthFirst = 0xff01,
                       kFullWidthLast = 0xff5e, kFullWidthDelta = 0xfee0;
    if (codepoint >= kAscii) {
      if (codepoint != kFullWidthLead || bytes.size() - cursor < 3) {
        throw std::runtime_error("Unsupported original English text encoding");
      }
      constexpr unsigned kPayload = 0x3f, kSixBits = 6;
      codepoint = kFullWidthBase |
                  ((bytes[cursor + 1] & kPayload) << kSixBits) |
                  (bytes[cursor + 2] & kPayload);
      cursor += 2;
      if (codepoint < kFullWidthFirst || codepoint > kFullWidthLast) {
        throw std::runtime_error("Unsupported original English text character");
      }
      codepoint -= kFullWidthDelta;
    }
    if (used + 1 >= output.size()) {
      throw std::length_error("Original power-up text exceeds capacity");
    }
    output[used++] = static_cast<char>(codepoint);
  }
  throw std::runtime_error("Unterminated original power-up text");
}
float sample(const Track& track, float seconds) {
  const auto fraction =
      std::clamp((seconds - track.begin) / (track.end - track.begin), 0.F, 1.F);
  if (!track.tangent_response) return math::quintic_ease(fraction);
  return math::eased_tangent_interpolate(
      math::EasedTangentEndpoints<float>{0, 1, track.shape, track.shape},
      fraction);
}
unsigned selected_level(const PowerUpPresentation& view) {
  switch (view.category) {
    case UpgradeCategory::stat:
      return view.progression.levels.stats.at(view.upgrade_row);
    case UpgradeCategory::weapon:
      return view.progression.levels.weapons.at(view.upgrade_row);
    case UpgradeCategory::spell:
      return view.progression.levels.spells.at(view.upgrade_row);
  }
  throw std::invalid_argument("Invalid power-up category");
}
}  // namespace
struct PowerUpSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  resources::ResourceService& resources;
  render::SpriteOutput& output;
  memory::Allocator<std::byte> allocator;
  resources::ArchiveBytes input_icons;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, kImageCapacity> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<std::array<char, kTextCapacity>, kTextCount> text{};
  std::array<std::array<char, kTextCapacity>, 2> choices{}, prompts{};
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool begun{}, prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer,
        resources::ArchiveBytes icons)
      : resources(service),
        output(renderer),
        allocator(storage_for(service)),
        input_icons(std::move(icons)) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    if (begun) output.clear_sprites();
    while (image_count != 0) {
      output.release_texture(images.at(--image_count).texture);
    }
    font.reset();
    for (auto& lease : leases) lease.reset();
    resources.release(kPowerUpTag);
    begun = prepared = false;
    count = 0;
  }
  const Image& image(unsigned id) const {
    for (const auto& item : std::span(images).first(image_count)) {
      if (item.id == id) return item;
    }
    throw std::runtime_error("Original power-up image missing");
  }
  void begin() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kPowerUpTag);
  }
  bool ready() {
    if (prepared) return true;
    if (!begun) throw std::logic_error("Power-up surface has not begun");
    const auto report = resources.pump(kPowerUpTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) return false;
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      leases.at(group) = resources.acquire(kGroups.at(group));
      if (!leases.at(group)) {
        throw std::runtime_error("Power-up resource publication missing");
      }
      const auto& lease = leases.at(group).value();
      if (group == 3) {
        for (unsigned id = 1; id < text.size(); ++id) {
          load_text(lease.bytes(), id, text.at(id));
        }
        continue;
      }
      if (group == 4) {
        for (unsigned id = 1; id <= choices.size(); ++id) {
          load_text(lease.bytes(), id, choices.at(id - 1));
          load_text(lease.bytes(), kPromptLabels.at(id - 1),
                    prompts.at(id - 1));
        }
        continue;
      }
      const bool is_font = group == 2;
      if (is_font) font = formats::BitmapFont::parse(lease.bytes(), allocator);
      const auto& archive = lease.archive();
      std::size_t index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        constexpr std::size_t kIdentityBytes = 2;
        if (!is_font && index >= lease.bytes().size() / kIdentityBytes) {
          throw std::runtime_error("Power-up sprite identity missing");
        }
        constexpr unsigned kByteBits = 8;
        const auto id =
            is_font
                ? 0U
                : static_cast<unsigned>(lease.bytes()[index * kIdentityBytes]) |
                      (static_cast<unsigned>(
                           lease.bytes()[index * kIdentityBytes + 1])
                       << kByteBits);
        ++index;
        if (group == 1 && (id < kFirstSystemImage || id > kLastSystemImage) &&
            id != kFooterImage) {
          continue;
        }
        if (image_count == images.size()) {
          throw std::length_error("Power-up image capacity exceeded");
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        if (is_font &&
            (!font || index != 1 || decoded.width != font->atlas_width() ||
             decoded.height != font->atlas_height())) {
          throw std::runtime_error(
              "Power-up font atlas disagrees with metrics");
        }
        const auto texture = output.upload_texture(decoded);
        images.at(image_count++) = {id, texture,
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
      }
    }
    constexpr std::size_t kIconTable = 8, kWordBytes = 4;
    if (word(input_icons, 0) != kIconCount) {
      throw std::runtime_error("Unsupported native input icon pack");
    }
    for (const auto icon : kInputIcons) {
      const auto begin = word(input_icons, kIconTable + icon * kWordBytes);
      const auto end = word(input_icons, kIconTable + (icon + 1) * kWordBytes);
      if (begin < kIconTable + (kIconCount + 1) * kWordBytes || end < begin ||
          end > input_icons.size()) {
        throw std::runtime_error("Native input icon exceeds pack");
      }
      if (image_count == images.size()) {
        throw std::length_error("Power-up input texture capacity exceeded");
      }
      const auto decoded = formats::decode_png(
          std::span(input_icons).subspan(begin, end - begin), {}, allocator);
      images.at(image_count++) = {
          kIconIdentityBase + icon,
          output.upload_texture(decoded.width, decoded.height, decoded.rgba),
          static_cast<float>(decoded.width),
          static_cast<float>(decoded.height)};
    }
    if (image_count == images.size()) {
      throw std::length_error("Power-up primitive texture capacity exceeded");
    }
    constexpr std::array<std::uint8_t, 4> kWhitePixel{255, 255, 255, 255};
    images.at(image_count++) = {kSolidImage,
                                output.upload_texture(1, 1, kWhitePixel), 1, 1};
    prepared = true;
    return true;
  }
  void sprite(unsigned id, Position position, render::Color color = {},
              float scale = 1) {
    const auto& art = image(id);
    commands.at(count++) = {
        art.texture,
        {position.x, position.y, art.width * scale, art.height * scale},
        {0, 0, 1, 1},
        color};
  }
  Position track(const Track& motion, float seconds, render::Color color = {}) {
    const auto fraction = sample(motion, seconds);
    const Position position{std::lerp(motion.first.x, motion.last.x, fraction),
                            std::lerp(motion.first.y, motion.last.y, fraction)};
    sprite(motion.image, position, color,
           std::lerp(motion.first_scale, 1.F, fraction));
    return position;
  }
  void label(std::string_view value, SpriteTextPosition position,
             render::Color color = kText) {
    if (!font) throw std::logic_error("Power-up font is not prepared");
    count += append_sprite_text(*font, image(0).texture, value, position, color,
                                std::span(commands).subspan(count));
  }
  void digits(DigitAtlas atlas_kind, unsigned value, Position right,
              Position cell, float opacity = 1) {
    const auto& atlas = image(
        atlas_kind == DigitAtlas::wallet ? kWalletImage : kUpgradeDigitsImage);
    constexpr unsigned kRadix = 10;
    for (;;) {
      const auto digit = value % kRadix;
      value /= kRadix;
      right.x -= cell.x;
      commands.at(count++) = {atlas.texture,
                              {right.x, right.y, cell.x, cell.y},
                              {static_cast<float>(digit) * cell.x / atlas.width,
                               0, cell.x / atlas.width, cell.y / atlas.height},
                              {1, 1, 1, opacity}};
      if (value == 0) break;
    }
  }
  void outer(const PowerUpPresentation& view) {
    const auto elapsed =
        view.outer_exiting
            ? PowerUpPresentation::kOuterDuration - view.outer_seconds
            : view.outer_seconds;
    sprite(kBackgroundImage, {});
    for (const auto& motion : kOuterTracks) track(motion, elapsed);
    const auto selected = view.page == PowerUpPage::categories
                              ? view.focused
                              : static_cast<std::size_t>(view.category);
    for (std::size_t i = 0; i < kCategoryTracks.size(); ++i) {
      auto motion = kCategoryTracks.at(i);
      motion.first.y += view.category_offsets.at(i) - kCategoryOffset;
      motion.last.y += view.category_offsets.at(i) - kCategoryOffset;
      if (i == selected) {
        motion.first.y += view.category_bob;
        motion.last.y += view.category_bob;
      }
      const auto tint =
          i == selected ? .4F + .6F * view.category_selection_strength : .4F;
      track(motion, elapsed, {tint, tint, tint, 1});
    }
    auto description_color = kText;
    description_color.alpha = view.category_description_opacity;
    label(text.at(kCategoryDescriptionFirst + selected).data(),
          kCategoryDescription, description_color);
    if (elapsed >= 1.F) {
      digits(DigitAtlas::wallet,
             std::min(view.progression.points, kWalletMaximum), kWalletRight,
             kWalletCell, view.wallet_opacity);
    }
  }
  void child(const PowerUpPresentation& view) {
    const auto category = static_cast<std::size_t>(view.category);
    const auto elapsed =
        view.child_exiting
            ? PowerUpPresentation::kChildDuration - view.child_seconds
            : view.child_seconds;
    track(kChildBase, elapsed);
    const auto detail = track(kChildDetail, elapsed);
    const auto level = selected_level(view);
    for (unsigned i = 0; i < level; ++i) {
      sprite(kLevelMarkerImage, {detail.x + kLevelMarkerOffset.x +
                                     static_cast<float>(i) * kLevelMarkerStep,
                                 detail.y + kLevelMarkerOffset.y});
    }
    digits(DigitAtlas::upgrade, level,
           {detail.x + kLevelDigitOffset.x, detail.y + kLevelDigitOffset.y},
           kUpgradeCell);
    const auto cost =
        upgrade_cost(view.progression, {view.category, view.upgrade_row});
    if (cost) {
      digits(DigitAtlas::upgrade, *cost,
             {detail.x +
                  (*cost < kTwoDigitLimit ? kCostShortRight : kCostLongRight),
              detail.y + kLevelDigitOffset.y},
             kUpgradeCell);
    } else {
      sprite(kMaximumImage,
             {detail.x + kMaximumOffset.x, detail.y + kMaximumOffset.y});
    }
    const auto art = track(kChildIllustration, elapsed);
    const auto illustration = kIllustrations.at(category).at(view.upgrade_row);
    sprite(illustration, {art.x + (category == 1 ? kWeaponIllustrationOffset.x
                                                 : kIllustrationOffset.x),
                          art.y + (category == 1 ? kWeaponIllustrationOffset.y
                                                 : kIllustrationOffset.y)});
    const auto list = track(kChildLists.at(category), elapsed);
    const auto highlight = std::clamp(
        1 + view.child_highlight_strength * view.child_highlight_strength, 0.F,
        1.F);
    sprite(kRowMarkerImage,
           {list.x + kRowMarkerOffset.x,
            list.y + kRowMarkerOffset.y +
                static_cast<float>(view.upgrade_row) * kRowSpacing},
           {highlight, highlight, highlight, 1});
    for (unsigned row = 0; row < kRows.at(category); ++row) {
      label(text.at(kFirstLabel.at(category) + row).data(),
            {list.x + kRowTextOffset.x,
             list.y + kRowTextOffset.y + static_cast<float>(row) * kRowSpacing,
             kTextSize},
            row == view.upgrade_row ? kText : kDimText);
    }
    unsigned description = kStatDescriptionFirst + view.upgrade_row;
    if (category == 1) {
      description = (level == 0 ? kLockedWeaponDescriptionFirst
                                : kWeaponDescriptionFirst) +
                    view.upgrade_row;
    }
    if (category == 2) description = kSpellDescriptionFirst + view.upgrade_row;
    label(text.at(description).data(),
          {detail.x + kDescriptionOffset.x, detail.y + kDescriptionOffset.y,
           kTextSize});
  }
  float text_width(std::string_view value, float size) const {
    if (!font) throw std::logic_error("Power-up font is not prepared");
    float width = 0;
    for (const auto character : value) {
      const auto glyph = font->glyph(static_cast<unsigned char>(character));
      if (!glyph) throw std::runtime_error("Original power-up glyph missing");
      width += static_cast<float>(glyph->advance) * size /
               static_cast<float>(font->pixel_size());
    }
    return width;
  }
  void centered_text(std::string_view value, Position center, float size,
                     render::Color color) {
    std::size_t begin = 0;
    for (;;) {
      const auto end = value.find('\n', begin);
      const auto line = value.substr(
          begin, end == std::string_view::npos ? end : end - begin);
      label(line,
            {center.x - text_width(line, size) * kHalf, center.y - size * kHalf,
             size},
            color);
      if (end == std::string_view::npos) break;
      begin = end + 1;
      center.y += size;
    }
  }
  void dialog(const PowerUpPresentation& view) {
    const bool confirmation = view.notice == PowerUpNotice::none;
    const auto message =
        confirmation
            ? 17U
            : (view.notice == PowerUpNotice::insufficient_points ? 18U : 36U);
    const std::string_view content(text.at(message).data());
    constexpr float kTextSize = 48, kBorder = 36, kChoiceGap = 12,
                    kChoiceHeight = 64;
    const auto lines =
        1 + static_cast<unsigned>(std::ranges::count(content, '\n'));
    const auto text_height = static_cast<float>(lines) * kTextSize;
    const auto half_height = (text_height + 2 * kBorder +
                              (confirmation ? kChoiceGap + kChoiceHeight : 0)) *
                             kHalf;
    const auto progress = std::clamp(view.dialog_progress, 0.F, 1.F);
    const auto expansion = math::eased_tangent_interpolate(
        math::EasedTangentEndpoints<float>{0, 1, 0, 2},
        std::clamp(progress / .9F, 0.F, 1.F));
    const auto opacity = std::clamp((progress - .3F) / .3F, 0.F, 1.F);
    const auto decoration = std::clamp((progress - .7F) / .3F, 0.F, 1.F);
    const auto top = kDialogCenterY - expansion * half_height;
    const auto bottom = kDialogCenterY + expansion * half_height;
    commands.at(count++) = {image(kSolidImage).texture,
                            {0, top, kDialogWidth, bottom - top},
                            {0, 0, 1, 1},
                            {0, 0, 0, kDialogBlackAlpha * opacity}};
    const auto& edge = image(kDialogEdgeImage);
    sprite(kDialogEdgeImage, {0, top - edge.height * kHalf},
           {1, 1, 1, opacity});
    sprite(kDialogEdgeImage, {0, bottom - edge.height * kHalf},
           {1, 1, 1, opacity});
    auto color = kText;
    color.alpha = decoration;
    centered_text(content,
                  {kDialogCenterX,
                   kDialogCenterY - half_height + kBorder + kTextSize * kHalf},
                  kTextSize, color);
    if (!confirmation) {
      if (view.dialog_continue_visible) {
        constexpr float kContinueX = 1076, kContinueBottomOffset = 48;
        const auto icon = view.input_layout == PowerUpInputLayout::controller
                              ? kInputIcons.at(2)
                              : kInputIcons.at(0);
        sprite(
            kIconIdentityBase + icon,
            {kContinueX, kDialogCenterY + half_height - kContinueBottomOffset},
            {1, 1, 1, 1}, kPromptScale);
      }
      return;
    }
    const auto choice_y = kDialogCenterY + half_height - kBorder - 26;
    const auto& button = image(kDialogButtonImage);
    for (unsigned choice = 0; choice < 2; ++choice) {
      const Position center{choice == 0 ? 448.F : 832.F, choice_y};
      const bool selected = choice == view.focused;
      const auto button_color =
          selected ? render::Color{1, 1, 1, decoration}
                   : render::Color{kText.red * kHalf, kText.green * kHalf,
                                   kText.blue * kHalf,
                                   kInactiveChoiceAlpha * decoration};
      sprite(
          kDialogButtonImage,
          {center.x - button.width * kHalf, center.y - button.height * kHalf},
          button_color);
      auto choice_color =
          selected ? kText
                   : render::Color{kText.red * kHalf, kText.green * kHalf,
                                   kText.blue * kHalf, kInactiveChoiceAlpha};
      choice_color.alpha *= decoration;
      centered_text(choices.at(choice).data(), center, kTextSize, choice_color);
    }
  }
  void control_prompts(PowerUpInputLayout input_layout, float opacity) {
    if (opacity == 0) return;
    const auto first = input_layout == PowerUpInputLayout::controller
                           ? std::size_t{2}
                           : std::size_t{0};
    for (std::size_t action = 0; action < 2; ++action) {
      const auto x = kPromptX + static_cast<float>(action) * kPromptStep;
      sprite(kIconIdentityBase + kInputIcons.at(first + action), {x, kPromptY},
             {1, 1, 1, opacity}, kPromptScale);
      auto color = kText;
      color.alpha = opacity;
      label(prompts.at(action).data(),
            {x + kPromptAdvance + kPromptTextGap, kPromptY + kPromptTextOffset,
             kPromptTextSize},
            color);
    }
  }
  void show(const PowerUpPresentation& view);
};
PowerUpSurface::PowerUpSurface(resources::ResourceService& resources,
                               render::SpriteOutput& output,
                               resources::ArchiveBytes input_icons)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output, std::move(input_icons))) {}
PowerUpSurface::~PowerUpSurface() = default;
void PowerUpSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool PowerUpSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void PowerUpSurface::end() noexcept { state_->end(); }
void PowerUpSurface::show(const PowerUpPresentation& presentation) {
  state_->show(presentation);
}
void PowerUpSurface::State::show(const PowerUpPresentation& view) {
  if (!prepared) throw std::logic_error("Power-up surface is not prepared");
  const auto category = static_cast<std::size_t>(view.category);
  validate_progression(view.progression);
  if (!std::isfinite(view.category_bob) ||
      !std::ranges::all_of(view.category_offsets, [](float offset) {
        return std::isfinite(offset);
      })) {
    throw std::invalid_argument("Invalid category selector offset");
  }
  const std::array channels{view.wallet_opacity,
                            view.category_description_opacity,
                            view.category_selection_strength,
                            view.child_highlight_strength,
                            view.outer_prompt_opacity,
                            view.child_prompt_opacity};
  if (std::ranges::any_of(channels, [](float value) {
        return !std::isfinite(value) || value < 0 || value > 1;
      })) {
    throw std::invalid_argument("Invalid power-up presentation channel");
  }
  if (static_cast<unsigned>(view.input_layout) >
          static_cast<unsigned>(PowerUpInputLayout::controller) ||
      static_cast<unsigned>(view.page) >
          static_cast<unsigned>(PowerUpPage::confirmation) ||
      static_cast<unsigned>(view.notice) >
          static_cast<unsigned>(PowerUpNotice::maximum_level)) {
    throw std::invalid_argument("Invalid power-up presentation page");
  }
  if (category >= kRows.size() || view.upgrade_row >= kRows.at(category) ||
      !std::isfinite(view.outer_seconds) ||
      !std::isfinite(view.child_seconds) ||
      !std::isfinite(view.dialog_progress) || view.outer_seconds < 0 ||
      view.child_seconds < 0 || view.dialog_progress < 0 ||
      (view.page == PowerUpPage::categories && view.focused >= 4) ||
      (view.page == PowerUpPage::upgrades &&
       view.focused >= kRows.at(category)) ||
      (view.page == PowerUpPage::confirmation && view.focused >= 2)) {
    throw std::invalid_argument("Invalid native power-up presentation");
  }
  count = 0;
  outer(view);
  if (view.page != PowerUpPage::categories) child(view);
  if (view.page == PowerUpPage::confirmation ||
      view.notice != PowerUpNotice::none) {
    dialog(view);
  }
  control_prompts(view.input_layout, view.outer_prompt_opacity);
  if (view.page != PowerUpPage::categories) {
    control_prompts(view.input_layout, view.child_prompt_opacity);
  }
  output.set_sprites(std::span(commands).first(count));
}
}  // namespace bw::game
