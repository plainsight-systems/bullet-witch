#include "bw/rendering/shaders/permutation.hpp"

#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/lit_template.hpp"
#include "bw/rendering/scene/cluster_records.hpp"
#include "bw/rendering/scene/compute_shaders.hpp"
#include "bw/rendering/scene/depth_shaders.hpp"
#include "bw/rendering/scene/draw_records.hpp"
#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/forward_pass.hpp"
#include "bw/rendering/scene/instance.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string_view>

// Responsibility: the out-of-line parts of bw/rendering/shaders/permutation.hpp:
// the enumeration of a shader's build set, the registry of every shader the
// engine builds, and lookup by name.
//
// Contract:
//   - enumerate_permutations writes every id below permutation_count that
//     should_compile admits (all ids when it is null), strictly ascending, and
//     returns how many. A span shorter than permutation_count violates the
//     header's precondition; that is a programming error and the function
//     aborts rather than return a short build set that would look complete.
//   - shader_registry() is one constexpr table with unique names, checked at
//     compile time below.
//   - find_shader returns the entry whose name is equal, or null.
//
// Algorithm, enumerate (each permutation id is offered to the predicate):
//   1. Check the output capacity.
//   2. For id = 0 .. permutation_count - 1, keep id when the predicate admits it.
//
// Design and cost: O(permutation_count) predicate calls at build time; the
// registry is read-only data with no initialization at runtime. find_shader
// is a linear scan: the registry holds a few dozen entries at most (CDSA.9:
// tiny arrays are scanned, not searched).
//
// Retrieved rules: SL.con.3 (writes never pass the output span; the capacity
// is checked before the loop instead of per element), SL.str.2 (names are
// string_views into static storage), E.27 (no error channel is invented for a
// precondition; violation is fatal and visible). Performance: CDSA.9 (linear
// scan for a tiny table), MEM.9 (no allocation; the caller owns the output).
namespace bw::shaders {
namespace {

// Per-draw block of the fixture shader. Offsets: color 0, scale 16,
// texture 24, sampler 28; size 32, dense with no padding.
struct alignas(kBlockAlignment) FullscreenTriangleDraw {
  Float4 color{};
  Float2 scale{};
  TextureIndex texture{};
  SamplerIndex sampler{};
};

} // namespace
} // namespace bw::shaders

// The macro stringizes its first argument as the block name, which becomes an
// HLSL identifier, so the type is named unqualified here.
using bw::shaders::FullscreenTriangleDraw;
BW_PARAMETER_BLOCK_LAYOUT(FullscreenTriangleDraw,
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, color),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, scale),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, texture),
                          BW_PARAMETER_MEMBER(FullscreenTriangleDraw, sampler));

namespace bw::shaders {
namespace {

// The fixture shader the integration tests compile. source_path names a file
// under the shader source root that step C of the shader-pipeline slice adds,
// once a compiler can prove it; nothing in this module opens it.
struct FullscreenTriangle {
  using FlipY = BoolDimension<"BW_FLIP_Y">;
  using Domain = PermutationDomain<FlipY>;
  static constexpr std::string_view name = "fullscreen_triangle";
  static constexpr std::string_view source_path = "fullscreen_triangle.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<FullscreenTriangleDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// The fixture pixel shader the RHI integration tests pair with
// FullscreenTriangle: it writes the interpolated color to target 0.
struct FullscreenTriangleColor {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = "fullscreen_triangle_color";
  static constexpr std::string_view source_path = "fullscreen_triangle_color.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Pixel;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = nullptr;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// The presentation slice's fixture shaders (bw/rendering/geometry/pulled_mesh_fixture.hpp):
// the pulled-vertex mesh, the decode probe and the pixel shader both share.
// They leave with the fixture when the importer supplies content.
struct PulledMesh {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = geometry::kPulledMeshShader;
  static constexpr std::string_view source_path = "pulled_mesh.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<PulledMeshDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// One permutation per element type, the define's value the enumerator's.
struct VertexDecodeProbe {
  using DecodeType = EnumDimension<"BW_DECODE_TYPE", geometry::VertexElementType,
                                   geometry::kVertexElementTypeCount>;
  using Domain = PermutationDomain<DecodeType>;
  static_assert(DecodeType::define == geometry::kDecodeTypeDefine);
  static constexpr std::string_view name = geometry::kVertexDecodeProbeShader;
  static constexpr std::string_view source_path = "vertex_decode_probe.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<VertexDecodeProbeDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

struct VertexColor {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = geometry::kVertexColorShader;
  static constexpr std::string_view source_path = "vertex_color.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Pixel;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = nullptr;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// The RHI's indirect-work fixture shaders (command_list.hpp, Indirect work):
// four quads chosen by a draw's index range, drawn with vertex_color, and a
// compute shader that stores one word per thread group. Neither reads a
// block; the compute shader's inputs are root constants.
struct IndirectQuads {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = "indirect_quads";
  static constexpr std::string_view source_path = "indirect_quads.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = nullptr;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

struct IndirectStore {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = "indirect_store";
  static constexpr std::string_view source_path = "indirect_store.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Compute;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr const BlockLayout* draw_block = nullptr;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

// The forward pass's lit shaders (bw/rendering/scene/forward_pass.hpp,
// contract 1): the lit template's domain and predicate, the View and
// FixtureLight static blocks and the RecordDraw per-draw block in both
// stages; the vertex stage declares DrawRecord and InstanceRecord as buffer
// blocks, and the pixel stage, which has no draw index, InstanceRecord and
// LitMaterial. Each artifact's reflection holds the bound blocks it
// reads and every buffer block it declares.
constexpr std::array<const BlockLayout*, 2> kForwardStaticBlocks{
    &BlockLayoutOf<View>::value, &BlockLayoutOf<FixtureLight>::value};
// A vertex stage that draws records (draw_records.hpp, contract 2).
constexpr std::array<const BlockLayout*, 2> kRecordVertexBufferBlocks{
    &BlockLayoutOf<DrawRecord>::value, &BlockLayoutOf<InstanceRecord>::value};
// A lit pixel stage that draws records: the instance at the index its
// vertex stage passes, and the instance's material.
constexpr std::array<const BlockLayout*, 2> kRecordLitPixelBufferBlocks{
    &BlockLayoutOf<InstanceRecord>::value, &BlockLayoutOf<LitMaterial>::value};
static_assert(kRecordLitPixelBufferBlocks.size() <= kMaxBufferBlocks);

struct ForwardLitMesh {
  using Domain = materials::LitDomain;
  static constexpr std::string_view name = scene::kForwardLitVertexShader;
  static constexpr std::string_view source_path = "forward_lit_mesh.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{kForwardStaticBlocks};
  static constexpr std::span<const BlockLayout* const> buffer_blocks{kRecordVertexBufferBlocks};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<RecordDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& domain) noexcept {
    return materials::lit_switches_admitted(materials::lit_switches(domain));
  }
};

struct ForwardLit {
  using Domain = materials::LitDomain;
  static constexpr std::string_view name = scene::kForwardLitPixelShader;
  static constexpr std::string_view source_path = "forward_lit.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Pixel;
  static constexpr std::span<const BlockLayout* const> static_blocks{kForwardStaticBlocks};
  static constexpr std::span<const BlockLayout* const> buffer_blocks{kRecordLitPixelBufferBlocks};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<RecordDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& domain) noexcept {
    return materials::lit_switches_admitted(materials::lit_switches(domain));
  }
};

// The lit template's depth-only shaders (bw/rendering/scene/depth_shaders.hpp,
// contract 1): the lit template's domain, the View static block and the
// RecordDraw per-draw block in both stages. The vertex stage is built for
// every admitted permutation and declares the record blocks; the pixel
// stage is built for the admitted masked permutations only and declares
// InstanceRecord and LitMaterial.
constexpr std::array<const BlockLayout*, 1> kDepthStaticBlocks{&BlockLayoutOf<View>::value};

struct DepthLitMesh {
  using Domain = materials::LitDomain;
  static constexpr std::string_view name = scene::kDepthVertexShader;
  static constexpr std::string_view source_path = "depth_lit_mesh.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Vertex;
  static constexpr std::span<const BlockLayout* const> static_blocks{kDepthStaticBlocks};
  static constexpr std::span<const BlockLayout* const> buffer_blocks{kRecordVertexBufferBlocks};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<RecordDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& domain) noexcept {
    return materials::lit_switches_admitted(materials::lit_switches(domain));
  }
};

struct DepthLit {
  using Domain = materials::LitDomain;
  static constexpr std::string_view name = scene::kDepthPixelShader;
  static constexpr std::string_view source_path = "depth_lit.hlsl";
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Pixel;
  static constexpr std::span<const BlockLayout* const> static_blocks{kDepthStaticBlocks};
  static constexpr std::span<const BlockLayout* const> buffer_blocks{kRecordLitPixelBufferBlocks};
  static constexpr const BlockLayout* draw_block = &BlockLayoutOf<RecordDraw>::value;
  [[nodiscard]] static constexpr bool should_compile(const Domain& domain) noexcept {
    const materials::LitSwitches switches = materials::lit_switches(domain);
    return materials::lit_switches_admitted(switches) && switches.masked;
  }
};

// The scene's compute shaders (bw/rendering/scene/compute_shaders.hpp,
// contracts 1 and 2): no permutation, no static block, a per-dispatch
// block, and the buffer blocks each kernel reads.
template <const std::string_view& Name, const std::string_view& Source,
          const std::span<const BlockLayout* const>& Buffers, const BlockLayout* Draw>
struct SceneCompute {
  using Domain = PermutationDomain<>;
  static constexpr std::string_view name = Name;
  static constexpr std::string_view source_path = Source;
  static constexpr std::string_view entry_point = "main";
  static constexpr ShaderStage stage = ShaderStage::Compute;
  static constexpr std::span<const BlockLayout* const> static_blocks{};
  static constexpr std::span<const BlockLayout* const> buffer_blocks = Buffers;
  static constexpr const BlockLayout* draw_block = Draw;
  [[nodiscard]] static constexpr bool should_compile(const Domain& /*domain*/) noexcept {
    return true;
  }
};

constexpr std::array<const BlockLayout*, 3> kCullInstancesBlocks{
    &BlockLayoutOf<InstanceRecord>::value, &BlockLayoutOf<LevelRecord>::value,
    &BlockLayoutOf<CullingView>::value};
constexpr std::array<const BlockLayout*, 4> kWriteSurvivorsBlocks{
    &BlockLayoutOf<InstanceRecord>::value, &BlockLayoutOf<LevelRecord>::value,
    &BlockLayoutOf<CullingView>::value, &BlockLayoutOf<CullSurvivor>::value};
constexpr std::array<const BlockLayout*, 4> kCullClustersBlocks{
    &BlockLayoutOf<InstanceRecord>::value, &BlockLayoutOf<ClusterRecord>::value,
    &BlockLayoutOf<CullingView>::value, &BlockLayoutOf<CullSurvivor>::value};
constexpr std::array<const BlockLayout*, 1> kCountRecordsBlocks{&BlockLayoutOf<CullingList>::value};
constexpr std::array<const BlockLayout*, 4> kWriteRecordsBlocks{
    &BlockLayoutOf<CullSurvivor>::value, &BlockLayoutOf<ClusterRecord>::value,
    &BlockLayoutOf<CullingList>::value, &BlockLayoutOf<DrawRecord>::value};

constexpr std::span<const BlockLayout* const> kNoBlocks{};
constexpr std::span<const BlockLayout* const> kCullInstancesSpan{kCullInstancesBlocks};
constexpr std::span<const BlockLayout* const> kWriteSurvivorsSpan{kWriteSurvivorsBlocks};
constexpr std::span<const BlockLayout* const> kCullClustersSpan{kCullClustersBlocks};
constexpr std::span<const BlockLayout* const> kCountRecordsSpan{kCountRecordsBlocks};
constexpr std::span<const BlockLayout* const> kWriteRecordsSpan{kWriteRecordsBlocks};

constexpr std::string_view kCullInstancesSource = "scene_cull_instances.hlsl";
constexpr std::string_view kScanInstancesSource = "scene_scan_instances.hlsl";
constexpr std::string_view kWriteSurvivorsSource = "scene_write_survivors.hlsl";
constexpr std::string_view kCullClustersSource = "scene_cull_clusters.hlsl";
constexpr std::string_view kCountRecordsSource = "scene_count_records.hlsl";
constexpr std::string_view kWriteRecordsSource = "scene_write_records.hlsl";
constexpr std::string_view kReduceSource = "depth_hierarchy_reduce.hlsl";

constexpr const BlockLayout* kCullingDispatchBlock = &BlockLayoutOf<CullingDispatch>::value;
using SceneCullInstances = SceneCompute<scene::kCullInstancesShader, kCullInstancesSource,
                                        kCullInstancesSpan, kCullingDispatchBlock>;
using SceneScanInstances = SceneCompute<scene::kScanInstancesShader, kScanInstancesSource,
                                        kNoBlocks, kCullingDispatchBlock>;
using SceneWriteSurvivors = SceneCompute<scene::kWriteSurvivorsShader, kWriteSurvivorsSource,
                                         kWriteSurvivorsSpan, kCullingDispatchBlock>;
using SceneCullClusters = SceneCompute<scene::kCullClustersShader, kCullClustersSource,
                                       kCullClustersSpan, kCullingDispatchBlock>;
using SceneCountRecords = SceneCompute<scene::kCountRecordsShader, kCountRecordsSource,
                                       kCountRecordsSpan, kCullingDispatchBlock>;
using SceneWriteRecords = SceneCompute<scene::kWriteRecordsShader, kWriteRecordsSource,
                                       kWriteRecordsSpan, kCullingDispatchBlock>;
using DepthHierarchyReduceShader =
    SceneCompute<scene::kDepthHierarchyReduceShader, kReduceSource, kNoBlocks,
                 &BlockLayoutOf<DepthHierarchyReduce>::value>;

// Every shader the engine builds. A renderer slice adds an entry here and its
// main source under the first shader source root.
constexpr std::array kRegistry{
    describe_shader<FullscreenTriangle>(), describe_shader<FullscreenTriangleColor>(),
    describe_shader<PulledMesh>(),         describe_shader<VertexDecodeProbe>(),
    describe_shader<VertexColor>(),        describe_shader<ForwardLitMesh>(),
    describe_shader<ForwardLit>(),         describe_shader<IndirectQuads>(),
    describe_shader<IndirectStore>(),      describe_shader<DepthLitMesh>(),
    describe_shader<DepthLit>(),           describe_shader<SceneCullInstances>(),
    describe_shader<SceneScanInstances>(), describe_shader<SceneWriteSurvivors>(),
    describe_shader<SceneCullClusters>(),  describe_shader<SceneCountRecords>(),
    describe_shader<SceneWriteRecords>(),  describe_shader<DepthHierarchyReduceShader>(),
};

[[nodiscard]] consteval bool registry_names_unique() {
  for (std::size_t i = 0; i < kRegistry.size(); ++i) {
    for (std::size_t j = i + 1; j < kRegistry.size(); ++j) {
      if (kRegistry[i].name == kRegistry[j].name) {
        return false;
      }
    }
  }
  return true;
}
static_assert(registry_names_unique(), "shader registry names are unique");

} // namespace

std::size_t enumerate_permutations(const ShaderDescription& shader,
                                   std::span<PermutationId> out) noexcept {
  // Step 1: precondition of the header; a short span is a programming error.
  if (out.size() < shader.permutation_count) {
    std::abort();
  }
  // Step 2: ascending walk; the predicate is pure, so the set is the same on
  // every host.
  std::size_t written = 0;
  for (std::uint32_t id = 0; id < shader.permutation_count; ++id) {
    const PermutationId candidate{id};
    if (shader.should_compile == nullptr || shader.should_compile(candidate)) {
      out[written] = candidate;
      ++written;
    }
  }
  return written;
}

std::span<const ShaderDescription> shader_registry() noexcept { return kRegistry; }

const ShaderDescription* find_shader(std::span<const ShaderDescription> registry,
                                     std::string_view name) noexcept {
  for (const ShaderDescription& shader : registry) {
    if (shader.name == name) {
      return &shader;
    }
  }
  return nullptr;
}

} // namespace bw::shaders
