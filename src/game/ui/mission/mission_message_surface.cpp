// Prepare only original font/dialog art; shape authored Unicode/color markup
// into bounded glyphs when a page changes, then position those glyphs in the
// shared native dialog geometry. Textures stay resident until explicit end.
// Core SL.con.3 (#rsl-bounds) bounds glyph/line/command arrays; performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) keeps frame work
// allocation-free. Unsupported markup fails rather than rendering raw tags.
#include "mission_message_surface.hpp"

#include <array>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <utility>

#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "game/ui/common/input_icon.hpp"
#include "game/ui/common/sprite_text.hpp"
namespace bw::game {
namespace {
constexpr resources::ResourceTag kTag = 26;
constexpr std::array<std::string_view, 2> kGroups{"sprite.font",
                                                  "sprite.system"};
constexpr unsigned kEdge = 326;
constexpr float kHalf = .5F, kCenterX = 640, kAlpha = 178.F / 255;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& service) {
  const auto domain = service.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
render::Color text_color(std::string_view tag) {
  if (tag == "col end") {
    return kText;
  }
  if (tag == "col yellow") {
    return {1, 1, 0, 1};
  }
  if (tag == "col blue") {
    return {0, 0, 1, 1};
  }
  constexpr std::size_t kHexDigits = 8, kHexPrefix = 5;
  constexpr int kHexRadix = 16;
  constexpr unsigned kMask = 255, kByteBits = 8, kRedBits = 16, kAlphaBits = 24;
  if (tag.starts_with("col #") && tag.size() == kHexPrefix + kHexDigits) {
    std::uint32_t value{};
    const auto code = tag.substr(kHexPrefix);
    const auto parsed =
        std::from_chars(std::to_address(code.begin()),
                        std::to_address(code.end()), value, kHexRadix);
    if (parsed.ec == std::errc{} && parsed.ptr == std::to_address(code.end())) {
      return {static_cast<float>((value >> kRedBits) & kMask) / kMask,
              static_cast<float>((value >> kByteBits) & kMask) / kMask,
              static_cast<float>(value & kMask) / kMask,
              static_cast<float>(value >> kAlphaBits) / kMask};
    }
  }
  throw std::runtime_error("Unsupported mission instruction markup");
}
}  // namespace
struct MissionMessageSurface::State {
  struct Glyph {
    render::Sprite sprite;
    std::size_t line{};
  };
  resources::ResourceService& resources;
  render::SpriteOutput& output;
  memory::Allocator<std::byte> allocator;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::optional<formats::BitmapFont> font;
  render::TextureHandle atlas, edge, solid, icons;
  resources::ArchiveBytes input_icons;
  MenuPrompt confirm = menu_prompts(MenuInputDevice::keyboard_mouse).confirm;
  float edge_width{}, edge_height{};
  std::array<Glyph, render::kMaximumSprites> glyphs{};
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  static constexpr std::size_t kLines = 64;
  std::array<float, kLines> widths{};
  std::size_t glyph_count{}, line_count{};
  std::string_view shaped;
  bool begun{}, prepared{}, visible{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer,
        resources::ArchiveBytes encoded_icons)
      : resources(service),
        output(renderer),
        allocator(storage_for(service)),
        input_icons(std::move(encoded_icons)) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void hide() noexcept {
    if (visible) {
      output.clear_sprites();
    }
    visible = false;
  }
  void end() noexcept {
    hide();
    for (auto* handle : {&atlas, &edge, &solid, &icons}) {
      if (handle->value != 0) {
        output.release_texture(*handle);
      }
      *handle = {};
    }
    font.reset();
    for (auto& lease : leases) {
      lease.reset();
    }
    resources.release(kTag);
    shaped = {};
    begun = prepared = false;
  }
  void begin() {
    end();
    begun = true;
    for (auto group : kGroups) {
      resources.request(group, kTag);
    }
  }
  bool ready() {
    if (prepared) {
      return true;
    }
    if (!begun) {
      throw std::logic_error("Mission message surface not begun");
    }
    const auto report = resources.pump(kTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) {
      return false;
    }
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      leases.at(group) = resources.acquire(kGroups.at(group));
      if (!leases.at(group)) {
        throw std::runtime_error("Mission dialog resource missing");
      }
      const auto& lease = *leases.at(group);
      const bool is_font = group == 0;
      if (is_font) {
        font = formats::BitmapFont::parse(lease.bytes(), allocator);
      }
      const auto& archive = lease.archive();
      std::size_t index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") {
          continue;
        }
        constexpr std::size_t kIdBytes = 2;
        constexpr unsigned kByteBits = 8;
        if (!is_font && index >= lease.bytes().size() / kIdBytes) {
          throw std::runtime_error("Mission dialog sprite identity missing");
        }
        const auto id =
            is_font ? 0U
                    : static_cast<unsigned>(lease.bytes()[index * kIdBytes]) |
                          (static_cast<unsigned>(
                               lease.bytes()[index * kIdBytes + 1])
                           << kByteBits);
        ++index;
        if (!is_font && id != kEdge) {
          continue;
        }
        if ((is_font && atlas.value != 0) || (!is_font && edge.value != 0)) {
          throw std::runtime_error("Duplicate mission dialog texture");
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        if (is_font) {
          if (!font || decoded.width != font->atlas_width() ||
              decoded.height != font->atlas_height()) {
            throw std::runtime_error("Mission font atlas mismatch");
          }
          atlas = output.upload_texture(decoded);
        } else {
          edge = output.upload_texture(decoded);
          edge_width = static_cast<float>(decoded.width);
          edge_height = static_cast<float>(decoded.height);
        }
      }
    }
    if (atlas.value == 0 || edge.value == 0) {
      throw std::runtime_error("Mission dialog artwork missing");
    }
    constexpr std::array<std::uint8_t, 4> kWhite{255, 255, 255, 255};
    solid = output.upload_texture(1, 1, kWhite);
    const auto icon_atlas = decode_input_icon_atlas(
        input_icons, memory::Allocator<std::uint8_t>(allocator));
    icons = output.upload_texture(icon_atlas.width, icon_atlas.height,
                                  icon_atlas.rgba);
    prepared = true;
    return true;
  }
  // Preserve authored Unicode and color runs; no ASCII rewriting or raw tags.
  void shape(std::string_view text) {
    if (!font) {
      throw std::logic_error("Mission font is not prepared");
    }
    shaped = {};
    glyph_count = 0;
    line_count = 1;
    widths.fill(0);
    auto color = kText;
    for (std::size_t cursor = 0; cursor < text.size();) {
      if (text[cursor] == '<') {
        const auto end = text.find('>', cursor);
        if (end == std::string_view::npos) {
          throw std::runtime_error("Unterminated mission markup");
        }
        color = text_color(text.substr(cursor + 1, end - cursor - 1));
        cursor = end + 1;
        continue;
      }
      const auto begin = cursor;
      const auto character = next_sprite_character(text, cursor);
      if (character == '\n') {
        if (line_count == widths.size()) {
          throw std::length_error("Mission text line capacity");
        }
        ++line_count;
        continue;
      }
      const auto metric = font->glyph(character);
      if (!metric) {
        throw std::runtime_error("Original mission glyph missing");
      }
      std::array<render::Sprite, 1> glyph;
      const auto line = line_count - 1;
      const auto count = append_sprite_text(
          *font, atlas, text.substr(begin, cursor - begin),
          {widths.at(line), static_cast<float>(line) * menu_dialog_font_size,
           menu_dialog_font_size},
          color, glyph);
      if (count != 0) {
        // Reserve panel, two borders and up to four binding glyphs.
        constexpr std::size_t kPanelCommands = 7;
        if (glyph_count == glyphs.size() - kPanelCommands) {
          throw std::length_error("Mission text glyph capacity");
        }
        glyphs.at(glyph_count++) = {glyph[0], line};
      }
      widths.at(line) += static_cast<float>(metric->advance) *
                         menu_dialog_font_size /
                         static_cast<float>(font->pixel_size());
    }
    shaped = text;
  }
  void show(std::string_view text, const MenuDialogPresentation& view) {
    if (!prepared || !font) {
      throw std::logic_error("Mission dialog not prepared");
    }
    if (shaped.data() != text.data() || shaped.size() != text.size()) {
      shape(text);
    }
    const auto geometry = menu_dialog_geometry(
        view.progress, static_cast<unsigned>(line_count), false);
    std::size_t count = 0;
    if (geometry.bottom > geometry.top) {
      commands.at(count++) = {solid,
                              {0, geometry.top, render::kCanvasWidth,
                               geometry.bottom - geometry.top},
                              {0, 0, 1, 1},
                              {0, 0, 0, kAlpha * geometry.opacity}};
    }
    for (float y : {geometry.top, geometry.bottom}) {
      commands.at(count++) = {
          edge,
          {0, y - edge_height * kHalf, edge_width, edge_height},
          {0, 0, 1, 1},
          {1, 1, 1, geometry.opacity}};
    }
    for (std::size_t i = 0; i < glyph_count; ++i) {
      auto sprite = glyphs.at(i).sprite;
      sprite.destination.x += kCenterX - widths.at(glyphs.at(i).line) * kHalf;
      sprite.destination.y += geometry.text_y - menu_dialog_font_size * kHalf;
      sprite.color.alpha *= geometry.decoration;
      commands.at(count++) = sprite;
    }
    // The original continuation indicator uses fixed UI action bindings and
    // the final panel edge, and is disabled as soon as closing begins.
    if (!view.closing && !view.complete) {
      constexpr float kPromptX = 1076, kPromptBottomOffset = 64;
      constexpr float kPromptSize = static_cast<float>(kInputIconPixels) / 3;
      const auto bottom =
          menu_dialog_geometry(1, static_cast<unsigned>(line_count), false)
              .bottom;
      for (unsigned i = 0; i < confirm.count; ++i) {
        commands.at(count++) = {
            icons,
            {kPromptX + static_cast<float>(i) * confirm.icon_pitch,
             bottom - kPromptBottomOffset, kPromptSize, kPromptSize},
            input_icon_uv(confirm.icons.at(i)),
            {}};
      }
    }
    output.set_sprites(std::span(commands).first(count));
    visible = true;
  }
};
MissionMessageSurface::MissionMessageSurface(
    resources::ResourceService& resources, render::SpriteOutput& output,
    resources::ArchiveBytes input_icons)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output, std::move(input_icons))) {}
MissionMessageSurface::~MissionMessageSurface() = default;
void MissionMessageSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool MissionMessageSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void MissionMessageSurface::show(std::string_view text,
                                 const MenuDialogPresentation& presentation) {
  state_->show(text, presentation);
}
void MissionMessageSurface::hide() noexcept { state_->hide(); }
void MissionMessageSurface::end() noexcept { state_->end(); }
void MissionMessageSurface::set_input_device(MenuInputDevice device) {
  state_->confirm = menu_prompts(device).confirm;
}
}  // namespace bw::game
