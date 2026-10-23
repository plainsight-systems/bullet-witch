// CPU stage definitions and retained original asset inputs. Caller supplies
// stable catalogs/files for the synchronous call and serializes access;
// returned owners retain the resource domain and bytes independently.
// Missing/malformed required meshes/textures fail atomically; unresolved
// load constructors are reported separately. Typed CPU readiness never implies
// gameplay or device readiness. Algorithm: enumerate matching model
// namespaces using native stage filters; resolve each mesh and runtime table
// texture through catalog priority; decode bounded meshes, retain each texture
// archive once and validate named payload presence; enumerate original stage
// auxiliary requests and construct bounded collision, area and animation views.
// Costs O(catalog entries +
// model metadata + indexed vertices + input bytes), plus catalog lookup cost.
// Optimization: texture archives are deduplicated by selected catalog/file
// identity; retained spans never copy bulk texture data. Design/implementation:
// cpp-guidelines SL.con.3 (#rsl-bounds), R.2
// (#rr-use-ptr) bound data and borrow owners; cpp-performance MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md)
// propagates the explicit allocator. See stage-content-decoding.md for source
// evidence.
#pragma once

#include "bw/content/access.hpp"
#include "bw/formats/model.hpp"
#include "bw/formats/section_table.hpp"
#include "bw/formats/stage_definitions.hpp"
#include "bw/formats/stage_environment.hpp"

namespace bw::content {
struct StageRecordLocation {
  std::size_t archive{}, record{};
};
struct StageTexture {
  StageTexture(std::string_view value, StageRecordLocation location, bool cube,
               const memory::Allocator<std::byte>& allocator)
      : name(value, allocator),
        archive(location.archive),
        record(location.record),
        cube(cube) {}
  memory::String name;
  std::size_t archive{}, record{};
  bool cube{};  // DDSC encoded input remains distinct from an ordinary PNG.
};
struct StageModel {
  StageModel(std::string_view value, formats::ModelDefinition definition,
             const memory::Allocator<std::byte>& allocator)
      : name(value, allocator),
        definition(std::move(definition)),
        textures(allocator) {}
  memory::String name;
  formats::ModelDefinition definition;
  // Primary stage construction creates an ordinary static object for this
  // model. Separate destructible/gimmick and dependency constructors stay
  // false; this does not imply every material or visibility group can draw.
  bool static_world_object{};
  // Optional original control-point USER retained by the character wrapper.
  std::optional<StageRecordLocation> control_points;
  memory::Vector<std::size_t>
      textures;  // Runtime texture-table order -> StageDefinition textures.
};
struct StageInput {
  StageInput(std::string_view value,
             const memory::Allocator<std::byte>& allocator,
             std::string_view reason)
      : name(value, allocator),
        unresolved(reason, allocator),
        animations(allocator),
        labels(allocator) {}
  memory::String name, unresolved;
  std::optional<std::size_t> archive;
  // True only after the original load-time constructor is represented, or an
  // optional catalog lookup completed absent. Device residency remains
  // separate.
  bool definition_ready{};
  std::optional<formats::CollisionDefinition> collision;
  std::optional<formats::PhysicsCollisionDefinition> physics_collision;
  std::optional<formats::AreaDefinition> area;
  std::optional<formats::SectionTable> table;
  std::optional<formats::ScalarAnimationDefinition> scalar_animation;
  std::optional<formats::MaterialAnimationDefinition> material_animation;
  std::optional<formats::MaterialAnimationLabels> material_labels;
  // Native list order, independently indexed animation and label publications.
  memory::Vector<std::size_t> animations, labels;
};
inline constexpr std::size_t kStageEnvironmentTextureSlots = 16;
struct StageDefinition {
  explicit StageDefinition(std::shared_ptr<memory::Domain> domain)
      : models(memory::Allocator<StageModel>(domain)),
        textures(memory::Allocator<StageTexture>(domain)),
        texture_table(memory::Allocator<std::size_t>(domain)),
        archives(memory::Allocator<resources::ResourceArchive>(domain)),
        inputs(memory::Allocator<StageInput>(std::move(domain))) {}
  memory::Vector<StageModel> models;
  memory::Vector<StageTexture> textures;
  // Exact standalone texture-set table order (palette sets have no model mesh).
  memory::Vector<std::size_t> texture_table;
  memory::Vector<resources::ResourceArchive> archives;
  memory::Vector<StageInput> inputs;
  // Authored scene settings are loaded once for the primary stage, not for
  // each dependency mesh. The customization archive remains retained above.
  std::optional<formats::StageEnvironment> environment;
  std::optional<StageRecordLocation> customization;
  // Generic retained settings; game consumers choose their own authored paths.
  std::span<const std::uint8_t> customization_bytes() const;
  // Native sixteen-slot stage environment map table, absent entries stay null.
  std::array<std::optional<std::size_t>, kStageEnvironmentTextureSlots>
      environment_textures{};
  std::span<const std::uint8_t> texture_bytes(std::size_t index) const;
  std::span<const std::uint8_t> texture_metadata(std::size_t index) const;
};
// Original stage-prefix enumeration, including all selected model definitions.
StageDefinition load_stage_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view stage, const std::shared_ptr<memory::Domain>& domain);
// Original sky request matches an exact model name, not a stage prefix.
StageDefinition load_model_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view model, const std::shared_ptr<memory::Domain>& domain);
// Exact texture-only set; exposes original table order through texture_table.
// Used by the original NPC palette registry; no dummy model is constructed.
StageDefinition load_texture_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view model, const std::shared_ptr<memory::Domain>& domain);
// Native USER publication retains encoded bytes. No future consumer semantics
// or device construction are implied; duplicate/missing publication is
// rejected.
struct UserDefinition {
  resources::ResourceArchive archive;
  std::size_t record{};
  std::span<const std::uint8_t> bytes() const {
    return archive.payload(record);
  }
};
UserDefinition load_user_definition(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view resource, const std::shared_ptr<memory::Domain>& domain);
}  // namespace bw::content
