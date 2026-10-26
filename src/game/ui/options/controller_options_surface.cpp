// Prepare native keyconfig panels and localized text; pack the original input
// icon set into one resident atlas instead of exhausting per-view texture
// slots. This is a storage adaptation only: original pixels, icon indices,
// scales and coordinates remain authored. Ordinary draw performs no decoding or
// uploads. Both-phase Core I.1/E.4 and performance MEM.9 apply as in the public
// contract.
#include "controller_options_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

#include "bw/content/formats/binary_reader.hpp"
#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/image.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "bw/math/scalar.hpp"
#include "game/ui/common/input_icon.hpp"
#include "game/ui/common/localized_text.hpp"
#include "game/ui/common/sprite_color.hpp"
#include "game/ui/common/sprite_text.hpp"
namespace bw::game {
namespace {
constexpr resources::ResourceTag kTag = 24;
constexpr std::array<std::string_view, 5> kGroups{
    "sprite.keyconfig", "sprite.option", "sprite.system", "sprite.font",
    "sprite.text.sys"};
constexpr unsigned kFont = 10000, kWhite = 10001, kIcons = 10002,
                   kIconCount = kInputIconCount, kFooter = 400,
                   kBackground = 209, kKeyTitle = 94, kChoices = 96,
                   kPanel = 79, kMarker = 95, kBracketLeft = 92,
                   kBracketRight = 93, kFill = 222, kFillFrame = 223,
                   kDialogEdge = 326, kDialogButton = 327;
constexpr std::size_t kImageCapacity = 20, kTextCount = 130, kTextBytes = 256,
                      kSystemLabelCount = 6;
constexpr float kFontSize = 32, kRowPitch = 33, kTabPitch = 53, kHalf = .5F,
                kDim = .314F, kInactive = .628F;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
constexpr std::array<unsigned, 5> kTabs{41, 42, 43, 44, 45},
    kMisc{69, 70, 86, 126, 87};
constexpr std::array<unsigned, 17> kKeys{48, 49, 50, 51, 52, 54, 56, 53, 57,
                                         58, 59, 60, 61, 62, 63, 64, 68};
constexpr std::array<unsigned, 11> kPad{52, 53, 57, 58, 54, 55,
                                        56, 65, 66, 67, 68};
constexpr std::array<unsigned, 17> kKeyDescriptions{
    16, 17, 18, 19, 21, 22, 23, 24, 25, 26, 27, 28, 28, 29, 30, 31, 32};
constexpr std::array<unsigned, 11> kPadDescriptions{21, 24, 25, 26, 33, 34,
                                                    35, 36, 20, 20, 32};
constexpr std::array<unsigned, 5> kMiscDescriptions{37, 38, 88, 127, 89};
struct Position {
  float x{}, y{};
};
constexpr Position kCaptureText{420, 320};
constexpr float kCaptureOpacity = .65F;
constexpr unsigned kYesLabel = 4, kNoLabel = 5;
constexpr unsigned kChoicesX = 73, kFooterX = 794, kFooterY = 610,
                   kTabTextX = 145, kTabTextY = 26, kMarkerX = 92,
                   kMarkerY = 22, kTopDescription = 11, kRowTextX = 86,
                   kLeftBracketX = 542, kRightBracketX = 604, kBindingX = 550,
                   kValueX = 434, kSliderFrameOffset = 12, kDescriptionX = 102,
                   kDescriptionOffset = 295, kConfirmX = 826, kPromptY = 636,
                   kPromptIconSize = 42, kConfirmTextX = 877,
                   kPromptTextY = 649, kBackX = 1026, kBackTextX = 1077;

memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
struct Text {
  std::array<char, kTextBytes> bytes{};
  std::string_view view() const { return bytes.data(); }
};
void prepare_label(std::string_view source, Text& result) {
  // Native PC menu strings are UTF-8; remove only source color tags here. Color
  // runs are rendered separately by text() so the actual wording stays intact.
  if (source.size() >= result.bytes.size()) {
    throw std::length_error("Controller label too long");
  }
  std::ranges::copy(source, result.bytes.begin());
  result.bytes.at(source.size()) = 0;
}
}  // namespace
struct ControllerOptionsSurface::State {
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
  std::array<Image, kImageCapacity> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<Text, kTextCount> labels;
  std::array<Text, kSystemLabelCount> system_labels;
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool begun{}, prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer,
        resources::ArchiveBytes icons, resources::ArchiveBytes menu)
      : resources(service),
        output(renderer),
        allocator(storage_for(service)),
        icon_bytes(std::move(icons)),
        menu_bytes(std::move(menu)) {}
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
    begun = prepared = false;
    count = 0;
  }
  void begin() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kTag);
  }
  const Image& image(unsigned id) const {
    for (const auto& item : std::span(images).first(image_count)) {
      if (item.id == id) return item;
    }
    throw std::runtime_error("Controller artwork missing");
  }
  void load_text() {
    const resources::detail::Reader reader{menu_bytes};
    constexpr std::size_t kWord = 4, kLanguages = 6, kEnglish = 1;
    const auto rows = reader.u32(0);
    reader.range(kWord, static_cast<std::uint64_t>(rows) * kLanguages * kWord);
    const auto pool =
        kWord + static_cast<std::size_t>(rows) * kLanguages * kWord;
    for (std::size_t i = 0; i < labels.size(); ++i) {
      if (i >= rows) break;
      const auto offset =
          reader.u32(kWord + (i * kLanguages + kEnglish) * kWord);
      if (offset == 0) continue;
      if (offset < pool || offset >= menu_bytes.size()) {
        throw std::runtime_error("Controller text escapes pool");
      }
      prepare_label(reader.text_view({pool, menu_bytes.size() - pool},
                                     static_cast<std::uint32_t>(offset - pool)),
                    labels.at(i));
    }
  }
  void load_icons() {
    const auto atlas = decode_input_icon_atlas(
        icon_bytes, memory::Allocator<std::uint8_t>(allocator));
    images.at(image_count++) = {
        kIcons, output.upload_texture(atlas.width, atlas.height, atlas.rgba),
        static_cast<float>(atlas.width), static_cast<float>(atlas.height)};
  }
  bool ready() {
    if (prepared) return true;
    if (!begun) throw std::logic_error("Controller surface not begun");
    const auto report = resources.pump(kTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) return false;
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      leases.at(group) = resources.acquire(kGroups.at(group));
      if (!leases.at(group)) {
        throw std::runtime_error("Controller resources missing");
      }
      const auto& lease = leases.at(group).value();
      constexpr unsigned kOptionsGroup = 1, kSystemGroup = 2, kFontGroup = 3,
                         kTextGroup = 4;
      if (group == kTextGroup) {
        const auto table = formats::TextTable::parse(lease.bytes());
        constexpr std::array<std::string_view, kSystemLabelCount> keys{
            "0164", "0165", "0027", "0028", "0001", "0002"};
        for (std::size_t i = 0; i < keys.size(); ++i) {
          localized_text::prepare_text(
              localized_text::system_text(table, keys.at(i)),
              system_labels.at(i).bytes);
        }
        continue;
      }
      if (group == kFontGroup) {
        font = formats::BitmapFont::parse(lease.bytes(), allocator);
      }
      const auto& archive = lease.archive();
      const resources::detail::Reader reader{lease.bytes()};
      std::size_t index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        const auto id = group == kFontGroup
                            ? kFont
                            : static_cast<unsigned>(
                                  reader.u16(index * sizeof(std::uint16_t)));
        ++index;
        if (group == kOptionsGroup && id != kBackground && id != kFill &&
            id != kFillFrame) {
          continue;
        }
        if (group == kSystemGroup && id != kFooter && id != kDialogEdge &&
            id != kDialogButton) {
          continue;
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        images.at(image_count++) = {id, output.upload_texture(decoded),
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
      }
    }
    load_text();
    load_icons();
    constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
    images.at(image_count++) = {kWhite, output.upload_texture(1, 1, white), 1,
                                1};
    for (const auto id :
         {kFont, kIcons, kKeyTitle, kChoices, kPanel, kMarker, kBracketLeft,
          kBracketRight, kFill, kFillFrame, kFooter, kBackground}) {
      (void)image(id);
    }
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
  void icon(unsigned index, Position position, float size = kFontSize) {
    if (index >= kIconCount) {
      throw std::invalid_argument("Controller icon index invalid");
    }
    commands.at(count++) = {image(kIcons).texture,
                            {position.x, position.y, size, size},
                            input_icon_uv(index),
                            {}};
  }
  void text(std::string_view value, Position position,
            render::Color color = kText, float size = kFontSize) {
    if (!font) throw std::logic_error("Controller font missing");
    auto cursor = position;
    const auto original = color;
    for (std::size_t i = 0; i < value.size();) {
      if (value[i] == '<') {
        const auto end = value.find('>', i);
        if (end == std::string_view::npos) {
          throw std::runtime_error("Controller text markup incomplete");
        }
        const auto tag = value.substr(i, end - i + 1);
        if (tag == "<col yellow>") {
          color = {1, 1, 0, original.alpha};
        } else if (tag == "<col end>") {
          color = original;
        } else {
          throw std::runtime_error("Unknown controller text markup");
        }
        i = end + 1;
        continue;
      }
      const auto begin = i;
      const auto code = next_sprite_character(value, i);
      if (code == '\n') {
        cursor.x = position.x;
        cursor.y += size;
        continue;
      }
      const auto glyph = font->glyph(code);
      if (!glyph) throw std::runtime_error("Controller glyph missing");
      count += append_sprite_text(*font, image(kFont).texture,
                                  value.substr(begin, i - begin),
                                  {cursor.x, cursor.y, size}, color,
                                  std::span(commands).subspan(count));
      cursor.x += static_cast<float>(glyph->advance) * size /
                  static_cast<float>(font->pixel_size());
    }
  }
  void centered_text(std::string_view value, Position center,
                     render::Color color, float size) {
    if (!font) throw std::logic_error("Controller font missing");
    const auto& active_font = *font;
    std::size_t begin = 0;
    while (begin <= value.size()) {
      const auto end = value.find('\n', begin);
      const auto line = value.substr(begin, end == std::string_view::npos
                                                ? value.size() - begin
                                                : end - begin);
      float width = 0;
      for (std::size_t index = 0; index < line.size();) {
        const auto glyph =
            active_font.glyph(next_sprite_character(line, index));
        if (!glyph) throw std::runtime_error("Controller dialog glyph missing");
        width += static_cast<float>(glyph->advance) * size /
                 static_cast<float>(active_font.pixel_size());
      }
      text(line, {center.x - width * kHalf, center.y - size * kHalf}, color,
           size);
      if (end == std::string_view::npos) break;
      begin = end + 1;
      center.y += size;
    }
  }
  void shade(float alpha) {
    constexpr float kWidth = 1280, kHeight = 720;
    commands.at(count++) = {image(kWhite).texture,
                            {0, 0, kWidth, kHeight},
                            {0, 0, 1, 1},
                            {0, 0, 0, alpha}};
  }
  void show(const ControllerOptionsPresentation& view) {
    if (!prepared || view.selected > ControllerSection::exit ||
        view.displayed > ControllerSection::miscellaneous ||
        !std::isfinite(view.elapsed) || view.elapsed < 0 ||
        !valid_menu_prompt(view.prompts.confirm) ||
        !valid_menu_prompt(view.prompts.back)) {
      throw std::invalid_argument("Invalid controller surface snapshot");
    }
    count = 0;
    if (!view.active) {
      output.clear_sprites();
      return;
    }
    constexpr float kPanelDuration = 1.6F, kLeftDuration = 1.F,
                    kDescriptionDuration = 1.4F, kMainExitStart = .2F;
    const auto time = [&](float duration) {
      return view.opening
                 ? view.elapsed
                 : (view.exiting ? std::max(0.F, duration - view.elapsed)
                                 : duration);
    };
    const auto fraction = [](float elapsed, float begin, float end) {
      return std::clamp((elapsed - begin) / (end - begin), 0.F, 1.F);
    };
    const auto response = [&](float factor) {
      return view.exiting
                 ? math::quintic_ease(factor)
                 : math::eased_tangent_interpolate(
                       math::EasedTangentEndpoints<float>{0, 1, 1, 1}, factor);
    };
    const auto outer_time = time(kDescriptionDuration);
    const auto left = -730.F * (1 - fraction(outer_time, 0, kLeftDuration));
    const auto description_y =
        258.F +
        720.F * (1 - response(fraction(outer_time, 0, kDescriptionDuration)));
    const auto panel_x =
        514.F +
        1024.F * (1 - response(fraction(time(kPanelDuration), kMainExitStart,
                                        kPanelDuration)));
    constexpr float kFooterBegin = .2F, kFooterEnd = 1.2F;
    const auto footer_fraction = fraction(outer_time, kFooterBegin, kFooterEnd);
    constexpr Position kFooterStart{1296, 736};
    const Position footer{
        std::lerp(kFooterStart.x, static_cast<float>(kFooterX),
                  footer_fraction),
        std::lerp(kFooterStart.y, static_cast<float>(kFooterY),
                  footer_fraction)};
    sprite(kBackground, {0, 0});
    sprite(kKeyTitle, {left, 0});
    sprite(kChoices, {kChoicesX, description_y});
    sprite(kPanel, {panel_x, 4});
    sprite(kFooter, footer);
    for (unsigned row = 0; row < kTabs.size(); ++row) {
      auto color = kText;
      color.alpha = (row == static_cast<unsigned>(view.selected)
                         ? (view.editing ? kInactive : 1)
                         : kDim);
      text(labels.at(kTabs.at(row)).view(),
           {kTabTextX,
            description_y + kTabTextY + kTabPitch * static_cast<float>(row)},
           color);
    }
    sprite(kMarker,
           {kMarkerX, description_y + kMarkerY +
                          kTabPitch * static_cast<float>(view.selected)},
           original_sprite_tint(
               {}, {view.marker_brightness - 1, view.marker_brightness - 1,
                    view.marker_brightness - 1, 0}));
    unsigned description =
        kTopDescription + static_cast<unsigned>(view.selected);
    const auto section = static_cast<unsigned>(view.displayed);
    const unsigned rows = section == 0
                              ? kKeys.size()
                              : (section == 1 ? kPad.size() : kMisc.size());
    if (view.row >= rows) throw std::invalid_argument("Controller row invalid");
    for (unsigned row = 0; row < rows; ++row) {
      const auto y = 4 + kRowPitch * (static_cast<float>(row) + 1);
      auto color = kText;
      color.alpha = view.editing ? (row == view.row ? 1 : kDim) : kInactive;
      const auto label = section == 0
                             ? kKeys.at(row)
                             : (section == 1 ? kPad.at(row) : kMisc.at(row));
      text(labels.at(label).view(), {panel_x + kRowTextX, y}, color);
      sprite(kBracketLeft, {panel_x + kLeftBracketX, y}, color);
      sprite(kBracketRight, {panel_x + kRightBracketX, y}, color);
      if (section < 2) {
        unsigned icon_index = 0;
        if (section == 0) {
          const auto code = view.settings.keys.at(row);
          icon_index = input_key_icon(code);
        } else {
          const auto& buttons =
              view.settings.controller_identity == kSourceXinputIdentity
                  ? view.settings.buttons
                  : kSourceXinputButtons;
          const auto code = buttons.at(kControllerEditSlots.at(row));
          if (code < kControllerButtonCount) icon_index = input_pad_icon(code);
        }
        icon(icon_index, {panel_x + kBindingX, y - 2});
      } else if (row < 3) {
        const bool enabled = row == 0
                                 ? view.settings.invert_vertical
                                 : (row == 1 ? view.settings.invert_horizontal
                                             : view.device.vibration);
        const unsigned value =
            (row == 2 ? 84U : 71U) + static_cast<unsigned>(enabled);
        text(labels.at(value).view(), {panel_x + kValueX, y}, color);
      } else {
        const auto value =
            row == 3 ? view.device.mouse_bias : view.device.turn_speed;
        constexpr float kMaximum = 100, kSliderX = 434, kFillX = 458.5F,
                        kFillTravel = 62;
        sprite(kFillFrame, {panel_x + kSliderX, y - kSliderFrameOffset});
        sprite(kFill,
               {panel_x + kFillX -
                    (1 - static_cast<float>(value) / kMaximum) * kFillTravel,
                y},
               color, static_cast<float>(value) / kMaximum);
      }
    }
    if (view.editing) {
      description = section == 0
                        ? kKeyDescriptions.at(view.row)
                        : (section == 1 ? kPadDescriptions.at(view.row)
                                        : kMiscDescriptions.at(view.row));
    }
    auto description_color = kText;
    description_color.alpha = view.description_alpha;
    if (description < labels.size()) {
      text(labels.at(description).view(),
           {kDescriptionX, description_y + kDescriptionOffset},
           description_color);
    }
    constexpr float kPromptSize = 22;
    struct PromptOffsets {
      float icon, text;
    };
    const auto prompt = [&](const MenuPrompt& value, PromptOffsets origin,
                            unsigned label) {
      for (unsigned index = 0; index < value.count; ++index) {
        icon(value.icons.at(index),
             {origin.icon + static_cast<float>(index) * value.icon_pitch,
              kPromptY},
             kPromptIconSize);
      }
      constexpr float kOriginalPadAdvance = 44.8F;
      text(system_labels.at(label).view(),
           {origin.text + value.text_advance - kOriginalPadAdvance,
            kPromptTextY},
           kText, kPromptSize);
    };
    prompt(view.prompts.confirm, {kConfirmX, kConfirmTextX}, 2);
    prompt(view.prompts.back, {kBackX, kBackTextX}, 3);
    if (view.capturing) {
      shade(kCaptureOpacity);
      text("Press a key or button", kCaptureText);
    }
    if (view.dialog != ControllerDialog::none) {
      const auto message =
          system_labels.at(view.dialog == ControllerDialog::defaults ? 0 : 1)
              .view();
      const auto lines =
          static_cast<unsigned>(std::ranges::count(message, '\n')) + 1;
      const auto layout =
          menu_dialog_geometry(view.dialog_view.progress, lines);
      constexpr float kPanelAlpha = 178.F / 255, kCanvasWidth = 1280,
                      kCenterX = 640, kChoiceLeft = 448, kChoiceRight = 832,
                      kDialogSize = menu_dialog_font_size, kInactiveAlpha = .8F;
      if (layout.bottom > layout.top) {
        commands.at(count++) = {
            image(kWhite).texture,
            {0, layout.top, kCanvasWidth, layout.bottom - layout.top},
            {0, 0, 1, 1},
            {0, 0, 0, kPanelAlpha * layout.opacity}};
      }
      const auto& edge = image(kDialogEdge);
      sprite(kDialogEdge, {0, layout.top - edge.height * kHalf},
             {1, 1, 1, layout.opacity});
      sprite(kDialogEdge, {0, layout.bottom - edge.height * kHalf},
             {1, 1, 1, layout.opacity});
      auto color = kText;
      color.alpha = layout.decoration;
      centered_text(message, {kCenterX, layout.text_y}, color, kDialogSize);
      const auto& button = image(kDialogButton);
      for (unsigned choice = 0; choice < 2; ++choice) {
        const auto x = choice == 0 ? kChoiceLeft : kChoiceRight;
        const bool selected = (choice == 0) == view.dialog_yes;
        auto tint = selected
                        ? kText
                        : render::Color{kText.red * kHalf, kText.green * kHalf,
                                        kText.blue * kHalf, kInactiveAlpha};
        auto background = selected ? render::Color{} : tint;
        background.alpha *= layout.decoration;
        sprite(
            kDialogButton,
            {x - button.width * kHalf, layout.choice_y - button.height * kHalf},
            background);
        tint.alpha *= layout.decoration;
        centered_text(
            system_labels.at(choice == 0 ? kYesLabel : kNoLabel).view(),
            {x, layout.choice_y}, tint, kDialogSize);
      }
    }
    output.set_sprites(std::span(commands).first(count));
  }
};
ControllerOptionsSurface::ControllerOptionsSurface(
    resources::ResourceService& resources, render::SpriteOutput& output,
    resources::ArchiveBytes icons, resources::ArchiveBytes menu)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output, std::move(icons),
                                         std::move(menu))) {}
ControllerOptionsSurface::~ControllerOptionsSurface() = default;
void ControllerOptionsSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool ControllerOptionsSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void ControllerOptionsSurface::end() noexcept { state_->end(); }
void ControllerOptionsSurface::show(const ControllerOptionsPresentation& view) {
  state_->show(view);
}
}  // namespace bw::game
