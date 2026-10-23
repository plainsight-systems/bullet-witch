// Memory migration: native owners propagate the ResourceService CPU domain to
// final pixel/glyph storage and retained presentation state/strings. Owning
// allocators retain the domain through copies and partial-construction cleanup;
// defaults are legacy parser/test compatibility, never exhaustion fallback.
// PNG writes directly to its final owned destination. Fixed draw storage adds
// no per-frame allocations; native texture lifetime stays with SpriteOutput.
// Design R.1 (#rr-raii), implementation E.6 (#re-raii), both-phase MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) and MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) retain owners and
// fixed presentation capacity. Pinned source/external libpng exclusions are
// recorded in research/engine/subsystems/memory-image-presentation.md. Title
// asset construction and original sprite/font submission. See header and
// research/engine/subsystems/title-assets.md for recovered data and
// coordinates. Leases retain encoded sibling records; PNG/FONT decoders
// validate input; persistent texture handles retire before their renderer. The
// copied per-frame command span contains three original sprites and bounded
// version glyphs. Design R.20 (#rr-owner), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) retain archive/device
// residency; implementation E.6 (#re-raii), COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) clean up partial
// construction without duplicate image owners. No media clock work lives here.
#include "startup_title_surface.hpp"

#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "bw/formats/bitmap_font.hpp"
#include "bw/formats/image.hpp"

namespace bw::game {
namespace {
memory::Allocator<std::byte> storage_for(
    const resources::ResourceService& resources) {
  const auto domain = resources.memory_domain();
  return domain ? memory::Allocator<std::byte>(domain)
                : memory::Allocator<std::byte>{};
}
constexpr resources::ResourceTag kTitleTag = 10;
constexpr resources::ResourceTag kLoadingTag = 11;
constexpr std::array<unsigned, 3> kDrawIds{424, 423, 452};
constexpr std::array<render::Rect, 3> kSpritePositions{
    render::Rect{350, 505, 0, 0}, render::Rect{350, 505, 0, 0},
    render::Rect{412, 632, 0, 0}};
constexpr std::u16string_view kVersion = u"Version 1.0.5";
constexpr std::size_t kDrawCapacity = kDrawIds.size() + kVersion.size();
unsigned sprite_id(std::span<const std::uint8_t> bytes, std::size_t index) {
  constexpr std::size_t word_bytes = 2;
  constexpr unsigned byte_shift = 8;
  if (index >= bytes.size() / word_bytes) {
    throw std::runtime_error("Sprite metadata does not cover image records");
  }
  const auto offset = index * word_bytes;
  return static_cast<unsigned>(bytes[offset]) |
         (static_cast<unsigned>(bytes[offset + 1]) << byte_shift);
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
  throw std::runtime_error("Unsupported font coverage channel");
}
}  // namespace
struct StartupTitleSurface::State {
  struct SpriteImage {
    unsigned id{};
    render::TextureHandle texture;
    float width{}, height{};
  };
  memory::Allocator<std::byte> allocator;
  resources::ResourceService& resources;
  render::SpriteOutput& renderer;
  std::function<unsigned()> choose_loading;
  std::optional<resources::ResourceLease> title, font, loading;
  std::optional<formats::BitmapFont> metrics;
  std::array<render::TextureHandle, render::kMaximumTextures> textures{};
  std::size_t texture_count{};
  std::array<SpriteImage, 3> images{};
  render::TextureHandle font_texture;
  std::array<render::Sprite, kDrawCapacity> commands{};
  enum class Phase : std::uint8_t { idle, title, loading, ready };
  Phase phase{Phase::idle};
  memory::String loading_name;
  State(resources::ResourceService& resource_service,
        render::SpriteOutput& output, std::function<unsigned()> selector)
      : allocator(storage_for(resource_service)),
        resources(resource_service),
        renderer(output),
        choose_loading(std::move(selector)),
        loading_name(allocator) {
    if (!choose_loading) {
      throw std::invalid_argument("Loading selector is required");
    }
  }
  ~State() { end(); }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
  void end() noexcept {
    renderer.clear_sprites();
    while (texture_count > 0) {
      renderer.release_texture(textures.at(--texture_count));
    }
    title.reset();
    font.reset();
    loading.reset();
    metrics.reset();
    resources.release(kTitleTag);
    resources.release(kLoadingTag);
    phase = Phase::idle;
  }
  resources::ResourceLease acquire(std::string_view name) {
    auto lease = resources.acquire(name);
    if (!lease) {
      throw std::runtime_error("Missing title resource: " + std::string(name));
    }
    return std::move(*lease);
  }
  bool pump(resources::ResourceTag tag) {
    const auto report = resources.pump(tag);
    if (report.state == resources::LoadState::failed) {
      throw std::runtime_error(report.diagnostic);
    }
    return report.state == resources::LoadState::ready;
  }
  SpriteImage upload(const resources::ResourceArchive& archive,
                     std::size_t index, unsigned id) {
    if (texture_count == textures.size()) {
      throw std::runtime_error("Title texture capacity exceeded");
    }
    const auto decoded =
        formats::decode_png(archive.payload(index), {}, allocator);
    const auto handle =
        renderer.upload_texture(decoded.width, decoded.height, decoded.rgba);
    textures.at(texture_count++) = handle;
    return {id, handle, static_cast<float>(decoded.width),
            static_cast<float>(decoded.height)};
  }
  void construct_title() {
    const auto& title_lease = title.emplace(acquire("sprite.title"));
    const auto& archive = title_lease.archive();
    const auto identities = title_lease.bytes();
    std::size_t image_index = 0;
    for (std::size_t index = 0; index < archive.records().size(); ++index) {
      if (archive.records()[index].type != "DDS ") {
        continue;
      }
      if (image_index >= images.size()) {
        throw std::runtime_error("Unexpected title image count");
      }
      images.at(image_index) =
          upload(archive, index, sprite_id(identities, image_index));
      ++image_index;
    }
    if (image_index != images.size()) {
      throw std::runtime_error("Missing title images");
    }
    font = acquire("sprite.font");
    const auto& font_metrics =
        metrics.emplace(formats::BitmapFont::parse(font->bytes(), allocator));
    const auto expected_width = static_cast<float>(font_metrics.atlas_width());
    const auto expected_height =
        static_cast<float>(font_metrics.atlas_height());
    bool found = false;
    const auto& font_archive = font->archive();
    for (std::size_t index = 0; index < font_archive.records().size();
         ++index) {
      if (font_archive.records()[index].type != "DDS ") {
        continue;
      }
      if (found) {
        throw std::runtime_error("Unexpected multiple font atlases");
      }
      const auto image = upload(font_archive, index, 0);
      if (image.width != expected_width || image.height != expected_height) {
        throw std::runtime_error("Font atlas dimensions disagree with metrics");
      }
      font_texture = image.texture;
      found = true;
    }
    if (!found) {
      throw std::runtime_error("Missing original font atlas");
    }
  }
  void construct_loading() {
    const auto& loading_lease = loading.emplace(acquire(loading_name));
    const auto& archive = loading_lease.archive();
    const auto identities = loading_lease.bytes();
    std::size_t image_index = 0;
    for (std::size_t index = 0; index < archive.records().size(); ++index) {
      if (archive.records()[index].type != "DDS ") {
        continue;
      }
      (void)upload(archive, index, sprite_id(identities, image_index++));
    }
    if (image_index == 0) {
      throw std::runtime_error("Missing selected loading-screen image");
    }
  }
};
StartupTitleSurface::StartupTitleSurface(
    resources::ResourceService& resources, render::SpriteOutput& renderer,
    std::function<unsigned()> choose_loading)
    : state_(std::allocate_shared<State>(storage_for(resources), resources,
                                         renderer, std::move(choose_loading))) {
}
StartupTitleSurface::~StartupTitleSurface() = default;
void StartupTitleSurface::begin() {
  auto& state = *state_;
  state.end();
  state.resources.request("sprite.title", kTitleTag);
  state.resources.request("sprite.font", kTitleTag);
  state.phase = State::Phase::title;
}
bool StartupTitleSurface::ready() {
  auto& state = *state_;
  if (state.phase == State::Phase::idle) {
    throw std::logic_error("Title resources not requested");
  }
  if (state.phase == State::Phase::title && state.pump(kTitleTag)) {
    state.construct_title();
    const auto selected = state.choose_loading();
    constexpr unsigned loading_count = 10;
    if (selected >= loading_count) {
      throw std::runtime_error("Loading selection out of range");
    }
    const auto number = selected + 1;
    state.loading_name = "sprite.loading";
    constexpr unsigned decimal_radix = 10;
    state.loading_name.push_back(
        static_cast<char>('0' + number / decimal_radix));
    state.loading_name.push_back(
        static_cast<char>('0' + number % decimal_radix));
    state.resources.request(state.loading_name, kLoadingTag);
    state.phase = State::Phase::loading;
    return false;
  }
  if (state.phase == State::Phase::loading && state.pump(kLoadingTag)) {
    state.construct_loading();
    state.phase = State::Phase::ready;
    return false;  // Preserve the separate initialization-completion
                   // observation.
  }
  return state.phase == State::Phase::ready;
}
void StartupTitleSurface::show(bool visible, TitlePresentation presentation) {
  auto& state = *state_;
  if (!visible) {
    state.renderer.clear_sprites();
    return;
  }
  if (state.phase != State::Phase::ready || !state.metrics) {
    throw std::logic_error("Title cannot draw before resource construction");
  }
  std::size_t count = 0;
  for (std::size_t index = 0; index < kDrawIds.size(); ++index) {
    const State::SpriteImage* selected = nullptr;
    for (const auto& image : state.images) {
      if (image.id == kDrawIds.at(index)) {
        selected = &image;
      }
    }
    if (selected == nullptr) {
      throw std::runtime_error("Missing original title sprite identity");
    }
    auto position = kSpritePositions.at(index);
    position.width = selected->width;
    position.height = selected->height;
    state.commands.at(count++) = {
        selected->texture,
        position,
        {0, 0, 1, 1},
        {1, 1, 1, presentation.sprite_opacity.at(index)}};
  }
  constexpr float font_size = 16;
  constexpr float text_left = 10;
  constexpr float text_top = 700;
  const auto scale =
      font_size / static_cast<float>(state.metrics->pixel_size());
  const auto baseline =
      text_top + static_cast<float>(state.metrics->baseline()) * scale;
  const auto atlas_width =
      static_cast<float>(state.metrics.value().atlas_width());
  const auto atlas_height =
      static_cast<float>(state.metrics.value().atlas_height());
  auto cursor = text_left;
  for (const auto codepoint : kVersion) {
    const auto glyph = state.metrics->glyph(codepoint);
    if (!glyph) {
      throw std::runtime_error("Missing original version glyph");
    }
    if (!glyph->blank) {
      const auto width = static_cast<float>(glyph->right - glyph->left);
      const auto height = static_cast<float>(glyph->bottom - glyph->top);
      state.commands.at(count++) = {
          state.font_texture,
          {cursor + static_cast<float>(glyph->bearing_left) * scale,
           baseline + static_cast<float>(glyph->bearing_top) * scale,
           width * scale, height * scale},
          {static_cast<float>(glyph->left) / atlas_width,
           static_cast<float>(glyph->top) / atlas_height, width / atlas_width,
           height / atlas_height},
          {1, 1, 1, presentation.version_opacity},
          channel(glyph->channel)};
    }
    cursor += static_cast<float>(glyph->advance) * scale;
  }
  state.renderer.set_sprites(std::span(state.commands).first(count));
}
void StartupTitleSurface::end() noexcept { state_->end(); }
}  // namespace bw::game
