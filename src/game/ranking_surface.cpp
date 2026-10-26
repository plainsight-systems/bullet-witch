// Original ranking shell and failure dialog adapter. Native descriptors drive
// the five entry tracks; prepared source network text supplies the
// acknowledgment. Core I.1 and MEM.9 were reread for implementation: copied
// presentation owns no authority, initialization bounds GPU residency, and draw
// never uploads.
#include "ranking_surface.hpp"

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
#include "sprite_text.hpp"
namespace bw::game {
namespace {
struct Position {
  float x{}, y{};
};
struct Track {
  unsigned id{};
  Position from, to;
  float begin{}, end{};
};
constexpr resources::ResourceTag kRankingTag = 25;
constexpr std::array<std::string_view, 4> kGroups{
    "sprite.ranking", "sprite.font", "sprite.system", "sprite.text.sys"};
constexpr std::size_t kImageCapacity = 12, kTextCapacity = 512;
constexpr unsigned kSolidImage = 10000, kDialogEdge = 326, kIconBase = 11000;
constexpr std::array<unsigned, 5> kBackgrounds{268, 270, 275, 276, 269};
constexpr std::array<unsigned, 3> kIcons{120, 94, 54};
constexpr std::array<Track, 5> kTracks{{{268, {0, 0}, {0, 0}, 0, 1},
                                        {270, {-730, 480}, {0, 0}, .2F, 1.5F},
                                        {275, {1274, 30}, {250, 30}, .2F, 1.5F},
                                        {276, {-730, 0}, {0, 0}, 0, 1},
                                        {269, {0, 1330}, {0, 610}, 0, 1}}};
constexpr float kHalf = .5F, kDialogX = 640, kDialogTextSize = 48,
                kDialogAlpha = 178.F / 255, kTrackSeconds = 1.5F;
constexpr render::Color kText{230.F / 255, 228.F / 255, 213.F / 255, 1};
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
bool unit_channel(float v) { return std::isfinite(v) && v >= 0 && v <= 1; }
}  // namespace
struct RankingSurface::State {
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
  std::array<char, kTextCapacity> message{};
  resources::ArchiveBytes icon_bytes;
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool begun{}, prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer,
        resources::ArchiveBytes icons)
      : resources(service),
        output(renderer),
        allocator(storage_for(service)),
        icon_bytes(std::move(icons)) {}
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
    resources.release(kRankingTag);
    begun = false;
    prepared = false;
    count = 0;
  }
  void begin() {
    end();
    begun = true;
    for (const auto group : kGroups) resources.request(group, kRankingTag);
  }
  const Image& image(unsigned id) const {
    for (const auto& entry : std::span(images).first(image_count)) {
      if (entry.id == id) return entry;
    }
    throw std::runtime_error("Original pause image missing");
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
    if (!begun) throw std::logic_error("Pause surface has not begun");
    const auto report = resources.pump(kRankingTag);
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
        pause_text::prepare_text(
            pause_text::localized_text(
                table, pause_text::TextNamespace::network, "0015"),
            message);
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
        if ((group == kSystemGroup && id != kDialogEdge) ||
            (group == 0 &&
             std::ranges::find(kBackgrounds, id) == kBackgrounds.end())) {
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
    (void)image(kDialogEdge);
    (void)image(0);
    load_icons();
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
  void dialog(const RankingPresentation& view) {
    const std::string_view text = message.data();
    const auto lines =
        1 + static_cast<unsigned>(std::ranges::count(text, '\n'));
    const auto geometry =
        menu_dialog_geometry(view.dialog_progress, lines, false);
    if (geometry.bottom > geometry.top) {
      commands.at(count++) = {image(kSolidImage).texture,
                              {0, geometry.top, render::kCanvasWidth,
                               geometry.bottom - geometry.top},
                              {0, 0, 1, 1},
                              {0, 0, 0, kDialogAlpha * geometry.opacity}};
    }
    const auto& edge = image(kDialogEdge);
    sprite(kDialogEdge, {0, geometry.top - edge.height * kHalf},
           {1, 1, 1, geometry.opacity});
    sprite(kDialogEdge, {0, geometry.bottom - edge.height * kHalf},
           {1, 1, 1, geometry.opacity});
    auto color = kText;
    color.alpha = geometry.decoration;
    centered_text(text, kDialogX, geometry.text_y, kDialogTextSize, color);
    constexpr float kConfirmX = 1076, kConfirmBottom = 64, kIconScale = 1.F / 3;
    const auto settled = menu_dialog_geometry(1, lines, false);
    for (unsigned i = 0; i < view.prompts.confirm.count; ++i) {
      const auto& art = image(kIconBase + view.prompts.confirm.icons.at(i));
      commands.at(count++) = {
          art.texture,
          {kConfirmX + static_cast<float>(i) * view.prompts.confirm.icon_pitch,
           settled.bottom - kConfirmBottom, art.width * kIconScale,
           art.height * kIconScale},
          {0, 0, 1, 1},
          {}};
    }
  }
  void show(const RankingPresentation& view) {
    if (!prepared) throw std::logic_error("Ranking surface is not prepared");
    if (view.phase > RankingPhase::concealing || !std::isfinite(view.elapsed) ||
        view.elapsed < 0 || !unit_channel(view.opacity) ||
        !unit_channel(view.dialog_progress) ||
        !valid_menu_prompt(view.prompts.confirm)) {
      throw std::invalid_argument("Invalid ranking presentation");
    }
    count = 0;
    if (view.phase == RankingPhase::complete) {
      output.clear_sprites();
      return;
    }
    const auto time = view.phase == RankingPhase::entering ? view.elapsed
                      : view.phase == RankingPhase::concealing
                          ? std::max(0.F, kTrackSeconds - view.elapsed)
                          : kTrackSeconds;
    for (const auto& track : kTracks) {
      const auto t = math::quintic_ease(std::clamp(
          (time - track.begin) / (track.end - track.begin), 0.F, 1.F));
      sprite(track.id, {math::linear_interpolate(track.from.x, track.to.x, t),
                        math::linear_interpolate(track.from.y, track.to.y, t)});
    }
    if (view.phase == RankingPhase::unavailable) dialog(view);
    if (view.opacity < 1) {
      commands.at(count++) = {
          image(kSolidImage).texture,
          {0, 0, render::kCanvasWidth, render::kCanvasHeight},
          {0, 0, 1, 1},
          {0, 0, 0, 1 - view.opacity}};
    }
    output.set_sprites(std::span(commands).first(count));
  }
};
RankingSurface::RankingSurface(resources::ResourceService& resources,
                               render::SpriteOutput& output,
                               resources::ArchiveBytes input_icons)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output, std::move(input_icons))) {}
RankingSurface::~RankingSurface() = default;
void RankingSurface::begin() {
  try {
    state_->begin();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool RankingSurface::ready() {
  try {
    return state_->ready();
  } catch (...) {
    state_->end();
    throw;
  }
}
void RankingSurface::show(const RankingPresentation& view) {
  state_->show(view);
}
void RankingSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
