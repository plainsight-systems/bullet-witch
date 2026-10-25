// DDS resource uploads preserve decoded format metadata and consume the host's
// immutable launch mip policy. Raw icon/generated RGBA uploads remain
// base-only; they are not implicitly reclassified as DDS. Both-phase C.121/I.11
// and COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) keep
// input borrowing distinct from native ownership. All chain construction is
// setup work. Portable ordered sprite submission and renderer-owned texture
// lifetimes. Upload borrows tightly packed straight-alpha RGBA8 only for the
// call; the renderer copies it and returns an opaque identity local to that
// output. Release invalidates that identity and removes any queued quads that
// reference it. Releasing an invalid identity is harmless. Commands use
// top-left logical 1280x720 coordinates and normalized UVs, are copied during
// submission, and render in order after video. Invalid data/capacity/stale
// identities throw before replacing the current commands. Normal textures use
// rgba; font coverage can select one packed channel and tint white coverage
// with command color. Main-thread, single-owner capability; renderer
// destruction releases textures. Algorithm: upload resources -> submit bounded
// quads -> native draw -> release. Fixed capacities bound setup residency and
// per-frame work; native textures allocate only on upload. GPU completion
// protects release and frame reuse. Design MCP: cpp-guidelines R.2
// (#rr-use-ptr) uses bounded borrowed spans; cpp-performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds command storage
// rather than growing containers during presentation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

namespace bw::formats {
struct WorldTextureImage;
}
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
// Destination uses the logical canvas; UVs address the selected texture.
struct Sprite {
  TextureHandle texture;
  Rect destination;
  Rect uv{0, 0, 1, 1};
  Color color;
  TextureChannel channel{TextureChannel::rgba};
  // Clockwise canvas rotation about a local pixel origin. Zero preserves the
  // existing axis-aligned contract. Authored loading art supplies this pivot;
  // it is not inferred from texture dimensions. I.1 exposes the transform and
  // GPU.1 keeps the image resident while only constants change.
  float rotation_radians{};
  float rotation_origin_x{}, rotation_origin_y{};
};
class SpriteOutput {
 public:
  SpriteOutput() = default;
  virtual ~SpriteOutput() = default;
  SpriteOutput(const SpriteOutput&) = delete;
  SpriteOutput& operator=(const SpriteOutput&) = delete;
  SpriteOutput(SpriteOutput&&) = delete;
  SpriteOutput& operator=(SpriteOutput&&) = delete;
  // Copy borrowed RGBA pixels into a resource owned by this output.
  virtual TextureHandle upload_texture(std::uint32_t width,
                                       std::uint32_t height,
                                       std::span<const std::uint8_t> rgba) = 0;
  // Prepared base DDS image; preserve source format, build configured levels.
  virtual TextureHandle upload_texture(
      const formats::WorldTextureImage& image) = 0;
  // Release also removes submitted quads that reference this identity.
  virtual void release_texture(TextureHandle texture) noexcept = 0;
  // Validate the whole submission before replacing the ordered command list.
  virtual void set_sprites(std::span<const Sprite> sprites) = 0;
  virtual void clear_sprites() noexcept = 0;
};
}  // namespace bw::render
