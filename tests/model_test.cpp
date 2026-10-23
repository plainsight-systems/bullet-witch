// In-process model contracts using authored bytes only. Validate indexed draw
// associations, ordered material/group identity, strict rejection and domain
// lifetime; do not claim original rendering acceptance. Algorithm: decode a
// valid independent fixture, move its owner, then mutate draw/buffer/framing
// boundaries and observe public error/lifetime results. Cost fixture-sized;
// no production performance claim. Design/implementation SL.con.3 (#rsl-bounds)
// and MEM.6 (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// use bounded fixture writes and an explicit injected test backing provider.
#include "bw/formats/model.hpp"

#include "model_fixture.hpp"

namespace {
class ModelBacking final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
bw::formats::ModelDefinition parse(
    bw::resources::Bytes bytes,
    const std::shared_ptr<bw::memory::Domain>& domain) {
  bw::resources::ArchiveBytes owned(
      bytes.begin(), bytes.end(), bw::memory::Allocator<std::uint8_t>(domain));
  return bw::formats::ModelDefinition::parse(
      bw::resources::ResourceArchive::parse(std::move(owned)), domain);
}
void rejects(
    bw::resources::Bytes bytes,
    const std::shared_ptr<bw::memory::Domain>& domain,
    bw::resources::ErrorCode code = bw::resources::ErrorCode::malformed) {
  bool rejected = false;
  try {
    (void)parse(std::move(bytes), domain);
  } catch (const bw::resources::Error& error) {
    rejected = error.code == code;
  }
  require(rejected, "Model malformed/unsupported input rejection");
  require(domain->snapshot().live_bytes == 0,
          "Model rejection releases all partial owners");
}
}  // namespace
void model_tests() {
  const auto domain =
      bw::memory::Domain::create(std::make_shared<ModelBacking>());
  {
    model_fixture::Options options;
    options.group = 3;
    options.volume = 0x02000019;
    auto model = parse(model_fixture::archive(options), domain);
    require(model.draws().size() == 1 && model.declarations().size() == 1,
            "Model draw and declaration");
    const auto& draw = model.draws().front();
    require(draw.group == options.group && draw.volume == options.volume &&
                draw.stream.stride == 12,
            "Model preserves native group/volume/stream metadata");
    require(model.materials().front().shader_selector == options.shader,
            "Model preserves shader feature words");
    const auto& material = model.materials().front();
    require(
        material.constants.size() == 1 &&
            material.constants.front().semantic ==
                std::array<std::uint32_t, 2>{model_fixture::tag("CCOL"), 0} &&
            material.constants.front().binding ==
                std::array<std::uint32_t, 2>{0xffffffff, 0xffffffff} &&
            material.constants.front().value ==
                std::array<float, 4>{1.0F, 0.5F, 0.25F, 1.0F},
        "Model preserves material semantic, native binding words and float4");
    auto moved = std::move(model);
    require(
        moved.vertex_bytes(0).size() == 36 && moved.index_bytes(0).size() == 6,
        "Moved model retains exact buffer bytes");
    require(domain->snapshot().live_bytes > 0,
            "Model storage charges explicit domain");
  }
  require(domain->snapshot().live_bytes == 0,
          "Model destruction releases domain");
  {
    model_fixture::Options options;
    options.material_continuation = true;
    options.base_only = true;
    const auto model = parse(model_fixture::archive(options), domain);
    const auto& material = model.materials().at(model.draws().front().material);
    require(
        material.constants.size() == 1 && material.textures.size() == 1 &&
            material.matrices.size() == 1 &&
            material.matrices.front().semantic == model_fixture::tag("JINT") &&
            material.matrices.front().source_matrix == 3 &&
            material.matrices.front().destination_matrix == 0,
        "Continuation inherits material bindings and retains its joint "
        "palette");
  }
  model_fixture::Options options;
  options.draw[4] = 1;
  rejects(model_fixture::archive(options), domain);
  options = {};
  options.draw[3] = 2;
  rejects(model_fixture::archive(options), domain);
  options = {};
  options.draw[1] = 0xffffffff;
  rejects(model_fixture::archive(options), domain);
  options = {};
  options.draw[0] = 5;
  rejects(model_fixture::archive(options), domain,
          bw::resources::ErrorCode::unsupported);
  const std::array<std::uint16_t, 3> invalid_indices{0, 1, 3};
  options = {};
  options.indices = invalid_indices;
  rejects(model_fixture::archive(options), domain);
  auto truncated = model_fixture::archive();
  truncated.pop_back();
  rejects(std::move(truncated), domain);
  // A valid nonzero start/base pair protects their separate D3D meanings.
  constexpr std::array<float, 12> positions{-2, -2, 0, -1, -1, 0,
                                            1,  -1, 0, 0,  1,  0};
  constexpr std::array<std::uint16_t, 6> indices{2, 2, 2, 0, 1, 2};
  {
    options = {};
    options.positions = positions;
    options.indices = indices;
    options.draw = {4, 1, 0, 3, 3, 1};
    const auto model = parse(model_fixture::archive(options), domain);
    require(model.draws().front().base_vertex == 1 &&
                model.draws().front().start_index == 3,
            "Model preserves base vertex independently of start index");
  }
  const auto limited = bw::memory::Domain::create(
      std::make_shared<ModelBacking>(), {.max_live_bytes = 1});
  bool exhausted = false;
  try {
    (void)parse(model_fixture::archive(), limited);
  } catch (const std::bad_alloc&) {
    exhausted = true;
  }
  require(exhausted && limited->snapshot().live_bytes == 0,
          "Model allocation failure has no fallback or leak");
}
