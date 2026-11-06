#ifndef BW_RENDERING_MODEL_RESOURCES_HPP
#define BW_RENDERING_MODEL_RESOURCES_HPP

#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/loader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: a model as the renderer holds it: one mesh id loaded
// through the content loader with everything its draws name, decoded and on
// the GPU: the mesh's resources, its skeleton, its materials and the
// textures those materials name. A host, a test, a viewer and the scene
// renderer all load a model here; what shades it is not decided here.
//
// Contract.
//   1. load_model loads `mesh_id` as a mesh, decodes it and makes its
//      resources (bw/rendering/geometry/mesh_resources.hpp). mesh_asset
//      owns the payload mesh_view reads.
//   2. When the mesh names a skeleton, it is loaded and decoded;
//      skeleton_asset is then set and owns what `skeleton` reads. A mesh
//      that names none leaves skeleton_asset empty.
//   3. materials holds one entry per material index of the mesh, in index
//      order: the material asset whose id is split_asset_id(mesh_id,
//      Material, index), loaded and decoded. Each entry's asset owns what
//      its view reads.
//   4. For each material and each name in `texture_parameters`, a texture
//      record of that name is loaded, decoded and created as a GPU texture
//      with a view of its own dimension, 2D or cube (usage ShaderResource,
//      initial access ShaderResourcePixel, named by its id). A texture id is loaded and
//      created once however many materials name it; textures holds each in
//      first-use order. Texture records under other names are not loaded.
//   5. texture(material, parameter) is the texture that material's record
//      of that name resolves to, or null when the material has no such
//      record or the name was not in `texture_parameters`.
//   6. The first failure ends the load and is returned with the id
//      concerned and the failing layer's own error and detail as text; on a
//      failure `out` is left empty. Nothing is skipped or replaced: a
//      material or texture that cannot be loaded fails the model.
//   7. A ModelResources is movable and not copyable; a move keeps every
//      view valid.
//   The detail text is for a person; its wording is not part of the
//   contract. The loader and the device must outlive the call; the device
//   must outlive the resources.
//
// Read guidance: R.1 (handles own payloads, Refs own GPU resources), C.21
// (copy deleted, move defaulted, stated together), E.27, I.1.
namespace bw::rendering {

inline constexpr std::uint32_t kNoModelTexture = 0xFFFFFFFFU;

struct ModelMaterial {
  content::AssetHandle asset{};
  materials::MaterialAssetView view{};
};

struct ModelTexture {
  content::AssetId id{};
  TextureAssetDimension dimension{TextureAssetDimension::Texture2D};
  rhi::Ref<rhi::Texture> texture{};
  rhi::Ref<rhi::ShaderResourceView> view{};
};

struct ModelResources {
  content::AssetHandle mesh_asset{};
  geometry::MeshAssetView mesh_view{};
  geometry::MeshResources mesh{};
  std::optional<content::AssetHandle> skeleton_asset{};
  animation::SkeletonAssetView skeleton{};
  std::vector<ModelMaterial> materials{};
  std::vector<ModelTexture> textures{};

  ModelResources() = default;
  ModelResources(const ModelResources&) = delete;
  ModelResources& operator=(const ModelResources&) = delete;
  ModelResources(ModelResources&&) noexcept = default;
  ModelResources& operator=(ModelResources&&) noexcept = default;
  ~ModelResources() = default;

  // The names load_model was asked for, and for material m and name p the
  // index into textures, or kNoModelTexture, at
  // m * texture_parameters.size() + p. Filled by load_model; read through
  // texture().
  std::vector<std::string> texture_parameters{};
  std::vector<std::uint32_t> material_textures{};

  // Precondition: material is below materials.size().
  [[nodiscard]] const ModelTexture* texture(std::uint32_t material,
                                            std::string_view parameter) const noexcept;
};

enum class ModelLoadError : std::uint8_t {
  None,
  MeshLoadFailed,
  MeshDecodeFailed,
  MeshResourcesFailed,
  SkeletonIdInvalid,
  SkeletonLoadFailed,
  SkeletonDecodeFailed,
  MaterialIdInvalid,
  MaterialLoadFailed,
  MaterialDecodeFailed,
  TextureIdInvalid,
  TextureLoadFailed,
  TextureDecodeFailed,
  TextureCreateFailed,
};

struct ModelLoadFailure {
  ModelLoadError error{ModelLoadError::None};
  // The mesh, skeleton, material or texture id concerned.
  std::string id{};
  std::string detail{};
  [[nodiscard]] bool ok() const noexcept { return error == ModelLoadError::None; }
};

[[nodiscard]] ModelLoadFailure load_model(rhi::Device& device, const content::Loader& loader,
                                          const content::AssetId& mesh_id,
                                          std::span<const std::string_view> texture_parameters,
                                          ModelResources& out);

} // namespace bw::rendering

#endif
