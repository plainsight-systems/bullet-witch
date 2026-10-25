// Headless mip-resource contracts within the existing native memory scenario.
// Prepared authored byte chains exercise every face/level, exact logical/native
// accounting, invalid-payload atomicity and candidate allocation rollback.
// No private mutation access, game data or window is required. Device command
// consumers are absent during writes/readback; callers retire them before release.
// Both MCP phases: E.6 (#re-raii), I.11 (#ri-raw), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) require
// move-only owners, borrowed inputs and observable rollback. Fixed test arrays
// avoid allocation unrelated to the resource contract.
#import <Metal/Metal.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "bw/formats/world_texture.hpp"
#include "platform/macos/cpu_backing.hpp"
#include "platform/macos/texture_allocation.hpp"
#include "platform/macos/world_renderer.hpp"

namespace {
using bw::platform::macos::TextureAllocation;
static_assert(!std::is_copy_constructible_v<TextureAllocation>);
static_assert(!std::is_copy_assignable_v<TextureAllocation>);
static_assert(std::is_nothrow_move_constructible_v<TextureAllocation>);
static_assert(std::is_nothrow_move_assignable_v<TextureAllocation>);
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void verify_levels(const TextureAllocation& texture, std::span<const std::uint8_t> pixels,
                   std::size_t channels) {
  const std::size_t faces = texture.get().textureType == MTLTextureTypeCube ? 6 : 1;
  std::size_t offset = 0;
  std::array<std::uint8_t, 128> actual{};
  for (NSUInteger level = 0; level < texture.get().mipmapLevelCount; ++level) {
    const auto width = std::max(texture.get().width >> level, NSUInteger{1});
    const auto height = std::max(texture.get().height >> level, NSUInteger{1});
    const auto face_bytes = width * height * channels;
    require(face_bytes <= actual.size(), "Mip readback fixture fits bounded storage");
    for (NSUInteger face = 0; face < faces; ++face) {
      [texture.get() getBytes:actual.data()
                  bytesPerRow:width * channels
                bytesPerImage:face_bytes
                   fromRegion:MTLRegionMake2D(0, 0, width, height)
                  mipmapLevel:level
                        slice:face];
      require(std::ranges::equal(std::span(actual).first(face_bytes),
                                 pixels.subspan(offset, face_bytes)),
              "Every native mip and face preserves its prepared bytes");
      offset += face_bytes;
    }
  }
  require(offset == pixels.size(), "Native mip layout consumes the complete payload");
}
TextureAllocation transfer(TextureAllocation& source) { return std::move(source); }
void chain_contract(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  constexpr std::size_t bytes = (std::size_t{4} * 2 + std::size_t{2} * 1 + 1) * 4;
  std::array<std::uint8_t, bytes> pixels{};
  for (std::size_t i = 0; i < pixels.size(); ++i) pixels.at(i) = static_cast<std::uint8_t>(i);
  auto owner = TextureAllocation::create(device, budget, 4, 2, MTLPixelFormatRGBA8Unorm,
                                         MTLTextureType2D, MTLTextureUsageShaderRead, {.count = 3});
  owner.upload(pixels);
  require(owner.get().mipmapLevelCount == 3 && budget->snapshot().live_logical_bytes == bytes &&
              budget->snapshot().live_native_bytes == owner.get().allocatedSize,
          "Allocation and accounting include all rectangular levels");
  verify_levels(owner, pixels, 4);
  bool rejected = false;
  try {
    owner.upload(std::span(pixels).first(bytes - 1));
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "Incomplete chain rejected before upload");
  verify_levels(owner, pixels, 4);
  for (const auto count : {0U, 4U}) {
    rejected = false;
    try {
      (void)TextureAllocation::create(device, budget, 4, 2, MTLPixelFormatRGBA8Unorm,
                                      MTLTextureType2D, MTLTextureUsageShaderRead,
                                      {.count = count});
    } catch (const std::invalid_argument&) {
      rejected = true;
    }
    require(rejected && budget->snapshot().live_resources == 1 &&
                budget->snapshot().pending_resources == 0,
            "Invalid mip metadata cannot acquire a native charge");
  }
  auto limited = bw::memory::DeviceBudget::create(domain, {.max_logical_bytes = bytes - 1});
  rejected = false;
  try {
    owner = TextureAllocation::create(device, limited, 4, 2, MTLPixelFormatRGBA8Unorm,
                                      MTLTextureType2D, MTLTextureUsageShaderRead, {.count = 3});
  } catch (const std::bad_alloc&) {
    rejected = true;
  }
  require(rejected && limited->snapshot().live_resources == 0 &&
              limited->snapshot().pending_resources == 0,
          "Full-chain budget rejection preserves the old owner and unwinds candidate charge");
  verify_levels(owner, pixels, 4);
  auto moved = transfer(owner);
  require(owner.get() == nil && moved.get() != nil, "Moving native ownership empties the source");
  moved.reset();
  require(budget->snapshot().live_resources == 0, "Mip owner release returns every level charge");
}
void cube_contract(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  constexpr std::size_t bytes = (std::size_t{4} * 4 + std::size_t{2} * 2 + 1) * 6 * 8;
  std::array<std::uint8_t, bytes> pixels{};
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    pixels.at(i) = static_cast<std::uint8_t>((i * 17 + i / 13) % 256);
  }
  auto owner =
      TextureAllocation::create(device, budget, 4, 4, MTLPixelFormatRGBA16Float, MTLTextureTypeCube,
                                MTLTextureUsageShaderRead, {.count = 3});
  owner.upload(pixels);
  require(budget->snapshot().live_logical_bytes == bytes,
          "Half-float cube accounts all six faces at every level");
  verify_levels(owner, pixels, 8);
  owner.reset();
  bw::platform::macos::WorldRenderer renderer(device, [device newCommandQueue], domain, budget);
  bw::formats::WorldTextureImage image{
      4,
      4,
      bw::formats::WorldTextureFormat::rgba16_float,
      bw::memory::Vector<std::uint8_t>(pixels.begin(), pixels.end(),
                                       bw::memory::Allocator<std::uint8_t>(domain)),
      bw::formats::WorldTextureShape::cube,
      3};
  const auto handle = renderer.upload_world_texture(image);
  require(budget->snapshot().live_logical_bytes == bytes,
          "World publication preserves the prepared full-chain layout");
  image.pixels.pop_back();
  bool rejected = false;
  try {
    (void)renderer.upload_world_texture(image);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected && budget->snapshot().live_resources == 1 &&
              budget->snapshot().pending_resources == 0,
          "Failed world upload retires its candidate without replacing current residency");
  renderer.release_world_texture(handle);
  require(budget->snapshot().live_resources == 0 && budget->snapshot().live_native_bytes == 0,
          "World release retires the complete mip resource");
}
}  // namespace
void BWTestTextureMips(id<MTLDevice> device) {
  chain_contract(device);
  cube_contract(device);
}
