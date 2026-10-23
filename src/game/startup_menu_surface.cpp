// Implementation of the menu asset and draw contract in
// startup_menu_surface.hpp. Resource leases retain archive bytes through
// construction. Textures are kept resident until end; temporary PNG decode
// storage is released after each upload. Fixed arrays bound images and glyph
// commands. Original main-menu art/positions are recovered; child page list
// geometry remains explicitly reconstructed. Implementation MCP SL.con.3
// (#rsl-bounds) checks metadata/command capacities; GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) uploads only at activation.
#include "startup_menu_surface.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>

#include "bw/formats/bitmap_font.hpp"
#include "bw/formats/image.hpp"
namespace bw::game {
namespace {
struct TextPosition {
  float x{}, y{}, size{};
};
constexpr unsigned kMainBackground = 334, kStageBackground = 306,
                   kOptionBackground = 209;
constexpr unsigned kAsciiLimit = 128;
constexpr float kPointerLeft = 35, kPointerTop = 15, kPointerSize = 28;
constexpr float kRowLeft = 345, kRowValue = 865, kRowSize = 25,
                kSelectionLeft = 310;
constexpr std::size_t kVisibleRows = 14;
constexpr TextPosition kHeading{310, 70, 32}, kMessage{310, 660, 18};
constexpr render::Color kTextColor{0.08F, 0.06F, 0.05F, 1};
constexpr resources::ResourceTag kMenuTag = 12;
constexpr std::size_t kCostumeCount = 6;
constexpr unsigned kCostumeImageBase = 10000;
constexpr unsigned kCustomBackground = 46, kCustomPanel = 48,
                   kDefaultCostumeImage = 445;
constexpr render::Rect kPreviewPosition{634, 64, 0, 0},
    kPreviewPanel{598, 28, 0, 0};
constexpr TextPosition kCostumeHeading{140, 90, 28};
constexpr float kCostumeRowX = 155, kCostumeRowY = 185, kCostumeRowStep = 62;
constexpr std::array<std::string_view, 16> kGroups{
    "sprite.titlemenu",     "sprite.font",          "sprite.stageselect",
    "sprite.option",        "sprite.optiontit",     "sprite.custom",
    "sprite.cos01",         "sprite.cos02",         "sprite.cos03",
    "sprite.cos04",         "sprite.cos05",         "sprite.text.costxt01",
    "sprite.text.costxt02", "sprite.text.costxt03", "sprite.text.costxt04",
    "sprite.text.costxt05"};
// The costume label records use full-width Latin UTF-8 text. This narrow
// compatibility reader selects the unique English costume title from the
// bounded STMANAGE pool; it is not a general language-table implementation.
std::string costume_label(std::span<const std::uint8_t> bytes) {
  constexpr std::size_t pool_field = 28, word_bytes = 4;
  constexpr unsigned byte_shift = 8;
  constexpr std::string_view signature = "STMANAGE";
  if (bytes.size() < pool_field + word_bytes ||
      !std::equal(signature.begin(), signature.end(), bytes.begin())) {
    throw std::runtime_error("Invalid costume text resource");
  }
  std::uint32_t pool = 0;
  for (std::size_t i = 0; i < word_bytes; ++i) {
    pool |= static_cast<std::uint32_t>(bytes[pool_field + i])
            << (i * byte_shift);
  }
  if (pool >= bytes.size()) {
    throw std::runtime_error("Costume text pool exceeds resource");
  }
  std::string result;
  for (std::size_t offset = pool; offset < bytes.size();) {
    std::string candidate;
    while (offset < bytes.size() && bytes[offset] != 0) {
      const auto first = bytes[offset++];
      if (first < kAsciiLimit) {
        candidate.push_back(static_cast<char>(first));
        continue;
      }
      constexpr std::uint8_t lead = 0xef, second_low = 0xbc, second_high = 0xbd,
                             continuation = 0x80;
      constexpr std::uint8_t continuation_mask = 0xc0, payload_mask = 0x3f;
      constexpr unsigned six_bits = 6, full_width_start = 0xff01,
                         full_width_end = 0xff5e, full_width_delta = 0xfee0;
      if (first == lead && bytes.size() - offset >= 2 &&
          (bytes[offset] == second_low || bytes[offset] == second_high) &&
          (bytes[offset + 1] & continuation_mask) == continuation) {
        const auto codepoint =
            0xf000U |
            (static_cast<unsigned>(bytes[offset] & payload_mask) << six_bits) |
            (bytes[offset + 1] & payload_mask);
        if (codepoint >= full_width_start && codepoint <= full_width_end) {
          candidate.push_back(static_cast<char>(codepoint - full_width_delta));
        } else {
          candidate.push_back('?');
        }
        offset += 2;
      } else {
        candidate.push_back('?');
      }
    }
    if (offset == bytes.size()) {
      throw std::runtime_error("Unterminated costume label");
    }
    ++offset;
    if (candidate.starts_with("Special Costume ")) {
      if (!result.empty() && result != candidate) {
        throw std::runtime_error("Ambiguous English costume label");
      }
      result = std::move(candidate);
    }
  }
  if (result.empty()) throw std::runtime_error("Missing English costume label");
  return result;
}
constexpr std::array<unsigned, 6> kMainImages{354, 353, 357, 356, 358, 355};
constexpr std::array<render::Rect, 6> kMainPositions{{{502, 135, 0, 0},
                                                      {568, 204, 0, 0},
                                                      {507, 267, 0, 0},
                                                      {483, 334, 0, 0},
                                                      {475, 399, 0, 0},
                                                      {457, 457, 0, 0}}};
constexpr std::array<unsigned, 4> kOrnaments{349, 350, 351, 352};
constexpr std::array<render::Rect, 4> kOrnamentPositions{
    {{143, 264, 0, 0}, {168, 96, 0, 0}, {0, 0, 0, 0}, {308, 32, 0, 0}}};
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
  throw std::runtime_error("Invalid font channel");
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
struct StartupMenuSurface::State {
  struct Image {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  resources::ResourceService& resources;
  render::SpriteOutput& renderer;
  std::array<std::optional<resources::ResourceLease>, kGroups.size()> leases;
  std::array<Image, render::kMaximumTextures> images{};
  std::size_t image_count{};
  std::optional<formats::BitmapFont> font;
  std::array<std::string, kCostumeCount> costume_labels;
  render::TextureHandle font_texture;
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  std::size_t count{};
  bool requested{}, constructed{};
  State(resources::ResourceService& service, render::SpriteOutput& output)
      : resources(service), renderer(output) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
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
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
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
      constexpr std::size_t first_costume_group = 6, first_label_group = 11;
      if (is_label) {
        costume_labels.at(group - first_label_group + 1) =
            costume_label(lease.bytes());
        continue;
      }
      costume_labels.at(0) = "Original";
      if (is_font) font.emplace(formats::BitmapFont::parse(lease.bytes()));
      const auto& archive = lease.archive();
      std::size_t image_index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        if (image_count == images.size()) {
          throw std::runtime_error("Menu texture capacity exceeded");
        }
        const auto id =
            is_font ? 0
                    : (is_costume ? kCostumeImageBase +
                                        static_cast<unsigned>(
                                            group - first_costume_group + 1)
                                  : sprite_id(lease.bytes(), image_index));
        formats::Image decoded;
        try {
          decoded = formats::decode_png(archive.payload(record));
        } catch (const std::exception& error) {
          throw std::runtime_error(std::string(kGroups.at(group)) + "/" +
                                   archive.records()[record].name + ": " +
                                   error.what());
        }
        if (is_font && !font) {
          throw std::logic_error("Missing font metrics");
        }
        if (is_font && font &&
            (image_index != 0 || decoded.width != font.value().atlas_width() ||
             decoded.height != font.value().atlas_height())) {
          throw std::runtime_error("Menu font atlas disagrees with metrics");
        }
        const auto texture = renderer.upload_texture(
            decoded.width, decoded.height, decoded.rgba);
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
    constructed = true;
  }
  void append(render::Sprite command) {
    if (count == commands.size()) {
      throw std::runtime_error("Menu draw capacity exceeded");
    }
    commands.at(count++) = command;
  }
  void image(unsigned id, render::Rect position, render::Color tint = {}) {
    for (std::size_t index = 0; index < image_count; ++index) {
      const auto& entry = images.at(index);
      if (entry.id != id) continue;
      if (position.width == 0) position.width = entry.width;
      if (position.height == 0) position.height = entry.height;
      append({entry.texture, position, {0, 0, 1, 1}, tint});
      return;
    }
    throw std::runtime_error("Missing menu image identity");
  }
  void text(std::string_view value, TextPosition position, render::Color tint) {
    if (!font) {
      throw std::logic_error("Missing menu font");
    }
    const auto& metrics = *font;
    const auto scale = position.size / static_cast<float>(metrics.pixel_size());
    const auto atlas_width = static_cast<float>(metrics.atlas_width());
    const auto atlas_height = static_cast<float>(metrics.atlas_height());
    auto cursor = position.x;
    auto baseline = position.y + static_cast<float>(metrics.baseline()) * scale;
    for (const char character : value) {
      const auto codepoint = static_cast<unsigned char>(character);
      if (codepoint == '\n') {
        cursor = position.x;
        baseline += position.size;
        continue;
      }
      if (codepoint >= kAsciiLimit) {
        throw std::runtime_error("Menu label needs Unicode decoding");
      }
      const auto glyph = metrics.glyph(codepoint);
      if (!glyph) throw std::runtime_error("Missing original menu font glyph");
      if (!glyph->blank) {
        const auto width = static_cast<float>(glyph->right - glyph->left);
        const auto height = static_cast<float>(glyph->bottom - glyph->top);
        append({font_texture,
                {cursor + static_cast<float>(glyph->bearing_left) * scale,
                 baseline + static_cast<float>(glyph->bearing_top) * scale,
                 width * scale, height * scale},
                {static_cast<float>(glyph->left) / atlas_width,
                 static_cast<float>(glyph->top) / atlas_height,
                 width / atlas_width, height / atlas_height},
                tint,
                channel(glyph->channel)});
      }
      cursor += static_cast<float>(glyph->advance) * scale;
    }
  }
};
StartupMenuSurface::StartupMenuSurface(resources::ResourceService& resources,
                                       render::SpriteOutput& renderer)
    : state_(std::make_unique<State>(resources, renderer)) {}
StartupMenuSurface::~StartupMenuSurface() = default;
void StartupMenuSurface::begin() {
  state_->end();
  state_->requested = true;
  for (const auto group : kGroups) {
    try {
      state_->resources.request(group, kMenuTag);
    } catch (const std::exception& error) {
      throw std::runtime_error(std::string(group) + ": " + error.what());
    }
  }
}
bool StartupMenuSurface::ready() {
  auto& state = *state_;
  if (!state.requested) throw std::logic_error("Menu assets not requested");
  if (state.constructed) return true;
  const auto report = state.resources.pump(kMenuTag);
  if (report.state == resources::LoadState::failed) {
    throw std::runtime_error(report.diagnostic);
  }
  if (report.state != resources::LoadState::ready) return false;
  state.construct();
  return false;
}
void StartupMenuSurface::show(const MenuPresentation& presentation) {
  auto& state = *state_;
  if (!state.constructed || !state.font) {
    throw std::logic_error("Menu cannot draw before construction");
  }
  if (presentation.count > presentation.rows.size() ||
      (presentation.count != 0 &&
       presentation.selected >= presentation.count)) {
    throw std::invalid_argument("Invalid menu row selection");
  }
  state.count = 0;
  if (presentation.page == MenuPage::main) {
    if (presentation.count != kMainImages.size()) {
      throw std::invalid_argument("Original main menu has six entries");
    }
    state.image(kMainBackground, {0, 0, 0, 0});
    for (std::size_t i = 0; i < kOrnaments.size(); ++i) {
      state.image(kOrnaments.at(i), kOrnamentPositions.at(i));
    }
    for (std::size_t i = 0; i < kMainImages.size(); ++i) {
      const float opacity = presentation.rows.at(i).enabled
                                ? (i == presentation.selected ? 1.0F : 0.65F)
                                : 0.3F;
      state.image(kMainImages.at(i), kMainPositions.at(i), {1, 1, 1, opacity});
      if (i == presentation.selected) {
        state.text(">",
                   {kMainPositions.at(i).x - kPointerLeft,
                    kMainPositions.at(i).y + kPointerTop, kPointerSize},
                   {0, 0, 0, 1});
      }
    }
  } else if (presentation.page == MenuPage::costume &&
             presentation.count != 0) {
    state.image(kCustomBackground, {0, 0, 0, 0});
    state.image(kCustomPanel, kPreviewPanel);
    const auto selected_id =
        presentation.rows.at(presentation.selected).compatibility_id;
    if (selected_id >= kCostumeCount) {
      throw std::runtime_error("Unsupported costume preview identity");
    }
    state.image(selected_id == 0 ? kDefaultCostumeImage
                                 : kCostumeImageBase + selected_id,
                kPreviewPosition);
    state.text(presentation.title, kCostumeHeading, kTextColor);
    for (std::size_t i = 0; i < presentation.count; ++i) {
      const auto& row = presentation.rows.at(i);
      if (row.compatibility_id >= kCostumeCount) {
        throw std::runtime_error("Unsupported costume label identity");
      }
      const auto y = kCostumeRowY + static_cast<float>(i) * kCostumeRowStep;
      constexpr render::Color active{0.6F, 0.02F, 0.02F, 1};
      const auto color = i == presentation.selected ? active : kTextColor;
      state.text(state.costume_labels.at(row.compatibility_id),
                 {kCostumeRowX, y, kRowSize}, color);
    }
  } else {
    const bool stage = presentation.page == MenuPage::stage ||
                       presentation.page == MenuPage::extra ||
                       presentation.page == MenuPage::costume;
    state.image(stage ? kStageBackground : kOptionBackground, {0, 0, 0, 0});
    constexpr render::Color normal{0.08F, 0.06F, 0.05F, 1};
    constexpr render::Color selected{0.6F, 0.02F, 0.02F, 1};
    constexpr render::Color disabled{0.42F, 0.4F, 0.38F, 1};
    state.text(presentation.title, kHeading, normal);
    constexpr float row_top = 155, row_step = 32;
    const auto first = presentation.selected >= kVisibleRows
                           ? presentation.selected - kVisibleRows + 1
                           : 0;
    const auto last = std::min(presentation.count, first + kVisibleRows);
    for (std::size_t i = first; i < last; ++i) {
      const auto& row = presentation.rows.at(i);
      const auto color = !row.enabled
                             ? disabled
                             : (i == presentation.selected ? selected : normal);
      const auto y = row_top + static_cast<float>(i - first) * row_step;
      if (i == presentation.selected) {
        state.text(">", {kSelectionLeft, y, kRowSize}, color);
      }
      state.text(row.label, {kRowLeft, y, kRowSize}, color);
      state.text(row.value, {kRowValue, y, kRowSize}, color);
    }
  }
  if (!presentation.message.empty()) {
    state.text(presentation.message, kMessage, kTextColor);
  }
  state.renderer.set_sprites(std::span(state.commands).first(state.count));
}
void StartupMenuSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
