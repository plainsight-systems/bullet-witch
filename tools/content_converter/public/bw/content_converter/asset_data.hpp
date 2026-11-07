#ifndef BW_CONTENT_CONVERTER_ASSET_DATA_HPP
#define BW_CONTENT_CONVERTER_ASSET_DATA_HPP
#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Responsibility: the owning, in-memory form of each asset kind the
// converter builds, and the encoders that write each into the payload
// layout its consuming system's header declares (mesh_asset.hpp,
// morph_targets_asset.hpp, skeleton_asset.hpp, texture_asset.hpp,
// material_asset.hpp) and into the cooked frame (cooked_asset.hpp). The
// runtime never encodes, so the encoders live in the tool; the layouts
// live with the readers, so there is one definition of each
// (docs/architecture/content.md, "Boundaries": the converter shares the
// format and id code with the runtime and nothing else).
//
// Contract.
//   Each encode_* produces bytes that the matching decode_* accepts and
//   that round-trip every field; sections are laid out in the order the
//   layout header states, each padded with zero bytes to the next 16-byte
//   boundary, so equal data gives equal bytes on every host.
//   encode_* validates its input against the same rules the decoder
//   checks and returns the decoder's error instead of writing bytes that
//   would not load.
//   encode_cooked_asset frames a payload: the header with the given kind,
//   id and input hash, payload_hash computed here, then the payload.
//
// Core guidelines read: R.1 (vectors own every buffer), I.1 (an encoder is
// a pure function of its data), E.27 (the decoder's named error is the
// encoder's). Performance guidelines read: CDSA.32 (this is the one place
// content is transformed into the consumer's layout).
namespace bw::content_converter {

struct MeshGroupData {
  geometry::VertexStreamLayout layout{};
  std::uint32_t vertex_count{};
  std::vector<std::byte> stream0{};
  std::vector<std::byte> stream1{};
  std::vector<std::uint16_t> indices{};
  std::uint32_t first_draw{};
  std::uint32_t draw_count{};
  std::uint32_t morph_set{geometry::kNoMorphSet};
};

struct MeshAssetData {
  std::vector<MeshGroupData> groups{};
  std::vector<geometry::MeshDrawRecord> draws{};
  std::uint32_t material_count{};
  std::vector<std::vector<std::uint32_t>> bone_maps{};
  std::array<float, 3> bounds_max{};
  std::array<float, 3> bounds_min{};
  std::array<float, 3> center{};
  float radius{};
  std::string skeleton_id{};
};

struct MorphTargetData {
  std::string name{};
  // vertex_count * kMorphDeltaBytes, as stored.
  std::vector<std::byte> deltas{};
};
struct MorphTargetsData {
  std::uint32_t group{};
  std::uint32_t vertex_count{};
  std::vector<MorphTargetData> targets{};
};

struct BoneData {
  std::string name{};
  std::uint32_t id{};
  std::int32_t parent{};
  std::uint32_t field_12{};
  std::int32_t field_20{};
  animation::BoneTransformRecord a{};
  animation::BoneTransformRecord b{};
};
struct SkeletonAssetData {
  animation::BindBlock bind_block{animation::BindBlock::Unknown};
  std::vector<BoneData> bones{};
};

struct SubresourceData {
  std::uint32_t row_pitch{};
  std::vector<std::byte> bytes{};
};
struct TextureAssetData {
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint32_t faces{1};
  std::uint32_t mip_levels{};
  rendering::TextureFormat format{};
  rendering::TextureAssetDimension dimension{rendering::TextureAssetDimension::Texture2D};
  std::uint32_t flags{};
  // RHI subresource order: slice-major, mip-minor.
  std::vector<SubresourceData> subresources{};
};

struct NamedSwitch {
  std::string name{};
  bool value{};
};
struct NamedScalar {
  std::string name{};
  float value{};
};
struct NamedVector {
  std::string name{};
  std::array<float, 4> value{};
};
struct NamedTexture {
  std::string name{};
  std::string id{};
};
struct MaterialAssetData {
  std::string template_name{};
  std::vector<NamedSwitch> switches{};
  std::vector<NamedScalar> scalars{};
  std::vector<NamedVector> vectors{};
  std::vector<NamedTexture> textures{};
};

// Each returns the payload bytes, or an empty vector with the decoder's
// error set in `error` (the error type of the matching layout header,
// carried as its numeric value, the failing index and, for a material,
// the table as material_asset.hpp numbers them; 0 otherwise).
struct EncodeFailure {
  std::uint32_t error{};
  std::uint32_t index{};
  std::uint32_t table{};
  [[nodiscard]] bool ok() const noexcept { return error == 0U; }
};

[[nodiscard]] std::vector<std::byte> encode_mesh_asset(const MeshAssetData& data,
                                                       EncodeFailure& failure);
[[nodiscard]] std::vector<std::byte> encode_morph_targets(const MorphTargetsData& data,
                                                          EncodeFailure& failure);
[[nodiscard]] std::vector<std::byte> encode_skeleton_asset(const SkeletonAssetData& data,
                                                           EncodeFailure& failure);
[[nodiscard]] std::vector<std::byte> encode_texture_asset(const TextureAssetData& data,
                                                          EncodeFailure& failure);
[[nodiscard]] std::vector<std::byte> encode_material_asset(const MaterialAssetData& data,
                                                           EncodeFailure& failure);

// The whole file: cooked header, id, padding, payload. An AssetId is
// validated at construction, so the frame always encodes for a non-empty
// id; the empty (default-constructed) id is the one input that cannot be
// framed and yields an empty vector.
[[nodiscard]] std::vector<std::byte> encode_cooked_asset(content::AssetKind kind,
                                                         const content::AssetId& id,
                                                         const content::Hash& input_hash,
                                                         std::span<const std::byte> payload);

} // namespace bw::content_converter

#endif
