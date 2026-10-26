// Original Help sprites and localized rich text. Unicode decoding, markup,
// font lookup and semantic markers prepare once. Frames resolve copied icon
// runs, adjust subsequent glyph positions per line, and translate immutable
// prepared glyphs, cull source lines and emit original atlas quads. Narrow
// output/resource borrows do not expose Profile or time mutation. Core I.1/C.9
// and performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md),
// both phases consulted; resources use the service domain and fixed-capacity
// presentation storage.
#include "pause_help_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/image.hpp"
#include "bw/content/formats/text_table.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "bw/math/scalar.hpp"
#include "game/ui/common/input_icon.hpp"
#include "game/ui/common/sprite_text.hpp"
namespace bw::game {
namespace {
constexpr resources::ResourceTag kHelpTag = 21;
constexpr std::array<std::string_view, 5> kGroups{
    "sprite.help", "sprite.system", "sprite.font", "sprite.text.hlp2",
    "sprite.text.sys"};
constexpr unsigned kIconBase = 20000, kSolid = 10000;
constexpr float kTextSize = 32, kLineHeight = 40, kDuration = 1.15F;
constexpr render::Color kText{1, 1, 1, 1}, kYellow{1, 1, 0, 1};
struct Point {
  float x{}, y{};
};
struct Track {
  unsigned image{};
  Point start, finish;
  float begin{}, end{}, shape{};
  bool tangent{true};
};
constexpr std::array<Track, 6> kOuter{
    {{66, {1520, 0}, {746, 0}, 0, .75F, .5F},
     {67, {1109, -483}, {1109, 231}, .35F, .95F, .5F},
     {73, {183, -400}, {183, 0}, .25F, .65F, .5F},
     {72, {183, 926}, {183, 526}, .25F, .65F, .5F},
     {400, {1094, 910}, {794, 610}, 0, .9F, 0, false},
     {71, {183, 720}, {183, 0}, .15F, .95F, .4F}}};
constexpr Track kComment{65, {574, -720}, {574, -6}, .2F, .85F, .5F};
constexpr Track kRow{74, {212, 820}, {212, -80}, .25F, 1.15F, .7F};
constexpr std::array<render::Color, 3> kCategories{
    {{41.F / 255, 51.F / 255, 44.F / 255, 1},
     {32.F / 255, 30.F / 255, 53.F / 255, 1},
     {39.F / 255, 24.F / 255, 19.F / 255, 1}}};

constexpr unsigned kByteBits = 8, kRadix = 10;
constexpr std::size_t kImageCapacity = 20;
constexpr unsigned kMoveIcon = 100, kCameraIcon = 101, kConfirmIcon = 120,
                   kBackIcon = 121, kReloadIcon = 122, kWeaponIcon = 123,
                   kLeftBumperIcon = 124, kRightBumperIcon = 125,
                   kDodgeIcon = 130, kFireIcon = 131, kPlusIcon = 148;
constexpr unsigned kFooterImage = 400, kCommentImage = 65, kRowImage = 74,
                   kFocusImage = 75, kReadingImage = 76, kCategoryImage = 449,
                   kUnreadImage = 450, kSliderUp = 70, kSliderDown = 68,
                   kSliderMiddle = 69, kConfirmLabel = 27, kBackLabel = 28;
constexpr float kInlineIconOffset = 7, kMagicIconAdvance = 39.822224F,
                kIconAdvance = 44.8F, kFadeHeight = 20, kVisibleBottom = 460,
                kSliderCap = 40, kSliderFixedHeight = 158,
                kBlackDuration = .45F, kBlackOpacity = .5F, kReadOffset = 7,
                kDescriptionX = 42, kDescriptionY = 94, kRowStep = 100,
                kRowClip = 410, kLabelCenterX = 175, kLabelCenterY = 44,
                kShadowOffset = 3, kSliderEntry = .95F, kListSliderX = 500,
                kListSliderY = 54, kListSliderHeight = 409, kBodySliderX = 581,
                kBodySliderY = 60, kBodySliderHeight = 528, kFooterEntry = .9F,
                kPromptX = 826, kPromptY = 636, kPromptTextX = 877,
                kPromptTextY = 649, kPromptSecondX = 1026,
                kPromptSecondTextX = 1077, kPromptSize = 22;
constexpr unsigned kSpellCategoryFirst = 9, kWorldCategoryFirst = 18;
enum class TextGroup : std::uint8_t { title, description };
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
// The original text tree stores topic IDs, two groups (title/body), then
// language children. Views remain within the retained resource lease.
std::string_view english(const formats::TextTable& table, unsigned id,
                         TextGroup selected_group) {
  const auto group = static_cast<unsigned>(selected_group);
  constexpr unsigned kEnglish = 200;
  for (std::size_t row = 0; row < table.records(1); ++row) {
    const auto item = table.node(1, row);
    unsigned key = 0;
    bool numeric = !item.text.empty();
    for (const char digit : item.text) {
      if (digit < '0' || digit > '9') {
        numeric = false;
        break;
      }
      key = key * kRadix + static_cast<unsigned>(digit - '0');
    }
    if (!numeric || key != id) continue;
    if (group >= item.children.count) {
      throw std::runtime_error("Help text group missing");
    }
    const auto variants = table.node(2, item.children.first + group).children;
    for (std::size_t language = 0; language < variants.count; ++language) {
      const auto entry = table.node(3, variants.first + language);
      if (entry.identifier == kEnglish) return entry.text;
    }
  }
  throw std::runtime_error("Original Help English text missing");
}
render::TextureChannel channel(formats::FontChannel value) {
  switch (value) {
    case formats::FontChannel::red:
      return render::TextureChannel::red;
    case formats::FontChannel::green:
      return render::TextureChannel::green;
    case formats::FontChannel::blue:
      return render::TextureChannel::blue;
    case formats::FontChannel::alpha:
      return render::TextureChannel::alpha;
  }
  throw std::runtime_error("Invalid Help font channel");
}
Point sample(const Track& track, float seconds) {
  const auto fraction =
      std::clamp((seconds - track.begin) / (track.end - track.begin), 0.F, 1.F);
  const auto response = track.tangent ? math::eased_tangent_interpolate(
                                            math::EasedTangentEndpoints<float>{
                                                0, 1, track.shape, track.shape},
                                            fraction)
                                      : math::quintic_ease(fraction);
  return {std::lerp(track.start.x, track.finish.x, response),
          std::lerp(track.start.y, track.finish.y, response)};
}
}  // namespace
struct PauseHelpSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  struct Glyph {
    render::Sprite sprite;
    unsigned line{};
    HelpInputAction action{HelpInputAction::count};
  };
  struct Text {
    memory::Vector<Glyph> glyphs;
    float width{};
    unsigned lines{1};
    explicit Text(const memory::Allocator<std::byte>& allocator)
        : glyphs(allocator) {}
  };
  resources::ResourceService& resources;
  render::SpriteOutput& output;
  memory::Allocator<std::byte> allocator;
  resources::ArchiveBytes input_icons;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, kImageCapacity> images{};
  std::size_t image_count{}, count{};
  std::optional<formats::BitmapFont> font;
  memory::Vector<Text> titles, bodies, prompts;
  std::array<unsigned, help_topic_count> lines{};
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  bool begun{}, prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer,
        resources::ArchiveBytes icons)
      : resources(service),
        output(renderer),
        allocator(storage_for(service)),
        input_icons(std::move(icons)),
        titles(allocator),
        bodies(allocator),
        prompts(allocator) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    if (begun) output.clear_sprites();
    titles.clear();
    bodies.clear();
    prompts.clear();
    font.reset();
    while (image_count != 0) {
      output.release_texture(images.at(--image_count).texture);
    }
    for (auto& lease : leases) lease.reset();
    resources.release(kHelpTag);
    begun = prepared = false;
    count = 0;
  }
  const Image& image(unsigned id) const {
    for (const auto& art : std::span(images).first(image_count)) {
      if (art.id == id) return art;
    }
    throw std::runtime_error("Help image missing");
  }
  void prepare() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kHelpTag);
  }
  void add_image(unsigned id, const formats::WorldTextureImage& decoded) {
    if (image_count == images.size()) {
      throw std::length_error("Help image capacity exceeded");
    }
    images.at(image_count++) = {id, output.upload_texture(decoded),
                                static_cast<float>(decoded.width),
                                static_cast<float>(decoded.height)};
  }
  render::Sprite icon_sprite(unsigned id, float x, float y, float size,
                             render::Color color = {}) const {
    return {
        image(kIconBase).texture, {x, y, size, size}, input_icon_uv(id), color};
  }
  // Shape original strings once. Markup selects color or controller artwork;
  // unsupported syntax fails preparation instead of displaying raw tags.
  Text shape(std::string_view text) const {
    if (!font) throw std::logic_error("Help font is not ready");
    Text result(allocator);
    result.glyphs.reserve(text.size());
    const auto scale = kTextSize / static_cast<float>(font->pixel_size());
    const auto baseline = static_cast<float>(font->baseline()) * scale;
    float x = 0, y = 0;
    render::Color color = kText;
    const auto icon = [&](unsigned id, float advance) {
      constexpr float kScale = .933333993F / 3.F;
      result.glyphs.push_back({icon_sprite(id, x, y - kInlineIconOffset,
                                           kInputIconPixels * kScale, kText),
                               result.lines - 1});
      x += advance;
    };
    for (std::size_t cursor = 0; cursor < text.size();) {
      if (text[cursor] == '<') {
        const auto end = text.find('>', cursor);
        if (end == std::string_view::npos) {
          throw std::runtime_error("Unterminated Help markup");
        }
        const auto tag = text.substr(cursor + 1, end - cursor - 1);
        cursor = end + 1;
        if (tag == "col yellow") {
          color = kYellow;
        } else if (tag == "col end") {
          color = kText;
        } else if (tag.starts_with("kico ")) {
          constexpr std::array<std::string_view, 6> kActions{
              "kico mcha", "kico mcam", "kico sht",
              "kico dod",  "kico atk",  "kico rel"};
          const auto found = std::ranges::find(kActions, tag);
          if (found == kActions.end()) {
            throw std::runtime_error("Unsupported logical Help icon");
          }
          icon(kMoveIcon, kIconAdvance);
          result.glyphs.back().action =
              static_cast<HelpInputAction>(found - kActions.begin());
        } else if (tag == "ico key_lrb") {
          icon(kLeftBumperIcon, kMagicIconAdvance);
          icon(kPlusIcon, kMagicIconAdvance);
          icon(kRightBumperIcon, kMagicIconAdvance);
        } else {
          unsigned id = 0;
          if (tag == "ico key_ls") {
            id = kMoveIcon;
          } else if (tag == "ico key_rs") {
            id = kCameraIcon;
          } else if (tag == "ico key_rt") {
            id = kFireIcon;
          } else if (tag == "ico key_lt") {
            id = kDodgeIcon;
          } else if (tag == "ico key_x") {
            id = kReloadIcon;
          } else if (tag == "ico key_a") {
            id = kConfirmIcon;
          } else if (tag == "ico key_b") {
            id = kBackIcon;
          } else if (tag == "ico key_y") {
            id = kWeaponIcon;
          } else if (tag == "ico key_lb") {
            id = kLeftBumperIcon;
          } else if (tag == "ico key_rb") {
            id = kRightBumperIcon;
          } else {
            throw std::runtime_error("Unsupported original Help markup");
          }
          icon(id, kIconAdvance);
        }
        continue;
      }
      const auto cp = next_sprite_character(text, cursor);
      if (cp == '\r') continue;
      if (cp == '\n') {
        result.width = std::max(result.width, x);
        x = 0;
        y += kLineHeight;
        ++result.lines;
        continue;
      }
      const auto found = font->glyph(cp);
      if (!found) throw std::runtime_error("Original Help font glyph missing");
      const auto& glyph = *found;
      if (!glyph.blank) {
        const auto width = static_cast<float>(glyph.right - glyph.left),
                   height = static_cast<float>(glyph.bottom - glyph.top);
        const auto aw = static_cast<float>(font->atlas_width()),
                   ah = static_cast<float>(font->atlas_height());
        result.glyphs.push_back(
            {{image(0).texture,
              {x + static_cast<float>(glyph.bearing_left) * scale,
               y + baseline + static_cast<float>(glyph.bearing_top) * scale,
               width * scale, height * scale},
              {static_cast<float>(glyph.left) / aw,
               static_cast<float>(glyph.top) / ah, width / aw, height / ah},
              color,
              channel(glyph.channel)},
             result.lines - 1});
      }
      x += static_cast<float>(glyph.advance) * scale;
    }
    result.width = std::max(result.width, x);
    return result;
  }
  bool ready() {
    if (prepared) return true;
    if (!begun) throw std::logic_error("Help surface has not prepared");
    const auto report = resources.pump(kHelpTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) return false;
    try {
      for (std::size_t group = 0; group < kGroups.size(); ++group) {
        leases.at(group) = resources.acquire(kGroups.at(group));
        if (!leases.at(group)) {
          throw std::runtime_error("Help resource publication missing");
        }
        if (group >= 3) continue;
        const auto& lease = *leases.at(group);
        const bool is_font = group == 2;
        if (is_font) {
          font = formats::BitmapFont::parse(lease.bytes(), allocator);
        }
        const auto& archive = lease.archive();
        std::size_t index = 0;
        for (std::size_t record = 0; record < archive.records().size();
             ++record) {
          if (archive.records()[record].type != "DDS ") continue;
          if (!is_font && index >= lease.bytes().size() / 2) {
            throw std::runtime_error("Help sprite identity missing");
          }
          const auto id =
              is_font ? 0U
                      : static_cast<unsigned>(lease.bytes()[index * 2]) |
                            (static_cast<unsigned>(lease.bytes()[index * 2 + 1])
                             << kByteBits);
          ++index;
          if (group == 1 && id != kFooterImage) continue;
          const auto decoded = formats::decode_world_texture(
              {archive.payload(record), archive.metadata(record)}, allocator);
          if (is_font &&
              (!font || index != 1 || decoded.width != font->atlas_width() ||
               decoded.height != font->atlas_height())) {
            throw std::runtime_error("Help font atlas disagrees with metrics");
          }
          add_image(id, decoded);
        }
      }
      const auto atlas = decode_input_icon_atlas(
          input_icons, memory::Allocator<std::uint8_t>(allocator));
      images.at(image_count++) = {
          kIconBase,
          output.upload_texture(atlas.width, atlas.height, atlas.rgba),
          static_cast<float>(atlas.width), static_cast<float>(atlas.height)};
      constexpr std::array<std::uint8_t, 4> kPixel{255, 255, 255, 255};
      images.at(image_count++) = {kSolid, output.upload_texture(1, 1, kPixel),
                                  1, 1};
      const auto content = formats::TextTable::parse(leases.at(3)->bytes());
      titles.reserve(help_topic_count);
      bodies.reserve(help_topic_count);
      for (unsigned topic = 0; topic < help_topic_count; ++topic) {
        titles.push_back(shape(english(content, topic, TextGroup::title)));
        bodies.push_back(
            shape(english(content, topic, TextGroup::description)));
        lines.at(topic) = bodies.back().lines;
      }
      const auto system = formats::TextTable::parse(leases.at(4)->bytes());
      prompts.reserve(2);
      prompts.push_back(
          shape(english(system, kConfirmLabel, TextGroup::title)));
      prompts.push_back(shape(english(system, kBackLabel, TextGroup::title)));
      prepared = true;
      return true;
    } catch (...) {
      end();
      throw;
    }
  }
  void emit(render::Sprite sprite) {
    if (count == commands.size()) {
      throw std::length_error("Help presentation exceeds sprite capacity");
    }
    commands.at(count++) = sprite;
  }
  void sprite(unsigned id, Point position, render::Color color = {}) {
    const auto& art = image(id);
    emit({art.texture,
          {position.x, position.y, art.width, art.height},
          {0, 0, 1, 1},
          color});
  }
  void text(const Text& value, const HelpInputIcons& bindings, Point position,
            float alpha = 1, bool shadow = false, bool clipped = false,
            float scroll = 0, float scale = 1) {
    unsigned previous_line = 0;
    float inline_offset = 0;
    for (const auto& glyph : value.glyphs) {
      if (glyph.line != previous_line) {
        inline_offset = 0;
        previous_line = glyph.line;
      }
      const float glyph_offset = inline_offset;
      if (glyph.action != HelpInputAction::count) {
        inline_offset +=
            bindings.actions.at(static_cast<unsigned>(glyph.action)).advance -
            kIconAdvance;
      }
      float opacity = alpha;
      if (clipped) {
        const auto row = static_cast<float>(glyph.line) * kLineHeight - scroll;
        opacity *= std::clamp((row + kFadeHeight) / kFadeHeight, 0.F, 1.F) *
                   std::clamp((kVisibleBottom - row) / kFadeHeight, 0.F, 1.F);
      }
      if (opacity <= 0) continue;
      auto command = glyph.sprite;
      command.destination.x =
          position.x + (command.destination.x + glyph_offset) * scale;
      command.destination.y =
          position.y + (command.destination.y - scroll) * scale;
      command.destination.width *= scale;
      command.destination.height *= scale;
      if (shadow) {
        command.color = {0, 0, 0, opacity};
      } else {
        command.color.alpha *= opacity;
      }
      if (glyph.action == HelpInputAction::count) {
        emit(command);
      } else {
        const auto& run =
            bindings.actions.at(static_cast<unsigned>(glyph.action));
        for (unsigned i = 0; i < run.count; ++i) {
          auto icon_command = command;
          icon_command.uv = input_icon_uv(run.icons.at(i));
          icon_command.destination.x +=
              static_cast<float>(i) * run.pitch * scale;
          emit(icon_command);
        }
      }
    }
  }
  void scrollbar(Point position, float height, float fraction, bool active) {
    if (!active) return;
    sprite(kSliderUp, position);
    sprite(kSliderDown, {position.x, position.y + height - kSliderCap});
    sprite(kSliderMiddle, {position.x, position.y + kSliderCap +
                                           (height - kSliderFixedHeight) *
                                               std::clamp(fraction, 0.F, 1.F)});
  }
  void show(const PauseHelpPresentation& view) {
    if (!valid_help_input_icons(view.input_icons) ||
        !valid_menu_prompt(view.prompts.confirm) ||
        !valid_menu_prompt(view.prompts.back)) {
      throw std::invalid_argument("Invalid Help prompt");
    }
    if (!prepared) throw std::logic_error("Help surface is not ready");
    if (view.phase == PauseHelpPhase::complete) {
      output.clear_sprites();
      return;
    }
    if (view.count == 0 || view.count > help_topic_count ||
        view.selected >= view.count || !std::isfinite(view.elapsed) ||
        view.elapsed < 0 || view.elapsed > kDuration ||
        !std::isfinite(view.scroll_pixels) || view.scroll_pixels < 0 ||
        !std::isfinite(view.maximum_scroll) || view.maximum_scroll < 0 ||
        view.scroll_pixels > view.maximum_scroll ||
        !std::isfinite(view.list_offset) || !std::isfinite(view.panel_focus) ||
        !std::isfinite(view.reading_strength) ||
        !std::isfinite(view.highlight)) {
      throw std::invalid_argument("Invalid Help presentation");
    }
    for (std::size_t i = 0; i < view.count; ++i) {
      if (view.topics.at(i) >= help_topic_count ||
          !std::isfinite(view.topic_strength.at(i))) {
        throw std::invalid_argument("Invalid Help topic");
      }
    }
    count = 0;
    const float seconds = view.phase == PauseHelpPhase::closing
                              ? kDuration - view.elapsed
                              : view.elapsed;
    const float fade = std::clamp(seconds / kBlackDuration, 0.F, 1.F);
    emit({image(kSolid).texture,
          {0, 0, render::kCanvasWidth, render::kCanvasHeight},
          {0, 0, 1, 1},
          {0, 0, 0, kBlackOpacity * fade}});
    for (const auto& motion : kOuter) {
      sprite(motion.image, sample(motion, seconds));
    }
    auto detail = sample(kComment, seconds);
    detail.x -= kReadOffset * view.panel_focus;
    detail.y -= kReadOffset * view.panel_focus;
    sprite(kCommentImage, detail);
    const auto topic = view.topics.at(view.selected);
    text(bodies.at(topic), view.input_icons,
         {detail.x + kDescriptionX, detail.y + kDescriptionY}, 1, false, true,
         view.scroll_pixels);
    const auto base = sample(kRow, seconds);
    for (int row = -4; row < static_cast<int>(view.count) + 4; ++row) {
      const float y =
          base.y + (static_cast<float>(row) + 4) * kRowStep + view.list_offset;
      if (std::abs(base.y + kRowStep * 4 - y) >= kRowClip) continue;
      sprite(kRowImage, {base.x, y});
      if (row < 0 || row >= static_cast<int>(view.count)) continue;
      const auto id = view.topics.at(static_cast<std::size_t>(row));
      const auto category = id < kSpellCategoryFirst   ? 0U
                            : id < kWorldCategoryFirst ? 1U
                                                       : 2U;
      sprite(kCategoryImage, {base.x, y}, kCategories.at(category));
      const auto& title = titles.at(id);
      const Point label{base.x + kLabelCenterX - title.width * .5F,
                        y + kLabelCenterY - kTextSize * .5F};
      text(title, view.input_icons,
           {label.x + kShadowOffset, label.y + kShadowOffset}, 1, true);
      text(title, view.input_icons, label);
      if (view.topic_strength.at(static_cast<std::size_t>(row)) > 0) {
        sprite(kFocusImage, {base.x, y},
               {1 + view.highlight, 1 + view.highlight, 1 + view.highlight,
                view.topic_strength.at(static_cast<std::size_t>(row))});
        if (static_cast<std::size_t>(row) == view.selected &&
            view.reading_strength > 0) {
          sprite(kReadingImage, {base.x, y},
                 {1 + view.highlight, 1 + view.highlight, 1 + view.highlight,
                  view.reading_strength});
        }
      }
      if (!view.read.at(id)) sprite(kUnreadImage, {base.x, y});
    }
    if (seconds >= kSliderEntry) {
      scrollbar({kListSliderX, kListSliderY}, kListSliderHeight,
                view.count > 1 ? static_cast<float>(view.selected) /
                                     static_cast<float>(view.count - 1)
                               : 0,
                view.count > 1);
      scrollbar(
          {detail.x + kBodySliderX, detail.y + kBodySliderY}, kBodySliderHeight,
          view.maximum_scroll > 0 ? view.scroll_pixels / view.maximum_scroll
                                  : 0,
          view.maximum_scroll > 0);
    }
    if (seconds >= kFooterEntry) {
      const auto prompt = [&](const MenuPrompt& value, float x) {
        constexpr float kScale = 1.F / 3;
        for (unsigned i = 0; i < value.count; ++i) {
          emit(icon_sprite(value.icons.at(i),
                           x + static_cast<float>(i) * value.icon_pitch,
                           kPromptY, kInputIconPixels * kScale));
        }
      };
      prompt(view.prompts.confirm, kPromptX);
      text(prompts.at(0), view.input_icons,
           {kPromptTextX + view.prompts.confirm.text_advance -
                menu_prompts(true).confirm.text_advance,
            kPromptTextY},
           1, false, false, 0, kPromptSize / kTextSize);
      prompt(view.prompts.back, kPromptSecondX);
      text(prompts.at(1), view.input_icons,
           {kPromptSecondTextX + view.prompts.back.text_advance -
                menu_prompts(true).back.text_advance,
            kPromptTextY},
           1, false, false, 0, kPromptSize / kTextSize);
    }
    output.set_sprites(std::span(commands).first(count));
  }
};
PauseHelpSurface::PauseHelpSurface(resources::ResourceService& resources,
                                   render::SpriteOutput& output,
                                   resources::ArchiveBytes input_icons)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output, std::move(input_icons))) {}
PauseHelpSurface::~PauseHelpSurface() = default;
void PauseHelpSurface::prepare() { state_->prepare(); }
bool PauseHelpSurface::ready() { return state_->ready(); }
const std::array<unsigned, help_topic_count>& PauseHelpSurface::line_counts()
    const {
  if (!state_->prepared) throw std::logic_error("Help content is not ready");
  return state_->lines;
}
void PauseHelpSurface::show(const PauseHelpPresentation& presentation) {
  state_->show(presentation);
}
void PauseHelpSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
