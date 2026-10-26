// Original Options presentation: prepare archives once, animate the authored
// nine-track panel, then compose source labels and copied settings. The
// brightness child is the original display-calibration artwork/instructions;
// it does not manufacture an exposure setting. Unsupported operations are
// reported by the flow in a bounded notice. Resource handles and text owners
// remain here; authority stays in the game-owned flow. Core E.6/R.14 and
// performance MEM.9/GPU.1 require failure cleanup and no per-frame asset work.
// E.4 (#re-design-invariants) preserves the native positive-area draw contract
// at the authored dialog's collapsed endpoint without changing its animation.
#include "pause_options_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

#include "../formats/reader.hpp"
#include "bw/formats/bitmap_font.hpp"
#include "bw/formats/image.hpp"
#include "bw/formats/world_texture.hpp"
#include "bw/math/scalar.hpp"
#include "pause_surface_text.hpp"
#include "sprite_color.hpp"
#include "sprite_text.hpp"
namespace bw::game {
namespace {
constexpr resources::ResourceTag kTag = 22;
constexpr std::array<std::string_view, 5> kGroups{
    "sprite.option", "sprite.font", "sprite.system", "sprite.brightness",
    "sprite.text.sys"};
constexpr unsigned kWhite = 10000, kFont = 10001, kCalibration = 10002,
                   kIconBase = 11000;
constexpr std::size_t kImageCount = 28, kMenuTextCount = 86,
                      kTextCapacity = 256, kDialogTextCount = 6;
constexpr float kHalf = .5F, kFontSize = 32, kTrackDuration = 1.3F,
                kFadeDuration = .25F, kInactiveAlpha = .8F;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
constexpr std::array<unsigned, 8> kLabelRows{73, 76, 77, 78, 79, 75, 82, 45};
constexpr std::array<unsigned, 8> kDescriptions{0, 3, 4, 5, 6, 2, 9, 10};
constexpr std::array<unsigned, 6> kIcons{120, 121, 94, 54, 95, 39};
struct Position {
  float x{}, y{};
};
struct Track {
  unsigned id{};
  Position from, to;
  float begin{}, end{};
  bool eased{};
};
// Mission variant of the source's descriptor tracks. Frontend-only rows/art
// are intentionally absent; Costume adds one row without changing source rows.
constexpr std::array<Track, 9> kTracks{
    {{209, {0, 0}, {0, 0}, 0, 1, false},
     {218, {-730, 569}, {0, 569}, .2F, 1.3F, true},
     {217, {-730, 436}, {0, 436}, 0, 1, true},
     {220, {1296, 0}, {650, 0}, .2F, 1.3F, true},
     {219, {1296, 30}, {970, 30}, 0, 1, true},
     {400, {1296, 736}, {794, 610}, .2F, 1.2F, false},
     {224, {-730, 0}, {0, 0}, 0, 1, true},
     {211, {-730, 363}, {72, 363}, .2F, 1.2F, true},
     {225, {1296, 146}, {514, 146}, .2F, 1.2F, true}}};
Position track_position(const Track& track, float elapsed) {
  const auto t =
      std::clamp((elapsed - track.begin) / (track.end - track.begin), 0.F, 1.F);
  const auto blend = track.eased ? math::quintic_ease(t) : t;
  return {math::linear_interpolate(track.from.x, track.to.x, blend),
          math::linear_interpolate(track.from.y, track.to.y, blend)};
}
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
constexpr unsigned kArrowLeft = 212;
constexpr unsigned kArrowRight = 213;
constexpr unsigned kBracketLeft = 214;
constexpr unsigned kBracketRight = 215;
constexpr unsigned kSliderFill = 222;
constexpr unsigned kSliderFrame = 223;
constexpr unsigned kDialogEdge = 326, kDialogButton = 327;
constexpr unsigned kFooter = 400;
constexpr unsigned kSelectionMarker = 431;
constexpr unsigned kVolumeMaximum = 256;
constexpr float kCanvasWidth = 1280;
constexpr float kCanvasHeight = 720;
constexpr unsigned kColorMaximum = 255;
constexpr float kNoticeAlphaByte = 178.F;
constexpr unsigned kNoticeTop = 280;
constexpr unsigned kNoticeHeight = 160;
constexpr unsigned kNoticeUpperEdge = 244;
constexpr unsigned kNoticeLowerEdge = 440;
constexpr unsigned kCenterX = 640;
constexpr unsigned kNoticeTextY = 312;
constexpr unsigned kCalibrationTextY = 540;
constexpr unsigned kCalibrationIconX = 1030;
constexpr unsigned kCalibrationIconY = 664;
constexpr unsigned kCalibrationConfirmX = 1080;
constexpr unsigned kPanelX = 514;
constexpr unsigned kPanelY = 146;
constexpr float kUnselectedTintByte = 96.F;
constexpr float kFirstLabelY = 250.F;
constexpr float kFollowingLabelY = 252.F;
constexpr unsigned kRowPitch = 31;
constexpr unsigned kLabelX = 603;
constexpr unsigned kMarkerX = 567;
constexpr unsigned kMarkerY = 249;
constexpr float kAutosaveY = 407.F;
constexpr unsigned kOnLabel = 83U;
constexpr unsigned kOffLabel = 84U;
constexpr unsigned kValueX = 1061;
constexpr unsigned kToggleLeft = 1014;
constexpr unsigned kToggleRight = 1099;
constexpr unsigned kToggleArrowLeft = 998;
constexpr unsigned kToggleArrowRight = 1110;
constexpr float kMusicFillY = 291.F;
constexpr float kEffectsFillY = 322.F;
constexpr unsigned kVolumeLeft = 964;
constexpr unsigned kVolumeFrameLeft = 960;
constexpr float kFillX = 984.5F;
constexpr unsigned kFillTravel = 62;
constexpr unsigned kVolumeRight = 1118;
constexpr unsigned kVolumeArrowLeft = 945;
constexpr unsigned kVolumeArrowRight = 1132;
constexpr float kIconScale = 1.F / 3, kBracketTint = .7F;
constexpr unsigned kDescriptionInset = 48;
constexpr unsigned kFooterIconX = 32;
constexpr unsigned kFooterIconY = 26;
constexpr float kFooterTextX = 82.8F;
constexpr unsigned kFooterTextY = 39;
constexpr unsigned kFooterBackIconX = 232;
constexpr float kFooterBackTextX = 282.8F;
constexpr float kPromptFontSize = 22;
}  // namespace
struct PauseOptionsSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  resources::ResourceService& resources;
  render::SpriteOutput& output;
  memory::Allocator<std::byte> allocator;
  resources::ArchiveBytes icon_bytes, menu_bytes;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, kImageCount> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<std::array<char, kTextCapacity>, kMenuTextCount> labels{};
  std::array<std::array<char, kTextCapacity>, 4> calibration_text{};
  std::array<std::array<char, kTextCapacity>, kDialogTextCount> dialog_text{};
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool begun{}, prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer,
        resources::ArchiveBytes icons, resources::ArchiveBytes text)
      : resources(service),
        output(renderer),
        allocator(storage_for(service)),
        icon_bytes(std::move(icons)),
        menu_bytes(std::move(text)) {}
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
    resources.release(kTag);
    begun = false;
    prepared = false;
    count = 0;
  }
  void begin() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kTag);
  }
  const Image& image(unsigned id) const {
    for (const auto& entry : std::span(images).first(image_count)) {
      if (entry.id == id) return entry;
    }
    throw std::runtime_error("Native Options image missing");
  }
  void load_labels() {
    const resources::detail::Reader reader{menu_bytes};
    constexpr std::size_t kWord = 4, kLanguages = 6, kEnglish = 1;
    const auto rows = reader.u32(0);
    reader.range(kWord, static_cast<std::uint64_t>(rows) * kLanguages * kWord);
    const auto pool =
        kWord + static_cast<std::size_t>(rows) * kLanguages * kWord;
    for (std::size_t i = 0; i < std::min<std::size_t>(rows, labels.size());
         ++i) {
      constexpr std::size_t kOn = 83, kOff = 84;
      if (std::ranges::find(kLabelRows, i) == kLabelRows.end() &&
          std::ranges::find(kDescriptions, i) == kDescriptions.end() &&
          i != kOn && i != kOff) {
        continue;
      }
      const auto offset =
          reader.u32(kWord + (i * kLanguages + kEnglish) * kWord);
      if (offset == 0) continue;
      if (offset < pool || offset >= menu_bytes.size()) {
        throw std::runtime_error("Options text escapes string pool");
      }
      pause_text::prepare_text(
          reader.text_view({pool, menu_bytes.size() - pool},
                           static_cast<std::uint32_t>(offset - pool)),
          labels.at(i));
    }
    for (const auto row : kLabelRows) {
      if (labels.at(row).front() == 0) {
        throw std::runtime_error("Options label missing");
      }
    }
  }
  void load_icons() {
    const resources::detail::Reader reader{icon_bytes};
    constexpr std::size_t kIconCount = 149, kOffsetTable = 8, kWord = 4;
    if (reader.u32(0) != kIconCount) {
      throw std::runtime_error("Options icon table unsupported");
    }
    reader.range(kOffsetTable, (kIconCount + 1) * kWord);
    for (const auto icon : kIcons) {
      const auto begin = reader.u32(kOffsetTable + icon * kWord),
                 end = reader.u32(kOffsetTable + (icon + 1) * kWord);
      if (begin < kOffsetTable + (kIconCount + 1) * kWord || end < begin) {
        throw std::runtime_error("Options icon range invalid");
      }
      reader.range(begin, end - begin);
      const auto decoded = formats::decode_png(
          std::span(icon_bytes).subspan(begin, end - begin), {}, allocator);
      if (image_count == images.size()) {
        throw std::length_error("Options icon capacity exceeded");
      }
      images.at(image_count++) = {
          kIconBase + icon,
          output.upload_texture(decoded.width, decoded.height, decoded.rgba),
          static_cast<float>(decoded.width),
          static_cast<float>(decoded.height)};
    }
  }
  bool ready() {
    if (prepared) return true;
    if (!begun) throw std::logic_error("Options surface has not begun");
    const auto report = resources.pump(kTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) return false;
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      leases.at(group) = resources.acquire(kGroups.at(group));
      if (!leases.at(group)) {
        throw std::runtime_error("Options resource publication missing");
      }
      const auto& lease = leases.at(group).value();
      constexpr std::size_t kFontGroup = 1, kSystemGroup = 2,
                            kBrightnessGroup = 3, kTextGroup = 4;
      if (group == kTextGroup) {
        const auto table = formats::TextTable::parse(lease.bytes());
        constexpr std::array<std::string_view, 4> kKeys{"0213", "0038", "0027",
                                                        "0028"};
        for (std::size_t i = 0; i < kKeys.size(); ++i) {
          pause_text::prepare_text(pause_text::system_text(table, kKeys.at(i)),
                                   calibration_text.at(i));
        }
        constexpr std::array<std::string_view, 6> kDialogKeys{
            "0001", "0002", "0012", "0013", "0040", "0059"};
        for (std::size_t i = 0; i < kDialogKeys.size(); ++i) {
          pause_text::prepare_text(
              pause_text::localized_text(
                  table,
                  i < 2 ? pause_text::TextNamespace::system
                        : pause_text::TextNamespace::memory,
                  kDialogKeys.at(i)),
              dialog_text.at(i));
        }
        continue;
      }
      const bool is_font = group == kFontGroup;
      if (is_font) font = formats::BitmapFont::parse(lease.bytes(), allocator);
      const auto& archive = lease.archive();
      std::size_t index = 0;
      const resources::detail::Reader reader{lease.bytes()};
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        auto id = is_font ? kFont
                          : static_cast<unsigned>(
                                reader.u16(index * sizeof(std::uint16_t)));
        ++index;
        if (group == kSystemGroup && id != kFooter && id != kDialogEdge &&
            id != kDialogButton) {
          continue;
        }
        if (group == kBrightnessGroup) id = kCalibration;
        if (image_count == images.size()) {
          throw std::length_error("Options texture capacity exceeded");
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        if (is_font &&
            (!font || index != 1 || decoded.width != font->atlas_width() ||
             decoded.height != font->atlas_height())) {
          throw std::runtime_error("Options font atlas mismatch");
        }
        images.at(image_count++) = {id, output.upload_texture(decoded),
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
      }
    }
    load_labels();
    load_icons();
    constexpr std::array<std::uint8_t, 4> kWhitePixel{
        kColorMaximum, kColorMaximum, kColorMaximum, kColorMaximum};
    if (image_count == images.size()) {
      throw std::length_error("Options primitive capacity exceeded");
    }
    images.at(image_count++) = {kWhite,
                                output.upload_texture(1, 1, kWhitePixel), 1, 1};
    for (const auto& track : kTracks) (void)image(track.id);
    constexpr std::array<unsigned, 9> kRequired{
        kFont,       kCalibration, kArrowLeft,
        kArrowRight, kBracketLeft, kBracketRight,
        kSliderFill, kSliderFrame, kSelectionMarker};
    for (const auto id : kRequired) (void)image(id);
    prepared = true;
    return true;
  }
  void sprite(unsigned id, Position position, render::Color color = {},
              float scale_x = 1) {
    const auto& art = image(id);
    const auto width = art.width * scale_x;
    commands.at(count++) = {art.texture,
                            {position.x + (art.width - width) * kHalf,
                             position.y, width, art.height},
                            {0, 0, 1, 1},
                            color};
  }
  void icon(unsigned id, Position position) {
    const auto& art = image(kIconBase + id);
    commands.at(count++) = {art.texture,
                            {position.x, position.y, art.width * kIconScale,
                             art.height * kIconScale},
                            {0, 0, 1, 1},
                            {}};
  }
  void prompt(const MenuPrompt& value, Position position) {
    for (unsigned i = 0; i < value.count; ++i) {
      icon(value.icons.at(i),
           {position.x + static_cast<float>(i) * value.icon_pitch, position.y});
    }
  }
  void text(std::string_view value, Position position,
            render::Color color = kText, bool centered = false,
            float size = kFontSize) {
    if (!font) throw std::logic_error("Options font not prepared");
    std::size_t begin = 0;
    for (;;) {
      const auto end = value.find('\n', begin);
      const auto line = value.substr(
          begin, end == std::string_view::npos ? end : end - begin);
      float width = 0;
      if (centered) {
        for (const auto character : line) {
          const auto glyph = font->glyph(static_cast<unsigned char>(character));
          if (!glyph) throw std::runtime_error("Options text glyph missing");
          width += static_cast<float>(glyph->advance) * size /
                   static_cast<float>(font->pixel_size());
        }
      }
      count +=
          append_sprite_text(*font, image(kFont).texture, line,
                             {position.x - width * kHalf, position.y, size},
                             color, std::span(commands).subspan(count));
      if (end == std::string_view::npos) break;
      begin = end + 1;
      position.y += size;
    }
  }
  void notice(std::string_view message) {
    commands.at(count++) = {image(kWhite).texture,
                            {0, kNoticeTop, kCanvasWidth, kNoticeHeight},
                            {0, 0, 1, 1},
                            {0, 0, 0, kNoticeAlphaByte / kColorMaximum}};
    sprite(kDialogEdge, {0, kNoticeUpperEdge});
    sprite(kDialogEdge, {0, kNoticeLowerEdge});
    text(message, {kCenterX, kNoticeTextY}, kText, true);
  }
  void dialog(const PauseOptionsPresentation& view) {
    constexpr std::size_t kDiscard = 2, kSave = 3, kManualSaved = 4,
                          kAutoSaved = 5;
    const std::string_view message =
        dialog_text
            .at(view.page == PauseOptionsPage::discard ? kDiscard
                : view.page == PauseOptionsPage::save_prompt
                    ? kSave
                    : (view.settings.autosave ? kAutoSaved : kManualSaved))
            .data();
    constexpr float kSize = 48, kLeftX = 448, kRightX = 832;
    const auto lines =
        1 + static_cast<unsigned>(std::ranges::count(message, '\n'));
    const auto geometry = menu_dialog_geometry(
        view.dialog_progress, lines, view.page != PauseOptionsPage::saved);
    const auto top = geometry.top, bottom = geometry.bottom;
    const auto opacity = geometry.opacity, decoration = geometry.decoration;
    // A collapsed authored panel has no coverage and is not a drawable quad.
    if (bottom > top) {
      commands.at(count++) = {
          image(kWhite).texture,
          {0, top, kCanvasWidth, bottom - top},
          {0, 0, 1, 1},
          {0, 0, 0, kNoticeAlphaByte / kColorMaximum * opacity}};
    }
    const auto& edge = image(kDialogEdge);
    sprite(kDialogEdge, {0, top - edge.height * kHalf}, {1, 1, 1, opacity});
    sprite(kDialogEdge, {0, bottom - edge.height * kHalf}, {1, 1, 1, opacity});
    auto color = kText;
    color.alpha = decoration;
    text(message, {kCenterX, geometry.text_y}, color, true, kSize);
    if (view.page == PauseOptionsPage::saved) {
      // The original acknowledgment embeds the ordinary confirm-button prompt
      // at the final panel boundary; it is not the loading spinner variant.
      constexpr float kConfirmX = 1076, kConfirmBottom = 64;
      const auto settled = menu_dialog_geometry(1.F, lines, false);
      prompt(view.prompts.confirm,
             {kConfirmX, settled.bottom - kConfirmBottom});
      return;
    }
    const auto y = geometry.choice_y;
    const auto& button = image(kDialogButton);
    for (unsigned choice = 0; choice < 2; ++choice) {
      const auto x = choice == 0 ? kLeftX : kRightX;
      const auto selected = (choice == 0) == view.dialog_yes;
      auto tint = selected
                      ? kText
                      : render::Color{kText.red * kHalf, kText.green * kHalf,
                                      kText.blue * kHalf, kInactiveAlpha};
      auto button_tint = selected ? render::Color{} : tint;
      button_tint.alpha *= decoration;
      sprite(kDialogButton,
             {x - button.width * kHalf, y - button.height * kHalf},
             button_tint);
      tint.alpha *= decoration;
      text(dialog_text.at(choice).data(), {x, y}, tint, true, kSize);
    }
  }
  void show(const PauseOptionsPresentation& view) {
    if (!valid_menu_prompt(view.prompts.confirm) ||
        !valid_menu_prompt(view.prompts.back)) {
      throw std::invalid_argument("Invalid Options prompt");
    }
    const auto selected = static_cast<std::size_t>(view.selected);
    if (!prepared || !std::isfinite(view.elapsed) || view.elapsed < 0 ||
        !std::isfinite(view.description_alpha) || view.description_alpha < 0 ||
        view.description_alpha > 1 || !std::isfinite(view.arrow_offset) ||
        !std::isfinite(view.marker_brightness) || view.marker_brightness < 0 ||
        selected >= kLabelRows.size() || view.settings.music > kVolumeMaximum ||
        view.settings.effects > kVolumeMaximum ||
        view.page > PauseOptionsPage::complete) {
      throw std::invalid_argument("Invalid native Options presentation");
    }
    count = 0;
    if (view.page == PauseOptionsPage::complete ||
        view.page == PauseOptionsPage::costume ||
        view.page == PauseOptionsPage::controls) {
      output.clear_sprites();
      return;
    }
    if (view.page == PauseOptionsPage::brightness) {
      sprite(kCalibration, {0, 0});
      const std::string_view instruction = calibration_text.front().data();
      const auto lines = 1 + std::ranges::count(instruction, '\n');
      text(instruction,
           {kCenterX,
            kCalibrationTextY - static_cast<float>(lines) * kFontSize * kHalf},
           kText, true);
      prompt(view.prompts.back, {kCalibrationIconX, kCalibrationIconY});
      text(calibration_text.at(1).data(),
           {kCalibrationConfirmX + view.prompts.back.text_advance -
                menu_prompts(true).back.text_advance,
            kCenterX});
      const auto shade =
          view.opening
              ? 1 - std::clamp(view.elapsed / kFadeDuration, 0.F, 1.F)
              : (view.exiting
                     ? std::clamp(view.elapsed / kFadeDuration, 0.F, 1.F)
                     : 0);
      if (shade > 0) {
        commands.at(count++) = {image(kWhite).texture,
                                {0, 0, kCanvasWidth, kCanvasHeight},
                                {0, 0, 1, 1},
                                {0, 0, 0, shade}};
      }
      output.set_sprites(std::span(commands).first(count));
      return;
    }
    const auto time = view.exiting
                          ? std::max(0.F, kTrackDuration - view.elapsed)
                          : (view.opening ? view.elapsed : kTrackDuration);
    for (const auto& track : kTracks) {
      sprite(track.id, track_position(track, time));
    }
    const auto panel = track_position(kTracks.back(), time);
    const auto dx = panel.x - kPanelX, dy = panel.y - kPanelY;
    constexpr float kDim = kUnselectedTintByte / kColorMaximum;
    for (std::size_t row = 0; row < kLabelRows.size(); ++row) {
      const auto color =
          row == selected ? kText
                          : render::Color{kText.red * kDim, kText.green * kDim,
                                          kText.blue * kDim, 1};
      const auto y =
          row == 0 ? kFirstLabelY
                   : kFollowingLabelY + kRowPitch * static_cast<float>(row);
      text(labels.at(kLabelRows.at(row)).data(), {kLabelX + dx, y + dy}, color);
    }
    sprite(kSelectionMarker,
           {kMarkerX + dx,
            kMarkerY + kRowPitch * static_cast<float>(selected) + dy},
           original_sprite_tint(
               {}, {view.marker_brightness - 1, view.marker_brightness - 1,
                    view.marker_brightness - 1, 0}));
    for (unsigned row = 0; row < 2; ++row) {
      const auto y = row == 0 ? kFirstLabelY : kAutosaveY;
      constexpr float kArrowYOffset = 5;
      constexpr std::size_t kAutosaveIndex = 5;
      const auto chosen = selected == (row == 0 ? 0U : kAutosaveIndex);
      const auto enabled =
          row == 0 ? view.settings.subtitles : view.settings.autosave;
      text(labels.at(enabled ? kOnLabel : kOffLabel).data(),
           {kValueX + dx, y + dy},
           chosen ? kText
                  : render::Color{kText.red * kDim, kText.green * kDim,
                                  kText.blue * kDim, 1},
           true);
      sprite(kBracketLeft, {kToggleLeft + dx, y - 1 + dy},
             {kBracketTint, kBracketTint, kBracketTint, 1});
      sprite(kBracketRight, {kToggleRight + dx, y - 1 + dy},
             {kBracketTint, kBracketTint, kBracketTint, 1});
      if (chosen) {
        sprite(kArrowLeft, {kToggleArrowLeft + dx - view.arrow_offset,
                            y + kArrowYOffset + dy});
        sprite(kArrowRight, {kToggleArrowRight + dx + view.arrow_offset,
                             y + kArrowYOffset + dy});
      }
    }
    for (unsigned row = 0; row < 2; ++row) {
      const auto y = row == 0 ? kMusicFillY : kEffectsFillY;
      constexpr float kFrameYOffset = 12, kBracketYOffset = 10,
                      kArrowYOffset = 5;
      sprite(kSliderFill, {kVolumeLeft + dx, y + dy});
      sprite(kSliderFrame, {kVolumeFrameLeft + dx, y - kFrameYOffset + dy});
      const auto value = static_cast<float>(row == 0 ? view.settings.music
                                                     : view.settings.effects) /
                         kVolumeMaximum;
      sprite(kSliderFill, {kFillX - (1 - value) * kFillTravel + dx, y + dy}, {},
             value);
      const auto chosen = selected == row + 1;
      const auto tint =
          chosen ? kText
                 : render::Color{kText.red * kDim, kText.green * kDim,
                                 kText.blue * kDim, 1};
      sprite(kBracketLeft, {kVolumeLeft + dx, y - kBracketYOffset + dy}, tint);
      sprite(kBracketRight, {kVolumeRight + dx, y - kBracketYOffset + dy},
             tint);
      if (chosen) {
        sprite(kArrowLeft, {kVolumeArrowLeft + dx - view.arrow_offset,
                            y - kArrowYOffset + dy});
        sprite(kArrowRight, {kVolumeArrowRight + dx + view.arrow_offset,
                             y - kArrowYOffset + dy});
      }
    }
    constexpr std::size_t kDescriptionTrack = 7, kFooterTrack = 5;
    const auto comment = track_position(kTracks.at(kDescriptionTrack), time);
    auto description_color = kText;
    description_color.alpha = view.description_alpha;
    text(labels.at(kDescriptions.at(selected)).data(),
         {comment.x + kDescriptionInset, comment.y + kDescriptionInset},
         description_color);
    const auto footer = track_position(kTracks.at(kFooterTrack), time);
    prompt(view.prompts.confirm,
           {footer.x + kFooterIconX, footer.y + kFooterIconY});
    constexpr std::size_t kConfirmText = 2, kBackText = 3;
    text(calibration_text.at(kConfirmText).data(),
         {footer.x + kFooterTextX + view.prompts.confirm.text_advance -
              menu_prompts(true).confirm.text_advance,
          footer.y + kFooterTextY},
         kText, false, kPromptFontSize);
    prompt(view.prompts.back,
           {footer.x + kFooterBackIconX, footer.y + kFooterIconY});
    text(calibration_text.at(kBackText).data(),
         {footer.x + kFooterBackTextX + view.prompts.back.text_advance -
              menu_prompts(true).back.text_advance,
          footer.y + kFooterTextY},
         kText, false, kPromptFontSize);
    if (view.page == PauseOptionsPage::notice) notice(view.notice);
    if (view.page == PauseOptionsPage::discard ||
        view.page == PauseOptionsPage::save_prompt ||
        view.page == PauseOptionsPage::saved) {
      dialog(view);
    }
    output.set_sprites(std::span(commands).first(count));
  }
};
PauseOptionsSurface::PauseOptionsSurface(resources::ResourceService& resources,
                                         render::SpriteOutput& output,
                                         resources::ArchiveBytes input_icons,
                                         resources::ArchiveBytes menu_text)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output, std::move(input_icons),
                                         std::move(menu_text))) {}
PauseOptionsSurface::~PauseOptionsSurface() = default;
void PauseOptionsSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool PauseOptionsSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void PauseOptionsSurface::end() noexcept { state_->end(); }
void PauseOptionsSurface::show(const PauseOptionsPresentation& presentation) {
  state_->show(presentation);
}
}  // namespace bw::game
