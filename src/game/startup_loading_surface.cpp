// DDS resources retain original texture metadata through the shared decoder;
// native construction consumes the immutable launch mip policy. Raw icon PNGs
// remain a distinct base-only upload. Both-phase E.6/I.11 and COPY.5 preserve
// borrowed archive views, candidate cleanup and renderer-owned identities.
// Loading assets remain resident across safe loading pulses. The original
// presentation uses repeated sliding art and localized text, not a fabricated
// percentage. Header/source evidence define the ownership and timing contract.
// Implementation Core E.6 (#re-raii) retires partial textures/leases on
// failure; performance GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md)
// changes only bounded sprite constants during presentation, never pixels or
// device owners.
#include "startup_loading_surface.hpp"

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

namespace bw::game {
namespace {
constexpr resources::ResourceTag kLoadingTag = 14;
constexpr unsigned kBackgroundId = 332, kSmallArtId = 447, kLargeArtId = 448;
constexpr std::size_t kImageCount = 8, kGroupCount = 3;
constexpr unsigned kItemCount = 19;
constexpr float kTextSize = 28, kTextX = 540, kTextY = 152;
constexpr float kFadeSeconds = 0.2F;
constexpr unsigned kIconIdentityBase = 1000;
constexpr std::array<unsigned, 4> kControllerIcons{124, 148, 125, 121};
constexpr float kColorRange = 255;
constexpr render::Color kTextColor{230 / kColorRange, 228 / kColorRange,
                                   213 / kColorRange, 1};
constexpr std::array<render::Rect, 10> kBackgroundPositions{{{0, 434, 0, 0},
                                                             {0, 440, 0, 0},
                                                             {16, 388, 0, 0},
                                                             {21, 414, 0, 0},
                                                             {71, 300, 0, 0},
                                                             {29, 392, 0, 0},
                                                             {13, 352, 0, 0},
                                                             {32, 398, 0, 0},
                                                             {18, 414, 0, 0},
                                                             {13, 392, 0, 0}}};
constexpr float kQuarterTurn = 1.5707963705062866F;
constexpr float kDiagonalDirection = 4.363323211669922F;
constexpr float kDiagonalRotation = 0.3490658700466156F;
struct ArtGroup {
  unsigned image{}, count{};
  float x{}, y{}, direction{}, rotation{}, spacing{}, start{}, begin{}, end{};
  bool overshoot{};
};
constexpr std::array<ArtGroup, 6> kArtGroups{
    {{kLargeArtId, 11, 0, 90, kQuarterTurn, 0, 120, -1280, 0, .8F},
     {kLargeArtId, 7, 531, 604, kQuarterTurn, 0, 120, 1280, 0, .8F},
     {kLargeArtId, 5, 246, 384, kDiagonalDirection, kDiagonalRotation, 120,
      1280, 0, .8F},
     {kLargeArtId, 8, 1200, 190, kDiagonalDirection, kDiagonalRotation, 120,
      -1280, 0, .8F},
     {kSmallArtId, 7, 172, 370, 0, 0, -80, -1280, .05F, .87F, true},
     {kSmallArtId, 7, 409, 370, 0, 0, -80, -1280, .1F, .92F, true}}};
float slide(const ArtGroup& group, float elapsed) {
  const auto t = std::clamp((elapsed - group.begin) / (group.end - group.begin),
                            0.0F, 1.0F);
  constexpr float kQuintic = 6, kQuartic = 15, kCubic = 10;
  const auto q = t * t * t * (t * (t * kQuintic - kQuartic) + kCubic);
  constexpr float kOvershootQuadratic = 3.75F, kOvershootCubic = 2.75F;
  const auto blend =
      group.overshoot ? q * q * (kOvershootQuadratic - kOvershootCubic * q) : q;
  return group.start * (1 - blend);
}
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& service) {
  const auto domain = service.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
unsigned image_id(std::span<const std::uint8_t> bytes, std::size_t index) {
  constexpr std::size_t kWordBytes = 2;
  constexpr unsigned kByteBits = 8;
  if (index >= bytes.size() / kWordBytes) {
    throw std::runtime_error("Loading sprite metadata omits an image");
  }
  const auto offset = index * kWordBytes;
  return static_cast<unsigned>(bytes[offset]) |
         (static_cast<unsigned>(bytes[offset + 1]) << kByteBits);
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
  throw std::runtime_error("Unknown loading font channel");
}
}  // namespace
struct StartupLoadingSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  memory::Allocator<std::byte> allocator;
  resources::ResourceService& resources;
  render::SpriteOutput& renderer;
  std::array<std::optional<resources::ResourceLease>, kGroupCount> leases;
  std::array<Image, kImageCount> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<std::string_view, kItemCount> labels{};
  resources::ArchiveBytes text_bytes;
  render::Rect background_position;
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t command_count{};
  bool prepared{};
  State(resources::ResourceService& service, render::SpriteOutput& output)
      : allocator(storage_for(service)),
        resources(service),
        renderer(output),
        text_bytes(allocator) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    renderer.clear_sprites();
    while (image_count != 0) {
      renderer.release_texture(images.at(--image_count).texture);
    }
    font.reset();
    labels.fill({});
    text_bytes.clear();
    for (auto& lease : leases) lease.reset();
    resources.release(kLoadingTag);
    command_count = 0;
    prepared = false;
  }
  const Image& image(unsigned id) const {
    for (std::size_t index = 0; index < image_count; ++index) {
      if (images.at(index).id == id) return images.at(index);
    }
    throw std::runtime_error("Original loading image is missing");
  }
  void append(render::Sprite sprite) {
    if (command_count == commands.size()) {
      throw std::runtime_error("Loading draw command capacity exceeded");
    }
    commands.at(command_count++) = sprite;
  }
  void load_images(std::size_t group, bool is_font) {
    const auto& retained = leases.at(group);
    if (!retained) throw std::logic_error("Loading resource lease is missing");
    const auto& lease = retained.value();
    if (is_font) font = formats::BitmapFont::parse(lease.bytes(), allocator);
    std::size_t image_index = 0;
    const auto& archive = lease.archive();
    for (std::size_t record = 0; record < archive.records().size(); ++record) {
      if (archive.records()[record].type != "DDS ") continue;
      const auto id = is_font ? 0 : image_id(lease.bytes(), image_index);
      ++image_index;
      const bool selected =
          is_font || (group == 0 && id == kBackgroundId) ||
          (group == 1 && (id == kSmallArtId || id == kLargeArtId));
      if (!selected) continue;
      if (image_count == images.size()) {
        throw std::runtime_error("Unexpected loading image count");
      }
      const auto decoded = formats::decode_world_texture(
          {archive.payload(record), archive.metadata(record)}, allocator);
      const auto texture = renderer.upload_texture(decoded);
      images.at(image_count++) = {id, texture,
                                  static_cast<float>(decoded.width),
                                  static_cast<float>(decoded.height)};
    }
  }
  void load_icons(std::span<const std::uint8_t> bytes) {
    const resources::detail::Reader reader{bytes};
    constexpr unsigned kOriginalIconCount = 149;
    constexpr std::size_t kOffsetTable = 8, kWordBytes = 4;
    if (reader.u32(0) != kOriginalIconCount) {
      throw std::runtime_error("Unsupported original controller icon table");
    }
    reader.range(kOffsetTable, (kOriginalIconCount + 1) * kWordBytes);
    for (const auto icon : kControllerIcons) {
      if (image_count == images.size()) {
        throw std::runtime_error("Loading icon texture capacity exceeded");
      }
      const auto begin = reader.u32(kOffsetTable + icon * kWordBytes);
      const auto end = reader.u32(kOffsetTable + (icon + 1) * kWordBytes);
      if (begin < kOffsetTable + (kOriginalIconCount + 1) * kWordBytes ||
          end < begin) {
        throw std::runtime_error("Invalid controller icon payload range");
      }
      reader.range(begin, end - begin);
      const auto decoded =
          formats::decode_png(bytes.subspan(begin, end - begin), {}, allocator);
      const auto texture =
          renderer.upload_texture(decoded.width, decoded.height, decoded.rgba);
      images.at(image_count++) = {kIconIdentityBase + icon, texture,
                                  static_cast<float>(decoded.width),
                                  static_cast<float>(decoded.height)};
    }
  }
  float icon(bool magic, float x, float y, float opacity) {
    constexpr float kSourceFontSize = 32;
    constexpr float kPadScale = .933333993F, kThird = .333333343F;
    constexpr float kSpacingBase = 64, kSpacingMultiplier = .8F;
    constexpr float kYOffset = -7, kHalf = .5F;
    constexpr float kMagicAdvance = 119.466674F, kButtonAdvance = 44.8F;
    const auto font_scale = kTextSize / kSourceFontSize;
    const auto scale = font_scale * kPadScale * kThird;
    const auto pivot_spacing =
        font_scale * kPadScale * kSpacingBase * kSpacingMultiplier;
    const auto count = magic ? std::size_t{3} : std::size_t{1};
    for (std::size_t component = 0; component < count; ++component) {
      const auto& icon_image = image(
          kIconIdentityBase +
          kControllerIcons.at(magic ? component : kControllerIcons.size() - 1));
      append({icon_image.texture,
              {x + static_cast<float>(component) * pivot_spacing * (1 - scale),
               y + kYOffset - (1 - font_scale) * kHalf * kYOffset,
               icon_image.width * scale, icon_image.height * scale},
              {0, 0, 1, 1},
              {1, 1, 1, opacity}});
    }
    return font_scale * (magic ? kMagicAdvance : kButtonAdvance);
  }
  void text(std::string_view value, float opacity) {
    if (!font) throw std::logic_error("Loading font metrics are missing");
    const auto& metrics = font.value();
    const auto& atlas = image(0);
    const auto scale = kTextSize / static_cast<float>(metrics.pixel_size());
    auto cursor = kTextX;
    auto line_y = kTextY;
    auto tint = kTextColor;
    tint.alpha = opacity;
    std::size_t index = 0;
    while (index < value.size()) {
      const auto codepoint = static_cast<unsigned char>(value[index++]);
      if (codepoint == '\n' || codepoint == '\r') {
        while (index < value.size() &&
               (value[index] == '\r' || value[index] == '\n')) {
          ++index;
        }
        constexpr float kLineGap = 4;
        cursor = kTextX;
        line_y += kTextSize + kLineGap;
        continue;
      }
      if (codepoint == '<') {
        const auto end = value.find('>', index);
        if (end == std::string_view::npos) {
          throw std::runtime_error("Unterminated loading text markup");
        }
        const auto tag = value.substr(index, end - index);
        index = end + 1;
        constexpr float kHalfColor = 128 / kColorRange;
        if (tag == "col end") {
          tint = kTextColor;
          tint.alpha = opacity;
        } else if (tag == "col blue") {
          tint.red *= kHalfColor;
          tint.green *= kHalfColor;
        } else if (tag == "col yellow") {
          tint.blue *= kHalfColor;
        } else if (tag == "kico mgc" || tag == "ico key_b") {
          cursor += icon(tag == "kico mgc", cursor, line_y, opacity);
        } else {
          throw std::runtime_error("Unsupported original loading text markup");
        }
        continue;
      }
      constexpr unsigned kAsciiLimit = 128;
      if (codepoint >= kAsciiLimit) {
        throw std::runtime_error("Loading text needs Unicode decoding");
      }
      const auto glyph = metrics.glyph(static_cast<char16_t>(codepoint));
      if (!glyph) {
        throw std::runtime_error("Original loading font omits a glyph");
      }
      if (!glyph->blank) {
        const auto width = static_cast<float>(glyph->right - glyph->left);
        const auto height = static_cast<float>(glyph->bottom - glyph->top);
        append({atlas.texture,
                {cursor + static_cast<float>(glyph->bearing_left) * scale,
                 line_y + static_cast<float>(metrics.baseline()) * scale +
                     static_cast<float>(glyph->bearing_top) * scale,
                 width * scale, height * scale},
                {static_cast<float>(glyph->left) / atlas.width,
                 static_cast<float>(glyph->top) / atlas.height,
                 width / atlas.width, height / atlas.height},
                tint,
                channel(glyph->channel)});
      }
      cursor += static_cast<float>(glyph->advance) * scale;
    }
  }
};
StartupLoadingSurface::StartupLoadingSurface(
    resources::ResourceService& resources, render::SpriteOutput& renderer)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         renderer)) {}
void StartupLoadingSurface::prepare(
    std::string_view selected_background,
    resources::ArchiveBytes localized_text,
    std::span<const std::uint8_t> controller_icons) {
  auto& state = *state_;
  state.end();
  constexpr std::string_view kPrefix = "sprite.loading";
  constexpr std::size_t kDigits = 2;
  constexpr unsigned kDecimal = 10;
  if (!selected_background.starts_with(kPrefix) ||
      selected_background.size() != kPrefix.size() + kDigits) {
    throw std::invalid_argument(
        "Original loading background selection is missing");
  }
  const auto digits = selected_background.substr(kPrefix.size());
  if (!std::ranges::all_of(
          digits, [](char value) { return value >= '0' && value <= '9'; })) {
    throw std::invalid_argument("Invalid loading background number");
  }
  const auto selected = static_cast<unsigned>(digits[0] - '0') * kDecimal +
                        static_cast<unsigned>(digits[1] - '0');
  if (selected == 0 || selected > kBackgroundPositions.size()) {
    throw std::invalid_argument(
        "Loading background number exceeds original choices");
  }
  state.background_position = kBackgroundPositions.at(selected - 1);
  try {
    state.text_bytes = std::move(localized_text);
    const resources::detail::Reader reader{state.text_bytes};
    constexpr std::size_t kWordBytes = 4, kLanguages = 6, kEnglish = 1;
    const auto rows = reader.u32(0);
    reader.range(kWordBytes,
                 static_cast<std::uint64_t>(rows) * kLanguages * kWordBytes);
    for (std::size_t item = 0;
         item < std::min<std::size_t>(rows, state.labels.size()); ++item) {
      const auto offset =
          reader.u32(kWordBytes + (item * kLanguages + kEnglish) * kWordBytes);
      if (offset == 0) continue;
      const auto pool =
          kWordBytes + static_cast<std::size_t>(rows) * kLanguages * kWordBytes;
      if (offset < pool || offset >= state.text_bytes.size()) {
        throw std::runtime_error("Loading text offset escapes its string pool");
      }
      state.labels.at(item) =
          reader.text_view({pool, state.text_bytes.size() - pool},
                           static_cast<std::uint32_t>(offset - pool));
    }
    const std::array<std::string_view, 3> groups{
        selected_background, "sprite.system", "sprite.font"};
    for (const auto group : groups) state.resources.request(group, kLoadingTag);
    while (true) {
      const auto result = state.resources.pump(kLoadingTag);
      if (result.state == resources::LoadState::failed) {
        throw std::runtime_error(result.diagnostic);
      }
      if (result.state == resources::LoadState::ready) break;
    }
    for (std::size_t index = 0; index < groups.size(); ++index) {
      auto lease = state.resources.acquire(groups.at(index));
      if (!lease) {
        throw std::runtime_error("Loading resource was not published");
      }
      state.leases.at(index) = std::move(lease);
      state.load_images(index, groups.at(index) == "sprite.font");
    }
    (void)state.image(kBackgroundId);
    (void)state.image(kSmallArtId);
    (void)state.image(kLargeArtId);
    (void)state.image(0);
    state.load_icons(controller_icons);
    state.prepared = true;
  } catch (...) {
    state.end();
    throw;
  }
}
float StartupLoadingSurface::show(unsigned item, double elapsed_seconds) {
  auto& state = *state_;
  if (!state.prepared || item >= state.labels.size() ||
      state.labels.at(item).empty() || !std::isfinite(elapsed_seconds) ||
      elapsed_seconds < 0) {
    throw std::invalid_argument(
        "Loading presentation is not ready for the requested item/time");
  }
  state.command_count = 0;
  // Text arrives after the original sliding motif has entered. Native ordering
  // draws its text first, then the six repeated sprite groups.
  const auto elapsed = static_cast<float>(elapsed_seconds);
  const auto& background = state.image(kBackgroundId);
  auto rectangle = state.background_position;
  rectangle.width = background.width;
  rectangle.height = background.height;
  state.append({background.texture, rectangle, {0, 0, 1, 1}, {}});
  constexpr float kTextBegin = .7F, kTextDuration = .3F;
  state.text(state.labels.at(item),
             std::clamp((elapsed - kTextBegin) / kTextDuration, 0.0F, 1.0F));
  for (const auto& group : kArtGroups) {
    const auto& art = state.image(group.image);
    const auto direction_x = std::sin(group.direction),
               direction_y = std::cos(group.direction);
    const auto offset = slide(group, elapsed);
    for (unsigned index = 0; index < group.count; ++index) {
      const auto distance = offset + static_cast<float>(index) * group.spacing;
      state.append({art.texture,
                    {group.x + direction_x * distance,
                     group.y + direction_y * distance, art.width, art.height},
                    {0, 0, 1, 1},
                    {},
                    render::TextureChannel::rgba,
                    group.rotation,
                    art.width / 2,
                    art.height / 2});
    }
  }
  state.renderer.set_sprites(
      std::span(state.commands).first(state.command_count));
  return std::min(elapsed / kFadeSeconds, 1.0F);
}
StartupLoadingSurface::~StartupLoadingSurface() = default;
void StartupLoadingSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
