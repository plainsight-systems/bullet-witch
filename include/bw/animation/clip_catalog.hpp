// Immutable named clips and ordered compatibility-slot aliases. Input motion
// definitions/skeleton are borrowed only during preparation; names and decoded
// keys are owned in the injected domain. One entry per distinct publication;
// aliases share that entry, never a copy of its keys. Unique names and unique
// in-range slots are required. Missing alias targets remain explicitly absent.
// Algorithm: index names, resolve aliases in caller order, apply each resolved
// repeat write to its shared resource, then prepare every clip once. Unaliased
// resources retain their authored repeat bit. Preserve requested alias repeat,
// raw resource repeat and final effective repeat separately. Preparation fails
// atomically through value ownership. Queries are immutable/concurrent-safe;
// returned references survive until destruction/move of this owner. Slot/index
// access is O(1), name lookup O(log clips); preparation O(keys + aliases log
// clips + clips log clips). No query allocation or mutation. SkeletonPose owns
// absent-track bind defaults so compatible costume poses may share these keys.
// Design: Core I.11 (#ri-raw) separates borrowing from ownership, E.4
// (#re-design-invariants) validates publication identity before construction;
// performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-
// consumer-layout.md) prepares/indexes once, MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) forbids steady-state allocation.
// A synchronous optional setup callback runs after each completed clip; it
// must not reenter the partial catalog. It is not retained.
// See prepared-animation-catalog.md for source mutation order and reference.
#pragma once

#include <functional>

#include "pose.hpp"

namespace bw::animation {
struct NamedClipSource {
  std::string_view name;
  std::reference_wrapper<const formats::MotionDefinition> definition;
};
struct ClipAliasRequest {
  std::size_t slot{};
  std::string_view name;
  bool repeating{};
};
struct PreparedClipAlias {
  std::optional<std::size_t> clip;
  bool requested_repeat{};
};
class PreparedClipCatalog {
 public:
  PreparedClipCatalog(std::span<const NamedClipSource> sources,
                      const formats::SkeletonDefinition& skeleton,
                      std::size_t slot_count,
                      std::span<const ClipAliasRequest> aliases,
                      const std::shared_ptr<memory::Domain>& domain,
                      const std::function<void()>& progress = {});
  PreparedClipCatalog(const PreparedClipCatalog&) = delete;
  PreparedClipCatalog& operator=(const PreparedClipCatalog&) = delete;
  PreparedClipCatalog(PreparedClipCatalog&&) noexcept = default;
  PreparedClipCatalog& operator=(PreparedClipCatalog&&) noexcept = default;
  ~PreparedClipCatalog() = default;
  std::size_t size() const { return clips_.size(); }
  std::size_t slot_count() const { return aliases_.size(); }
  const PreparedClip& clip(std::size_t index) const { return clips_.at(index); }
  std::string_view name(std::size_t index) const { return names_.at(index); }
  bool raw_repeat(std::size_t index) const { return raw_repeat_.at(index); }
  const PreparedClipAlias& alias(std::size_t slot) const {
    return aliases_.at(slot);
  }
  std::optional<std::size_t> find(std::string_view name) const;

 private:
  memory::Vector<memory::String> names_;
  memory::Vector<std::size_t> name_order_;
  memory::Vector<PreparedClip> clips_;
  memory::Vector<std::uint8_t> raw_repeat_;
  memory::Vector<PreparedClipAlias> aliases_;
};
}  // namespace bw::animation
