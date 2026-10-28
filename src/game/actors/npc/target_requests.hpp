// Original @t request arbitration is separate from perception scan ownership.
// World filtering submits copied target/knowledge/strength. Keep stable
// descending strength order, consume the first only when strength>=1, then
// discard all pending requests. Equal fixed-size @t payloads remain distinct:
// source5cdb30 returns a nonzero sentinel for equal payloads. Duration is
// carried source data, not a forced-target timer. Construction-thread-only; no
// clock or actor mutation. Capacity is explicit admission, not silent
// truncation. I.1 exposes the command boundary; MEM.9 fixes bounded storage.
// Both corpora read in both phases. Source5cd610/5ce090/5ce150/5ce1c0:
// first-combat-ai.md.
#pragma once
#include "perception.hpp"
namespace bw::game {
struct NpcTargetRequest {
  std::optional<std::uint64_t> target;
  TargetKnowledge knowledge{};
  float strength{}, duration{};
};
class NpcTargetRequests {
 public:
  static constexpr std::size_t kCapacity = 512;
  void submit(NpcTargetRequest);
  std::optional<NpcTargetRequest> consume() noexcept;
  void clear() noexcept { count_ = 0; }

 private:
  std::array<NpcTargetRequest, kCapacity> pending_{};
  std::size_t count_{};
};
}  // namespace bw::game
