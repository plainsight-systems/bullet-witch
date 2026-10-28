// Mission-owned original AI definitions. Resolve each requested ai publication,
// retain each archive once, follow its ai.add/amb argument to the USER wrapper,
// then decode immutable tree metadata. Actors borrow definitions until mission
// teardown; no actor state, VM, clock or physics capability enters this owner.
// Construction throws on missing, ambiguous or malformed bindings. All work
// and domain allocation occur during serial loading, proportional to archive
// bytes and selected nodes. Lookup is bounded by the requested closure size.
// Core I.11 (#ri-raw) makes returned references non-owning; performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// shares one decoded immutable resource across actor instances.
#pragma once
#include "bw/content/formats/behavior_tree.hpp"
#include "bw/content/resources/access.hpp"

namespace bw::game {
class MissionBehaviorAssets {
 public:
  MissionBehaviorAssets(const platform::Files&, const resources::CatalogSet&,
                        const std::shared_ptr<memory::Domain>&,
                        std::span<const std::string_view> names);
  MissionBehaviorAssets(const MissionBehaviorAssets&) = delete;
  MissionBehaviorAssets& operator=(const MissionBehaviorAssets&) = delete;
  MissionBehaviorAssets(MissionBehaviorAssets&&) = delete;
  MissionBehaviorAssets& operator=(MissionBehaviorAssets&&) = delete;
  ~MissionBehaviorAssets() = default;
  const formats::BehaviorTreeDefinition& definition(
      std::string_view name) const;

 private:
  struct Archive {
    std::size_t catalog{}, file{};
    resources::ResourceArchive data;
  };
  struct Entry {
    std::string_view name;
    formats::BehaviorTreeDefinition tree;
  };
  memory::Vector<Archive> archives_;
  memory::Vector<Entry> entries_;
};
}  // namespace bw::game
