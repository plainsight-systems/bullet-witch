// Retained load-time character support. Catalog/file capabilities are borrowed
// for this synchronous construction; result owns each selected archive once.
// Original compressed-first list lookup, ordered motion/label associations and
// optional USER publications are preserved. Missing optional lists are ready;
// malformed or unsupported present constructors fail without partial
// publication. Motion sampling, poses, actors and device meshes remain separate
// consumers. Algorithm: select support alias -> acquire optional lists ->
// resolve ordered constructor arguments -> decode motion payloads -> retain
// USER associations. Cost O(publications + channels + bytes read); archives are
// deduplicated and borrowed typed views remain valid until this move-only owner
// is destroyed. Both MCP phases: I.11 (#ri-raw) expresses owner/view
// boundaries; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) injects
// all retained metadata. See character-load-constructors.md.
#pragma once

#include "bw/content/formats/animation_definition.hpp"
#include "bw/content/resources/access.hpp"

namespace bw::content {
struct CharacterLocation {
  std::size_t archive{}, record{};
};
struct CharacterPublication {
  CharacterPublication(std::string_view name, CharacterLocation location,
                       const memory::Allocator<std::byte>& allocator)
      : name(name, allocator),
        archive(location.archive),
        record(location.record) {}
  memory::String name;
  std::size_t archive{}, record{};
};
struct CharacterMotion {
  CharacterPublication publication;
  formats::MotionDefinition definition;
};
struct CharacterShape {
  CharacterPublication publication;
  formats::ShapeAnimationDefinition definition;
};
struct CharacterSupport {
  explicit CharacterSupport(const std::shared_ptr<memory::Domain>& domain)
      : archives(memory::Allocator<resources::ResourceArchive>(domain)),
        motions(memory::Allocator<CharacterMotion>(domain)),
        shapes(memory::Allocator<CharacterShape>(domain)),
        publications(memory::Allocator<CharacterPublication>(domain)),
        motion_order(memory::Allocator<std::size_t>(domain)),
        shape_order(memory::Allocator<std::size_t>(domain)),
        parameter_order(memory::Allocator<std::size_t>(domain)) {}
  ~CharacterSupport() = default;
  CharacterSupport(const CharacterSupport&) = delete;
  CharacterSupport& operator=(const CharacterSupport&) = delete;
  CharacterSupport(CharacterSupport&&) noexcept = default;
  CharacterSupport& operator=(CharacterSupport&&) noexcept = default;
  memory::Vector<resources::ResourceArchive> archives;
  memory::Vector<CharacterMotion> motions;
  memory::Vector<CharacterShape> shapes;
  memory::Vector<CharacterPublication> publications;
  memory::Vector<std::size_t> motion_order, shape_order, parameter_order;
  std::optional<std::size_t> bone_names;
};
// Native alias: ea* -> ea0, pw* or m09 -> pw0, otherwise unchanged.
// Stage-suffix mesh/controlpoint binding remains in the separately retained
// model definition.
CharacterSupport load_character_support(
    const platform::Files& files, const resources::CatalogSet& catalogs,
    std::string_view base_name, const std::shared_ptr<memory::Domain>& domain);
}  // namespace bw::content
