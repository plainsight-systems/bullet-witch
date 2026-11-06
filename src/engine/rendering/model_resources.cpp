#include "bw/rendering/model_resources.hpp"

#include "bw/animation/skeleton_asset.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/cooked_asset.hpp"
#include "bw/content/loader.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/materials/material_asset.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/texture_asset.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Responsibility: the out-of-line part of bw/rendering/model_resources.hpp:
// the mesh, then its skeleton, then every material, then the textures the
// materials name under the asked parameters, each failure ending the load.
namespace bw::rendering {

namespace {

ModelLoadFailure failed(ModelLoadError error, std::string_view id, std::string detail) {
  return ModelLoadFailure{error, std::string(id), std::move(detail)};
}

// "error <number> at index <index>", the form every decoder's failure takes.
std::string decode_text(unsigned error, std::uint32_t index) {
  return "error " + std::to_string(error) + " at index " + std::to_string(index);
}

std::string device_text(std::string_view what, rhi::ResourceError error, std::string_view detail) {
  std::string text(what);
  text += ": error " + std::to_string(static_cast<unsigned>(error));
  if (!detail.empty()) {
    text += ": ";
    text += detail;
  }
  return text;
}

ModelLoadFailure load_mesh(rhi::Device& device, const content::Loader& loader,
                           const content::AssetId& mesh_id, ModelResources& model) {
  content::LoadResult loaded = loader.load(mesh_id, content::AssetKind::Mesh);
  if (!loaded.ok()) {
    return failed(ModelLoadError::MeshLoadFailed, mesh_id.view(),
                  content::load_failure_text(loaded.failure));
  }
  model.mesh_asset = std::move(loaded.handle);
  const geometry::MeshAssetFailure decoded =
      geometry::decode_mesh_asset(model.mesh_asset.payload(), model.mesh_view);
  if (decoded.error != geometry::MeshAssetError::None) {
    std::string detail = decode_text(static_cast<unsigned>(decoded.error), decoded.index);
    if (decoded.error == geometry::MeshAssetError::LayoutInvalid) {
      detail += " layout error " + std::to_string(static_cast<unsigned>(decoded.layout_error));
    }
    return failed(ModelLoadError::MeshDecodeFailed, mesh_id.view(), std::move(detail));
  }
  const geometry::MeshResourceFailure resources =
      geometry::create_mesh_resources(device, model.mesh_view, mesh_id.view(), model.mesh);
  if (!resources.ok()) {
    std::string detail = "step " + std::to_string(static_cast<unsigned>(resources.error)) +
                         " at index " + std::to_string(resources.index);
    if (resources.error == geometry::MeshResourceError::DrawRangeInvalid) {
      detail += " range error " + std::to_string(static_cast<unsigned>(resources.range));
    } else {
      detail += " " + device_text("device", resources.resource, resources.detail);
    }
    return failed(ModelLoadError::MeshResourcesFailed, mesh_id.view(), std::move(detail));
  }
  return {};
}

ModelLoadFailure load_skeleton(const content::Loader& loader, ModelResources& model) {
  const std::string_view text = model.mesh_view.skeleton_id();
  if (text.empty()) {
    return {};
  }
  const content::AssetIdResult id = content::make_asset_id(text);
  if (!id.ok()) {
    return failed(ModelLoadError::SkeletonIdInvalid, text,
                  "error " + std::to_string(static_cast<unsigned>(id.error)));
  }
  content::LoadResult loaded = loader.load(id.id, content::AssetKind::Skeleton);
  if (!loaded.ok()) {
    return failed(ModelLoadError::SkeletonLoadFailed, text,
                  content::load_failure_text(loaded.failure));
  }
  model.skeleton_asset = std::move(loaded.handle);
  const animation::SkeletonAssetFailure decoded =
      animation::decode_skeleton_asset(model.skeleton_asset->payload(), model.skeleton);
  if (decoded.error != animation::SkeletonAssetError::None) {
    return failed(ModelLoadError::SkeletonDecodeFailed, text,
                  decode_text(static_cast<unsigned>(decoded.error), decoded.index));
  }
  return {};
}

ModelLoadFailure load_materials(const content::Loader& loader, const content::AssetId& mesh_id,
                                ModelResources& model) {
  const std::uint32_t count = model.mesh.material_count;
  model.materials.reserve(count);
  for (std::uint32_t m = 0; m < count; ++m) {
    const content::AssetIdResult id =
        content::split_asset_id(mesh_id, content::SplitKind::Material, m);
    if (!id.ok()) {
      return failed(ModelLoadError::MaterialIdInvalid, mesh_id.view(),
                    "material " + std::to_string(m) + ": error " +
                        std::to_string(static_cast<unsigned>(id.error)));
    }
    content::LoadResult loaded = loader.load(id.id, content::AssetKind::Material);
    if (!loaded.ok()) {
      return failed(ModelLoadError::MaterialLoadFailed, id.id.view(),
                    content::load_failure_text(loaded.failure));
    }
    ModelMaterial& material = model.materials.emplace_back();
    material.asset = std::move(loaded.handle);
    const materials::MaterialAssetFailure decoded =
        materials::decode_material_asset(material.asset.payload(), material.view);
    if (decoded.error != materials::MaterialAssetError::None) {
      return failed(ModelLoadError::MaterialDecodeFailed, id.id.view(),
                    "error " + std::to_string(static_cast<unsigned>(decoded.error)) + " in table " +
                        std::to_string(decoded.table) + " at index " +
                        std::to_string(decoded.index));
    }
  }
  return {};
}

// The texture `text` names, loaded, decoded and created, appended to the
// model; `index` is its place in model.textures.
ModelLoadFailure load_texture(rhi::Device& device, const content::Loader& loader,
                              std::string_view text, ModelResources& model, std::uint32_t& index) {
  const content::AssetIdResult id = content::make_asset_id(text);
  if (!id.ok()) {
    return failed(ModelLoadError::TextureIdInvalid, text,
                  "error " + std::to_string(static_cast<unsigned>(id.error)));
  }
  const content::LoadResult loaded = loader.load(id.id, content::AssetKind::Texture);
  if (!loaded.ok()) {
    return failed(ModelLoadError::TextureLoadFailed, text,
                  content::load_failure_text(loaded.failure));
  }
  TextureAssetView view;
  const TextureAssetFailure decoded = decode_texture_asset(loaded.handle.payload(), view);
  if (decoded.error != TextureAssetError::None) {
    return failed(ModelLoadError::TextureDecodeFailed, text,
                  decode_text(static_cast<unsigned>(decoded.error), decoded.index));
  }
  const std::string name(text);
  rhi::TextureDescription description = view.texture_description();
  description.initial_access = rhi::Access::ShaderResourcePixel;
  description.debug_name = name;
  std::vector<rhi::SubresourceData> subresources;
  subresources.reserve(view.subresource_count());
  for (std::uint32_t i = 0; i < view.subresource_count(); ++i) {
    subresources.push_back(view.subresource(i));
  }
  rhi::TextureResult created = device.create_texture(description, subresources);
  if (!created.ok()) {
    return failed(ModelLoadError::TextureCreateFailed, text,
                  device_text("texture", created.error(), created.detail()));
  }
  rhi::TextureViewDescription view_description{};
  if (view.dimension() == TextureAssetDimension::Cube) {
    view_description.dimension = rhi::TextureDimension::TextureCube;
  }
  rhi::ShaderResourceViewResult viewed =
      device.create_shader_resource_view(created.value(), view_description);
  if (!viewed.ok()) {
    return failed(ModelLoadError::TextureCreateFailed, text,
                  device_text("texture view", viewed.error(), viewed.detail()));
  }
  index = static_cast<std::uint32_t>(model.textures.size());
  model.textures.push_back(
      ModelTexture{id.id, view.dimension(), std::move(created.value()), std::move(viewed.value())});
  return {};
}

ModelLoadFailure load_textures(rhi::Device& device, const content::Loader& loader,
                               std::span<const std::string_view> parameters,
                               ModelResources& model) {
  model.texture_parameters.assign(parameters.begin(), parameters.end());
  model.material_textures.assign(model.materials.size() * parameters.size(), kNoModelTexture);
  for (std::size_t m = 0; m < model.materials.size(); ++m) {
    for (std::size_t p = 0; p < parameters.size(); ++p) {
      const materials::MaterialAssetView::TextureLookup lookup =
          model.materials[m].view.find_texture(parameters[p]);
      if (!lookup.found) {
        continue;
      }
      std::uint32_t index = 0;
      while (index < model.textures.size() && model.textures[index].id.view() != lookup.id) {
        ++index;
      }
      if (index == model.textures.size()) {
        ModelLoadFailure failure = load_texture(device, loader, lookup.id, model, index);
        if (!failure.ok()) {
          return failure;
        }
      }
      model.material_textures[m * parameters.size() + p] = index;
    }
  }
  return {};
}

} // namespace

const ModelTexture* ModelResources::texture(std::uint32_t material,
                                            std::string_view parameter) const noexcept {
  for (std::size_t p = 0; p < texture_parameters.size(); ++p) {
    if (texture_parameters[p] == parameter) {
      const std::uint32_t index = material_textures[material * texture_parameters.size() + p];
      return index == kNoModelTexture ? nullptr : &textures[index];
    }
  }
  return nullptr;
}

ModelLoadFailure load_model(rhi::Device& device, const content::Loader& loader,
                            const content::AssetId& mesh_id,
                            std::span<const std::string_view> texture_parameters,
                            ModelResources& out) {
  out = ModelResources{};
  ModelResources model;
  ModelLoadFailure failure = load_mesh(device, loader, mesh_id, model);
  if (failure.ok()) {
    failure = load_skeleton(loader, model);
  }
  if (failure.ok()) {
    failure = load_materials(loader, mesh_id, model);
  }
  if (failure.ok()) {
    failure = load_textures(device, loader, texture_parameters, model);
  }
  if (failure.ok()) {
    out = std::move(model);
  }
  return failure;
}

} // namespace bw::rendering
