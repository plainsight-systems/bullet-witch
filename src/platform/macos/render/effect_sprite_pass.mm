// Retain one source-ordered GPU submission after complete packet validation.
// Core I.11 and GPU.1 were reread for implementation: texture/vertex inputs
// become native-owned observations and never borrow producer frame storage.
#include "effect_sprite_pass.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

#include "buffer_allocation.hpp"
#include "bw/math/matrix.hpp"
#include "bw/memory/allocator.hpp"

namespace bw::platform::macos {
namespace {
constexpr std::uint32_t kTriangleVertices = 3;
struct Parameters {
  std::array<std::array<float, 4>, 4> clip_rows;
  std::array<float, 4> color_rate;
  std::array<float, 4> fog;
};
}  // namespace
struct EffectSpritePass::State {
  struct Batch {
    render::WorldSpriteBatch command;
    id<MTLTexture> texture;
  };
  State(id<MTLDevice> native, const std::shared_ptr<memory::Domain>& owner,
        const std::shared_ptr<memory::DeviceBudget>& account)
      : device(native), domain(owner), budget(account), batches(memory::Allocator<Batch>(owner)) {}
  id<MTLDevice> device;
  std::shared_ptr<memory::Domain> domain;
  std::shared_ptr<memory::DeviceBudget> budget;
  BufferAllocation vertices;
  memory::Vector<Batch> batches;
  render::WorldSpriteCapacity capacity{};
  Parameters parameters{};
  std::array<float, 3> alpha_rate{}, additive_rate{};
  std::array<std::array<id<MTLRenderPipelineState>, 2>, 2> pipelines{};
  id<MTLDepthStencilState> depth{};
};
EffectSpritePass::EffectSpritePass(id<MTLDevice> device,
                                   const std::shared_ptr<memory::Domain>& domain,
                                   const std::shared_ptr<memory::DeviceBudget>& budget) {
  if (device == nil || !domain || !budget) throw std::invalid_argument("Missing sprite pass owner");
  state_ = std::allocate_shared<State>(memory::Allocator<State>(domain), device, domain, budget);
}
EffectSpritePass::~EffectSpritePass() = default;
void EffectSpritePass::configure(const render::WorldSpriteCapacity& capacity) {
  auto& state = *state_;
  if (!state.batches.empty() || capacity.vertices == 0 || capacity.batches == 0 ||
      capacity.vertices % kTriangleVertices != 0 ||
      capacity.batches > capacity.vertices / kTriangleVertices ||
      capacity.vertices > state.device.maxBufferLength / sizeof(render::WorldSpriteVertex)) {
    throw std::invalid_argument("Invalid sprite pass capacity or active reconfiguration");
  }
  memory::Vector<std::uint8_t> initial(memory::Allocator<std::uint8_t>(state.domain));
  initial.resize(static_cast<std::size_t>(capacity.vertices) * sizeof(render::WorldSpriteVertex));
  auto buffer = BufferAllocation::create(state.device, state.budget, initial);
  memory::Vector<State::Batch> batches(memory::Allocator<State::Batch>(state.domain));
  batches.reserve(capacity.batches);
  state.vertices = std::move(buffer);
  state.batches.swap(batches);
  state.capacity = capacity;
}
void EffectSpritePass::publish(const render::WorldSpriteUpdate& update,
                               std::span<const id<MTLTexture>> textures) {
  auto& state = *state_;
  if (update.vertices.size() > state.capacity.vertices ||
      update.batches.size() > state.capacity.batches || textures.size() != update.batches.size() ||
      !math::all_finite(update.clip_rows) || !math::all_finite(update.alpha_color_rate) ||
      !math::all_finite(update.additive_color_rate) || !std::isfinite(update.fog_far) ||
      !std::isfinite(update.fog_distance_reciprocal) || update.fog_distance_reciprocal < 0) {
    throw std::invalid_argument("Invalid sprite publication");
  }
  for (const auto& vertex : update.vertices) {
    if (!math::all_finite(vertex.position) || !math::all_finite(vertex.color) ||
        !math::all_finite(vertex.uv)) {
      throw std::invalid_argument("Nonfinite sprite vertex");
    }
  }
  std::size_t end = 0;
  for (std::size_t index = 0; index < update.batches.size(); ++index) {
    const auto& batch = update.batches[index];
    if (batch.first_vertex != end || batch.vertex_count == 0 ||
        batch.vertex_count % kTriangleVertices != 0 ||
        batch.vertex_count > update.vertices.size() - end || textures[index] == nil ||
        textures[index].textureType != MTLTextureType2D || textures[index].device != state.device ||
        (batch.blend != render::WorldSpriteBlend::alpha &&
         batch.blend != render::WorldSpriteBlend::additive)) {
      throw std::invalid_argument("Invalid sprite triangle range or texture binding");
    }
    end += batch.vertex_count;
  }
  if (end != update.vertices.size()) throw std::invalid_argument("Unbatched sprite vertices");
  // Existing host completion makes this retained shared-buffer write exclusive.
  if (!update.vertices.empty()) {
    std::memcpy(state.vertices.get().contents, update.vertices.data(),
                update.vertices.size_bytes());
  }
  state.batches.clear();
  for (std::size_t index = 0; index < update.batches.size(); ++index) {
    state.batches.push_back({update.batches[index], textures[index]});
  }
  state.parameters.clip_rows = update.clip_rows;
  state.parameters.fog = {update.fog_far, update.fog_distance_reciprocal, 0, 0};
  state.alpha_rate = update.alpha_color_rate;
  state.additive_rate = update.additive_color_rate;
}
void EffectSpritePass::encode(id<MTLRenderCommandEncoder> encoder, id<MTLLibrary> library,
                              MTLPixelFormat format) {
  auto& state = *state_;
  if (state.batches.empty()) return;
  if (encoder == nil || library == nil ||
      (format != MTLPixelFormatBGRA8Unorm && format != MTLPixelFormatRGBA16Float)) {
    throw std::invalid_argument("Invalid sprite encoding capability");
  }
  auto& pipelines = state.pipelines.at(format == MTLPixelFormatRGBA16Float ? 1 : 0);
  for (std::size_t blend = 0; blend < pipelines.size(); ++blend) {
    if (pipelines.at(blend) != nil) continue;
    auto* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"world_effect_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"world_effect_fragment"];
    descriptor.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
    auto* color = descriptor.colorAttachments[0];
    color.pixelFormat = format;
    color.blendingEnabled = YES;
    color.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    color.destinationRGBBlendFactor =
        blend == 0 ? MTLBlendFactorOneMinusSourceAlpha : MTLBlendFactorOne;
    color.rgbBlendOperation = MTLBlendOperationAdd;
    color.sourceAlphaBlendFactor = MTLBlendFactorSourceAlpha;
    color.destinationAlphaBlendFactor = color.destinationRGBBlendFactor;
    color.alphaBlendOperation = MTLBlendOperationAdd;
    NSError* error = nil;
    pipelines.at(blend) = [state.device newRenderPipelineStateWithDescriptor:descriptor
                                                                       error:&error];
    if (pipelines.at(blend) == nil) {
      throw std::runtime_error("Original sprite pipeline creation failed");
    }
  }
  if (state.depth == nil) {
    auto* descriptor = [[MTLDepthStencilDescriptor alloc] init];
    descriptor.depthCompareFunction = MTLCompareFunctionGreaterEqual;
    descriptor.depthWriteEnabled = NO;
    state.depth = [state.device newDepthStencilStateWithDescriptor:descriptor];
    if (state.depth == nil) throw std::runtime_error("Original sprite depth state creation failed");
  }
  [encoder setCullMode:MTLCullModeNone];
  [encoder setDepthStencilState:state.depth];
  [encoder setDepthBias:0 slopeScale:0 clamp:0];
  [encoder setVertexBuffer:state.vertices.get() offset:0 atIndex:0];
  for (const auto& batch : state.batches) {
    const bool additive = batch.command.blend == render::WorldSpriteBlend::additive;
    const auto& rate = additive ? state.additive_rate : state.alpha_rate;
    state.parameters.color_rate = {rate[0], rate[1], rate[2], 1};
    [encoder setRenderPipelineState:pipelines.at(additive ? 1 : 0)];
    [encoder setVertexBytes:&state.parameters length:sizeof(Parameters) atIndex:1];
    [encoder setFragmentTexture:batch.texture atIndex:0];
    [encoder drawPrimitives:MTLPrimitiveTypeTriangle
                vertexStart:batch.command.first_vertex
                vertexCount:batch.command.vertex_count];
  }
}
void EffectSpritePass::clear() noexcept {
  state_->batches.clear();
  state_->vertices.reset();
  state_->capacity = {};
}
bool EffectSpritePass::empty() const noexcept { return state_->batches.empty(); }
}  // namespace bw::platform::macos
