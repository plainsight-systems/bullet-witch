// Native final-edge witness inside the existing memory scenario. The literal
// expected image comes from original pixel-bytecode execution on this authored
// texture; no game asset or captured framebuffer is embedded. Core I.1 and
// GPU.9 were retrieved in both phases: activation is explicit and retained
// display storage is verified through completed device work/accounting.
#import <Metal/Metal.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include "bw/render/postprocess/world_output.hpp"
#include "platform/macos/memory/cpu_backing.hpp"
#include "platform/macos/render/texture_allocation.hpp"
#include "platform/macos/render/world_postprocess.hpp"
namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
struct EdgeUniforms {
  std::array<float, 2> inverse_extent{1.0F / 16, 1.0F / 16};
  float exposure_key{}, inverse_gamma{}, bloom_key{}, threshold{}, knee{}, log_luminance{};
  std::uint32_t adapted{}, bloom{}, enabled{};
  float opacity{1};
  std::array<float, 4> global_color{1, 1, 1, 1};
  std::array<float, 4> first_measurement_bounds{};
};
using Pixels = std::array<std::uint8_t, std::size_t{16} * 16 * 4>;
void read_pixels(id<MTLTexture> image, Pixels& pixels) {
  [image getBytes:pixels.data()
      bytesPerRow:NSUInteger{16} * 4
       fromRegion:MTLRegionMake2D(0, 0, 16, 16)
      mipmapLevel:0];
}
void finish(id<MTLCommandBuffer> command) {
  [command commit];
  [command waitUntilCompleted];
  require(command.status == MTLCommandBufferStatusCompleted, "Final edge command completed");
}
}  // namespace
void BWTestFinalEdge(id<MTLDevice> device) {
  auto domain = bw::memory::Domain::create(bw::platform::macos::make_cpu_backing());
  auto budget = bw::memory::DeviceBudget::create(domain);
  auto source = bw::platform::macos::TextureAllocation::create(device, budget, 16, 16,
                                                               MTLPixelFormatRGBA8Unorm);
  auto output = bw::platform::macos::TextureAllocation::create(
      device, budget, 16, 16, MTLPixelFormatBGRA8Unorm, MTLTextureType2D,
      MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead);
  Pixels pixels{};
  for (std::size_t y = 0; y < 16; ++y) {
    for (std::size_t x = 0; x < 16; ++x) {
      auto value = x < 4 ? 24 : (y + 3 > x ? 220 : 55);
      if (x >= 12) value = static_cast<int>((x * 37 + y * 53) % 256);
      for (std::size_t channel = 0; channel < 3; ++channel) {
        pixels.at((y * 16 + x) * 4 + channel) = static_cast<std::uint8_t>(value);
      }
      pixels.at((y * 16 + x) * 4 + 3) = 255;
    }
  }
  source.upload(pixels);
  auto queue = [device newCommandQueue];
  auto library = [device newLibraryWithURL:[NSURL fileURLWithPath:@BW_WORLD_SHADER_LIBRARY]
                                     error:nil];
  require(library != nil, "Final edge shader library available");
  auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
  descriptor.vertexFunction = [library newFunctionWithName:@"world_final_edge_vertex"];
  descriptor.fragmentFunction = [library newFunctionWithName:@"world_final_edge"];
  descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
  auto pipeline = [device newRenderPipelineStateWithDescriptor:descriptor error:nil];
  require(pipeline != nil, "Final edge shader pipeline available");
  auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
  pass.colorAttachments[0].texture = output.get();
  pass.colorAttachments[0].loadAction = MTLLoadActionClear;
  pass.colorAttachments[0].storeAction = MTLStoreActionStore;
  const auto draw_edge = [&] {
    auto command = [queue commandBuffer];
    auto encoder = [command renderCommandEncoderWithDescriptor:pass];
    const EdgeUniforms uniforms;
    [encoder setRenderPipelineState:pipeline];
    [encoder setFragmentTexture:source.get() atIndex:0];
    [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    [encoder endEncoding];
    finish(command);
    read_pixels(output.get(), pixels);
  };
  draw_edge();
  // Original ps9694e0 oracle, point/clamp sampling and integer pixel UVs.
  constexpr std::array<std::uint8_t, 256> expected{
      24,  24,  24,  24,  40,  55,  55,  55,  55,  55,  55,  55,  110, 196, 104, 24,  24,  24,  24,
      24,  40,  55,  55,  55,  55,  55,  55,  55,  122, 233, 14,  51,  24,  24,  24,  24,  40,  55,
      55,  55,  55,  55,  55,  55,  46,  30,  67,  104, 24,  24,  24,  24,  122, 220, 55,  55,  55,
      55,  55,  55,  46,  83,  120, 157, 24,  24,  24,  24,  122, 220, 220, 55,  55,  55,  55,  55,
      73,  136, 173, 210, 24,  24,  24,  24,  122, 220, 220, 220, 55,  55,  55,  55,  86,  189, 226,
      126, 24,  24,  24,  24,  122, 220, 220, 220, 220, 55,  55,  55,  113, 242, 23,  60,  24,  24,
      24,  24,  122, 220, 220, 220, 220, 220, 55,  55,  51,  39,  76,  113, 24,  24,  24,  24,  122,
      220, 220, 220, 220, 220, 220, 55,  51,  92,  129, 166, 24,  24,  24,  24,  122, 220, 220, 220,
      220, 220, 220, 220, 164, 145, 182, 219, 24,  24,  24,  24,  122, 220, 220, 220, 220, 220, 220,
      220, 213, 198, 235, 16,  24,  24,  24,  24,  122, 220, 220, 220, 220, 220, 220, 220, 213, 136,
      32,  69,  24,  24,  24,  24,  122, 220, 220, 220, 220, 220, 220, 220, 151, 48,  85,  122, 24,
      24,  24,  24,  122, 220, 220, 220, 220, 220, 220, 220, 178, 101, 138, 175, 24,  24,  24,  24,
      122, 220, 220, 220, 220, 220, 220, 220, 191, 154, 191, 228, 24,  24,  24,  24,  122, 220, 220,
      220, 220, 220, 220, 220, 218, 207, 244, 25};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      require(std::abs(static_cast<int>(pixels.at(i * 4 + channel)) - expected.at(i)) <= 1,
              "Final edge pixels agree with original bytecode on supplied texture");
    }
    require(pixels.at(i * 4 + 3) == 255, "Final edge retains source alpha");
  }
  // Independent colored/alpha input prevents grayscale from hiding incorrect
  // luminance weights or an alpha policy accidentally forced to one.
  for (std::size_t y = 0; y < 16; ++y) {
    for (std::size_t x = 0; x < 16; ++x) {
      const std::array values{(x * 37 + y * 11) % 256, (x * 5 + y * 59) % 256,
                              (x * 23 + y * 17) % 256, (x * 29 + y * 31) % 256};
      for (std::size_t c = 0; c < 4; ++c) {
        pixels.at((y * 16 + x) * 4 + c) = static_cast<std::uint8_t>(values.at(c));
      }
    }
  }
  source.upload(pixels);
  draw_edge();
  struct SourcePixel {
    std::size_t index;
    std::array<std::uint8_t, 4> bgra;
  };
  constexpr std::array<SourcePixel, 16> colored{{{0, {0, 0, 0, 0}},
                                                 {15, {94, 68, 36, 132}},
                                                 {17, {20, 32, 24, 30}},
                                                 {35, {83, 101, 109, 119}},
                                                 {59, {28, 200, 160, 126}},
                                                 {68, {140, 224, 168, 210}},
                                                 {94, {131, 77, 37, 19}},
                                                 {117, {154, 165, 113, 148}},
                                                 {136, {44, 224, 104, 194}},
                                                 {157, {176, 52, 44, 114}},
                                                 {181, {26, 130, 26, 200}},
                                                 {204, {204, 224, 40, 178}},
                                                 {222, {69, 36, 116, 68}},
                                                 {241, {130, 90, 178, 208}},
                                                 {254, {45, 155, 147, 73}},
                                                 {255, {68, 160, 184, 102}}}};
  for (const auto& pixel : colored) {
    for (std::size_t c = 0; c < 4; ++c) {
      require(std::abs(static_cast<int>(pixels.at(pixel.index * 4 + c)) - pixel.bgra.at(c)) <= 1,
              "Final edge preserves original luminance decisions and filtered alpha");
    }
  }
  // Exercise the owner, including its additional post-tone quantization and
  // retained allocation. Uniform input isolates ordering from edge geometry.
  bw::platform::macos::WorldPostprocess owner(device, domain, budget);
  bw::render::WorldOutputParameters policy;
  policy.tone_mapping_enabled = true;
  policy.exposure_key = .18F;
  policy.gamma = 1;
  const auto render = [&] {
    owner.configure(policy);
    auto buffer = [queue commandBuffer];
    auto scene = owner.begin_scene(buffer, 16, 16, {.25F, .5F, .75F});
    [scene endEncoding];
    owner.prepare_output(buffer, library, {.presentation_seconds = 1.0F / 60});
    auto display = [buffer renderCommandEncoderWithDescriptor:pass];
    owner.encode_display(display, library, 1);
    [display endEncoding];
    finish(buffer);
    read_pixels(output.get(), pixels);
  };
  render();
  const auto ordinary = pixels;
  const auto before = budget->snapshot().live_logical_bytes;
  policy.final_edge_filter_enabled = true;
  render();
  require(budget->snapshot().live_logical_bytes == before + std::size_t{16} * 16 * 4,
          "Enabled post-tone edge allocates one accounted BGRA8 intermediate");
  for (std::size_t i = 0; i < pixels.size(); ++i) {
    require(std::abs(static_cast<int>(pixels.at(i)) - ordinary.at(i)) <= 1,
            "Final edge preserves uniform completed tone output");
  }
  const auto retained = budget->snapshot().live_logical_bytes;
  render();
  require(budget->snapshot().live_logical_bytes == retained,
          "Final edge intermediate is reused across completed frames");
  policy.final_edge_filter_enabled = false;
  render();
  require(budget->snapshot().live_logical_bytes == retained,
          "Output toggle preserves retained storage and exposure history");
  policy.tone_mapping_enabled = false;
  policy.final_edge_filter_enabled = true;
  render();
  require(pixels.at(0) == 191 && pixels.at(1) == 128 && pixels.at(2) == 64,
          "Edge without tone consumes original scene color directly");
  owner.reset();
  source.reset();
  output.reset();
  require(budget->snapshot().live_resources == 0, "Final edge owners release completed resources");
}
