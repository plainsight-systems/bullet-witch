// Resolve native HUD art commands through original resources. This main-thread
// owner retains service/output borrows and exclusive leases/texture identities;
// it never borrows mutable gameplay state. Algorithm: request/pump/acquire ->
// validate sprite IDs and decode selected images -> retain textures -> resolve
// fixed commands and optional prepared glyphs -> submit once -> release.
// Errors unwind partial setup. Frame work allocates nothing; GPU images stay
// resident. Core R.4 (#rr-ref), F.16 (#rf-in), SL.con.3 (#rsl-bounds),
// performance MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) and
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md), both phases consulted.
#include "gameplay_hud_surface.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <stdexcept>

#include "bw/formats/world_texture.hpp"
#include "sprite_text.hpp"

namespace bw::game {
namespace {
constexpr resources::ResourceTag kHudTag = 18;
constexpr std::array<std::string_view, 3> kGroups{
    "sprite.cockpit", "sprite.magicring", "sprite.font"};
constexpr std::array<unsigned, 21> kCockpitImages{
    6,  9,  14, 17,  18,  19,  20,  21,  37,  38, 39,
    40, 41, 42, 425, 426, 427, 428, 429, 430, 434};
constexpr std::array<unsigned, 26> kRingImages{
    101, 102, 106, 107, 111, 112, 113, 114, 118, 119, 120, 121, 123,
    124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 435, 436};
constexpr std::size_t kImageCapacity =
    kCockpitImages.size() + kRingImages.size() + 1;
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
unsigned image_id(std::span<const std::uint8_t> metadata, std::size_t index) {
  constexpr std::size_t kIdBytes = 2;
  constexpr unsigned kByteBits = 8;
  if (index >= metadata.size() / kIdBytes) {
    throw std::runtime_error("HUD sprite identity missing");
  }
  const auto offset = index * kIdBytes;
  return static_cast<unsigned>(metadata[offset]) |
         (static_cast<unsigned>(metadata[offset + 1]) << kByteBits);
}
}  // namespace
struct GameplayHudSurface::State {
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
  std::array<render::Sprite, render::kMaximumSprites> commands{};
  bool ready{};
  State(resources::ResourceService& service, render::SpriteOutput& renderer)
      : resources(service), output(renderer), allocator(storage_for(service)) {}
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    while (image_count != 0) {
      output.release_texture(images.at(--image_count).texture);
    }
    font.reset();
    for (auto& lease : leases) lease.reset();
    resources.release(kHudTag);
    ready = false;
  }
  const Image& image(unsigned id) const {
    for (const auto& candidate : std::span(images).first(image_count)) {
      if (candidate.id == id) return candidate;
    }
    throw std::runtime_error("Original HUD sprite is not resident");
  }
  void prepare() {
    end();
    for (const auto group : kGroups) resources.request(group, kHudTag);
    for (;;) {
      const auto report = resources.pump(kHudTag);
      if (report.state == resources::LoadState::failed) {
        throw std::runtime_error(report.diagnostic);
      }
      if (report.state == resources::LoadState::ready) break;
    }
    for (std::size_t group = 0; group < kGroups.size(); ++group) {
      auto& slot = leases.at(group);
      slot = resources.acquire(kGroups.at(group));
      if (!slot) {
        throw std::runtime_error("HUD resource publication missing");
      }
      const auto& lease = slot.value();
      const bool is_font = group == kGroups.size() - 1;
      if (is_font) font = formats::BitmapFont::parse(lease.bytes(), allocator);
      const auto& archive = lease.archive();
      std::size_t image_index = 0;
      for (std::size_t record = 0; record < archive.records().size();
           ++record) {
        if (archive.records()[record].type != "DDS ") continue;
        const auto id = is_font ? 0 : image_id(lease.bytes(), image_index);
        ++image_index;
        if (group == 0 &&
            std::ranges::find(kCockpitImages, id) == kCockpitImages.end()) {
          continue;
        }
        if (group == 1 &&
            std::ranges::find(kRingImages, id) == kRingImages.end()) {
          continue;
        }
        if (image_count == images.size()) {
          throw std::length_error("HUD texture capacity exceeded");
        }
        const auto decoded = formats::decode_world_texture(
            {archive.payload(record), archive.metadata(record)}, allocator);
        if (is_font && (!font || image_index != 1 ||
                        decoded.width != font->atlas_width() ||
                        decoded.height != font->atlas_height())) {
          throw std::runtime_error("HUD font atlas disagrees with metrics");
        }
        const auto texture = output.upload_texture(decoded);
        images.at(image_count++) = {id, texture,
                                    static_cast<float>(decoded.width),
                                    static_cast<float>(decoded.height)};
      }
    }
    ready = true;
  }
};
GameplayHudSurface::GameplayHudSurface(resources::ResourceService& resources,
                                       render::SpriteOutput& output)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         output)) {}
GameplayHudSurface::~GameplayHudSurface() = default;
void GameplayHudSurface::prepare() {
  try {
    state_->prepare();
  } catch (...) {
    state_->end();
    throw;
  }
}
bool GameplayHudSurface::prepared() const noexcept { return state_->ready; }
void GameplayHudSurface::end() noexcept { state_->end(); }
void GameplayHudSurface::hide() noexcept { state_->output.clear_sprites(); }
void GameplayHudSurface::show(const GameplayHud& hud,
                              std::string_view diagnostic) {
  auto& state = *state_;
  if (!state.ready) throw std::logic_error("HUD surface is not prepared");
  std::size_t count = 0;
  for (const auto& command : hud.commands()) {
    if (count == state.commands.size()) {
      throw std::length_error("HUD submission capacity exceeded");
    }
    const auto& image = state.image(command.image);
    auto position = command.destination;
    if (position.width == 0) position.width = image.width;
    if (position.height == 0) position.height = image.height;
    constexpr float kHalf = .5F;
    if (command.centered) {
      position.x -= position.width * kHalf;
      position.y -= position.height * kHalf;
    }
    const float pivot_x = command.canvas_pivot
                              ? render::kCanvasWidth * kHalf
                              : position.x + position.width * kHalf;
    const float pivot_y = command.canvas_pivot
                              ? render::kCanvasHeight * kHalf
                              : position.y + position.height * kHalf;
    position.x = pivot_x + (position.x - pivot_x) * command.scale;
    position.y = pivot_y + (position.y - pivot_y) * command.scale;
    position.width *= command.scale;
    position.height *= command.scale;
    state.commands.at(count++) = {
        image.texture,
        position,
        command.uv,
        command.color,
        render::TextureChannel::rgba,
        command.rotation_radians,
        command.top_left_pivot ? 0 : pivot_x - position.x,
        command.top_left_pivot ? 0 : pivot_y - position.y,
        command.additive ? render::SpriteBlend::additive
                         : render::SpriteBlend::alpha};
  }
  constexpr std::size_t kDiagnosticLimit = 120;
  if (!diagnostic.empty()) {
    if (!state.font) throw std::logic_error("HUD diagnostic font unavailable");
    constexpr SpriteTextPosition kPosition{20, 680, 18};
    count +=
        append_sprite_text(*state.font, state.image(0).texture,
                           diagnostic.substr(0, kDiagnosticLimit), kPosition,
                           {}, std::span(state.commands).subspan(count));
  }
  state.output.set_sprites(std::span(state.commands).first(count));
}
}  // namespace bw::game
