// Pause-menu resource and presentation adapter; policy remains in
// PauseMenuFlow. Decode original art/font/system text during activation, retain
// leases and GPU handles, validate copied presentation, then submit a fixed
// ordered draw list. Failure unwinds partial preparation; teardown releases
// only this tag's assets. Original sprite matrices scale about the geometric
// center and sort the chosen item above its siblings. Full-width English text
// is normalized at preparation for the existing ASCII font adapter. Core R.14
// (#rr-ap), E.6 (#re-raii) and performance GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) were retrieved for the
// implementation: bounds travel with views, setup is exception-safe, textures
// stay resident, and show performs neither allocation nor upload.
// E.4 (#re-design-invariants) also preserves positive-area sprite submissions
// when the source dialog expansion reaches its collapsed endpoint.
#include "pause_menu_surface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>

#include "bw/content/formats/bitmap_font.hpp"
#include "bw/content/formats/text_table.hpp"
#include "bw/content/formats/world_texture.hpp"
#include "bw/math/scalar.hpp"
#include "game/ui/common/localized_text.hpp"
#include "game/ui/common/sprite_text.hpp"

namespace bw::game {
namespace {
struct Position {
  float x{}, y{};
};
constexpr resources::ResourceTag kPauseTag = 20;
constexpr std::array<std::string_view, 4> kGroups{
    "sprite.pauseMenu", "sprite.font", "sprite.system", "sprite.text.sys"};
constexpr std::size_t kImageCapacity = 14, kTextCapacity = 256;
constexpr unsigned kSolidImage = 10000, kDialogEdge = 326, kDialogButton = 327;
constexpr std::array<unsigned, 4> kBackgrounds{229, 228, 226, 227};
constexpr std::array<float, 4> kBackgroundY{0, 36, 160, 272};
constexpr std::array<unsigned, 6> kRows{231, 232, 234, 235, 233, 230};
constexpr std::array<std::string_view, 5> kSystemKeys{"0001", "0002", "0005",
                                                      "0006", "0007"};
constexpr float kHalf = .5F, kDialogX = 640,
                kDialogTextSize = menu_dialog_font_size,
                kDialogAlpha = 178.F / 255;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
bool unit_channel(float value) {
  return std::isfinite(value) && value >= 0 && value <= 1;
}
}  // namespace
struct PauseMenuSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  resources::ResourceService& resources;
  render::SpriteOutput& output;
  memory::Allocator<std::byte> allocator;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, kImageCapacity> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<std::array<char, kTextCapacity>, kSystemKeys.size()> texts{};
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool begun{}, prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer)
      : resources(service), output(renderer), allocator(storage_for(service)) {}
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
    resources.release(kPauseTag);
    begun = false;
    prepared = false;
    count = 0;
  }
  void begin() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kPauseTag);
  }
  const Image& image(unsigned id) const {
    for (const auto& entry : std::span(images).first(image_count)) {
      if (entry.id == id) return entry;
    }
    throw std::runtime_error("Original pause image missing");
  }
  bool ready() {
    if (prepared) return true;
    if (!begun) throw std::logic_error("Pause surface has not begun");
    const auto report = resources.pump(kPauseTag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    if (report.state != resources::LoadState::ready) return false;
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      leases.at(group) = resources.acquire(kGroups.at(group));
      if (!leases.at(group)) {
        throw std::runtime_error("Pause resource publication missing");
      }
      const auto& lease = leases.at(group).value();
      constexpr std::size_t kFontGroup = 1, kSystemGroup = 2, kTextGroup = 3;
      if (group == kTextGroup) {
        const auto table = formats::TextTable::parse(lease.bytes());
        for (std::size_t i = 0; i < kSystemKeys.size(); ++i) {
          localized_text::prepare_text(
              localized_text::system_text(table, kSystemKeys.at(i)),
              texts.at(i));
        }
        continue;
      }
      const bool is_font = group == kFontGroup;
      if (is_font) font = formats::BitmapFont::parse(lease.bytes(), allocator);
      const auto& archive = lease.archive();
      std::size_t index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        constexpr std::size_t kIdentityBytes = 2;
        constexpr unsigned kByteBits = 8;
        if (!is_font && index >= lease.bytes().size() / kIdentityBytes) {
          throw std::runtime_error("Pause sprite identity missing");
        }
        const auto id =
            is_font
                ? 0U
                : static_cast<unsigned>(lease.bytes()[index * kIdentityBytes]) |
                      (static_cast<unsigned>(
                           lease.bytes()[index * kIdentityBytes + 1])
                       << kByteBits);
        ++index;
        if (group == kSystemGroup && id != kDialogEdge && id != kDialogButton) {
          continue;
        }
        if (image_count == images.size()) {
          throw std::length_error("Pause texture capacity exceeded");
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        if (is_font &&
            (!font || index != 1 || decoded.width != font->atlas_width() ||
             decoded.height != font->atlas_height())) {
          throw std::runtime_error("Pause font atlas disagrees with metrics");
        }
        const auto texture = output.upload_texture(decoded);
        images.at(image_count++) = {id, texture,
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
      }
    }
    if (image_count == images.size()) {
      throw std::length_error("Pause primitive texture capacity exceeded");
    }
    constexpr std::array<std::uint8_t, 4> kWhitePixel{255, 255, 255, 255};
    images.at(image_count++) = {kSolidImage,
                                output.upload_texture(1, 1, kWhitePixel), 1, 1};
    // Readiness means every required identity exists, not merely the archives.
    for (const auto id : kBackgrounds) (void)image(id);
    for (const auto id : kRows) (void)image(id);
    (void)image(kDialogEdge);
    (void)image(kDialogButton);
    (void)image(0);
    prepared = true;
    return true;
  }
  void sprite(unsigned id, Position position, render::Color color = {},
              float scale = 1) {
    const auto& art = image(id);
    const auto width = art.width * scale;
    const auto height = art.height * scale;
    commands.at(count++) = {
        art.texture,
        {position.x + (art.width - width) * kHalf,
         position.y + (art.height - height) * kHalf, width, height},
        {0, 0, 1, 1},
        color};
  }
  void centered_text(std::string_view value, float x, float y, float size,
                     render::Color color) {
    if (!font) throw std::logic_error("Pause font is not prepared");
    std::size_t begin = 0;
    for (;;) {
      const auto end = value.find('\n', begin);
      const auto line = value.substr(
          begin, end == std::string_view::npos ? end : end - begin);
      float width = 0;
      for (const auto character : line) {
        const auto glyph = font->glyph(static_cast<unsigned char>(character));
        if (!glyph) throw std::runtime_error("Pause text glyph missing");
        width += static_cast<float>(glyph->advance) * size /
                 static_cast<float>(font->pixel_size());
      }
      count += append_sprite_text(*font, image(0).texture, line,
                                  {x - width * kHalf, y - size * kHalf, size},
                                  color, std::span(commands).subspan(count));
      if (end == std::string_view::npos) break;
      begin = end + 1;
      y += size;
    }
  }
  void dialog(const PauseMenuPresentation& view) {
    const bool confirmation = view.phase == PauseMenuPhase::confirmation;
    auto message = view.message;
    if (confirmation) {
      constexpr std::size_t kFirstConfirmation = 2;
      const auto action = static_cast<std::size_t>(view.selected);
      const auto first = static_cast<std::size_t>(PauseMenuAction::checkpoint);
      message = texts.at(kFirstConfirmation + action - first).data();
    }
    const auto lines =
        1 + static_cast<unsigned>(std::ranges::count(message, '\n'));
    const auto geometry =
        menu_dialog_geometry(view.dialog_progress, lines, confirmation);
    const auto top = geometry.top, bottom = geometry.bottom;
    const auto opacity = geometry.opacity, decoration = geometry.decoration;
    // The authored expansion begins collapsed. It has no raster coverage;
    // keep the native sprite output's positive-area submission contract.
    if (bottom > top) {
      commands.at(count++) = {image(kSolidImage).texture,
                              {0, top, render::kCanvasWidth, bottom - top},
                              {0, 0, 1, 1},
                              {0, 0, 0, kDialogAlpha * opacity}};
    }
    const auto& edge = image(kDialogEdge);
    sprite(kDialogEdge, {0, top - edge.height * kHalf}, {1, 1, 1, opacity});
    sprite(kDialogEdge, {0, bottom - edge.height * kHalf}, {1, 1, 1, opacity});
    auto color = kText;
    color.alpha = decoration;
    centered_text(message, kDialogX, geometry.text_y, kDialogTextSize, color);
    if (!confirmation) return;
    constexpr float kLeftChoiceX = 448, kRightChoiceX = 832,
                    kInactiveAlpha = .8F;
    const auto choice_y = geometry.choice_y;
    const auto& button = image(kDialogButton);
    for (unsigned choice = 0; choice < 2; ++choice) {
      const auto x = choice == 0 ? kLeftChoiceX : kRightChoiceX;
      const bool selected = (choice == 0) == view.confirmation_yes;
      const auto choice_color =
          selected ? kText
                   : render::Color{kText.red * kHalf, kText.green * kHalf,
                                   kText.blue * kHalf, kInactiveAlpha};
      auto button_color = selected ? render::Color{} : choice_color;
      button_color.alpha *= decoration;
      sprite(kDialogButton,
             {x - button.width * kHalf, choice_y - button.height * kHalf},
             button_color);
      auto text_color = choice_color;
      text_color.alpha *= decoration;
      centered_text(texts.at(choice).data(), x, choice_y, kDialogTextSize,
                    text_color);
    }
  }
  void show(const PauseMenuPresentation& view) {
    if (!prepared) throw std::logic_error("Pause surface is not prepared");
    const auto selected = static_cast<std::size_t>(view.selected);
    if (selected >= kRows.size() ||
        static_cast<unsigned>(view.phase) >
            static_cast<unsigned>(PauseMenuPhase::retiring) ||
        !unit_channel(view.shade_alpha) ||
        !unit_channel(view.dialog_progress) ||
        !std::ranges::all_of(view.background_x,
                             [](float x) { return std::isfinite(x); }) ||
        !std::ranges::all_of(view.background_alpha, unit_channel) ||
        !std::ranges::all_of(view.items, [](const PauseMenuItem& item) {
          return std::isfinite(item.x) && std::isfinite(item.y) &&
                 std::isfinite(item.scale) && item.scale >= 0 &&
                 unit_channel(item.alpha) && unit_channel(item.brightness);
        })) {
      throw std::invalid_argument("Invalid pause presentation");
    }
    if (view.phase == PauseMenuPhase::confirmation &&
        (view.selected < PauseMenuAction::checkpoint ||
         view.selected > PauseMenuAction::retire)) {
      throw std::invalid_argument("Pause confirmation has no operation");
    }
    count = 0;
    if (view.phase == PauseMenuPhase::inactive) {
      output.clear_sprites();
      return;
    }
    commands.at(count++) = {image(kSolidImage).texture,
                            {0, 0, render::kCanvasWidth, render::kCanvasHeight},
                            {0, 0, 1, 1},
                            {0, 0, 0, view.shade_alpha}};
    for (std::size_t i = 0; i < kBackgrounds.size(); ++i) {
      sprite(kBackgrounds.at(i), {view.background_x.at(i), kBackgroundY.at(i)},
             {1, 1, 1, view.background_alpha.at(i)});
    }
    const auto row = [&](std::size_t index) {
      const auto& item = view.items.at(index);
      sprite(kRows.at(index), {item.x, item.y},
             {item.brightness, item.brightness, item.brightness, item.alpha},
             item.scale);
    };
    for (std::size_t i = 0; i < kRows.size(); ++i) {
      if (i != selected) row(i);
    }
    row(selected);
    if (view.phase == PauseMenuPhase::confirmation ||
        view.phase == PauseMenuPhase::unavailable) {
      dialog(view);
    }
    output.set_sprites(std::span(commands).first(count));
  }
};
PauseMenuSurface::PauseMenuSurface(resources::ResourceService& resources,
                                   render::SpriteOutput& output)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output)) {}
PauseMenuSurface::~PauseMenuSurface() = default;
void PauseMenuSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool PauseMenuSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void PauseMenuSurface::show(const PauseMenuPresentation& presentation) {
  state_->show(presentation);
}
void PauseMenuSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
