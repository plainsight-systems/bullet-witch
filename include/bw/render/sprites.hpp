// Portable ordered sprite submission and renderer-owned texture lifetimes.
// Upload borrows tightly packed straight-alpha RGBA8 only for the call; the
// renderer copies it and returns an opaque identity local to that output.
// Release invalidates that
// identity and removes any queued quads that reference it. Releasing an invalid
// identity is harmless. Commands use top-left logical 1280x720 coordinates and
// normalized UVs, are copied during submission, and render in order after
// video. Invalid data/capacity/stale identities throw before replacing the
// current commands. Normal textures use rgba; font coverage can select one
// packed channel and tint white coverage with command color. Main-thread,
// single-owner capability; renderer destruction releases textures. Algorithm:
// upload resources -> submit bounded quads -> native draw -> release. Fixed
// capacities bound setup residency and per-frame work; native textures allocate
// only on upload. GPU completion protects release and frame reuse. Design MCP:
// cpp-guidelines R.2 (#rr-use-ptr) uses bounded borrowed spans; cpp-performance
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds command
// storage rather than growing containers during presentation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace bw::render {
inline constexpr float kCanvasWidth = 1280;
inline constexpr float kCanvasHeight = 720;
inline constexpr std::size_t kMaximumTextures = 128;
inline constexpr std::size_t kMaximumSprites = 511;
struct TextureHandle {
  std::uint64_t value{};
  friend bool operator==(TextureHandle, TextureHandle) = default;
};
struct Rect {
  float x{};
  float y{};
  float width{};
  float height{};
};
struct Color {
  float red{1};
  float green{1};
  float blue{1};
  float alpha{1};
};
enum class TextureChannel : std::uint8_t { rgba, red, green, blue, alpha };
struct Sprite {
  TextureHandle texture;
  Rect destination;
  Rect uv{0, 0, 1, 1};
  Color color;
  TextureChannel channel{TextureChannel::rgba};
};
class SpriteOutput {
 public:
  SpriteOutput() = default;
  virtual ~SpriteOutput() = default;
  SpriteOutput(const SpriteOutput&) = delete;
  SpriteOutput& operator=(const SpriteOutput&) = delete;
  SpriteOutput(SpriteOutput&&) = delete;
  SpriteOutput& operator=(SpriteOutput&&) = delete;
  virtual TextureHandle upload_texture(std::uint32_t width,
                                       std::uint32_t height,
                                       std::span<const std::uint8_t> rgba) = 0;
  virtual void release_texture(TextureHandle texture) noexcept = 0;
  virtual void set_sprites(std::span<const Sprite> sprites) = 0;
  virtual void clear_sprites() noexcept = 0;
};
}  // namespace bw::render
