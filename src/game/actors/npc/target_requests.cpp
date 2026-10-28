// Preserve native stable strength ordering and one-consumption reset. No data
// reads, allocations or perception mutation occur here. I.1/MEM.9 retain the
// explicit copied-request and fixed-storage contracts from the header.
#include "target_requests.hpp"

#include <cmath>
#include <stdexcept>
namespace bw::game {
void NpcTargetRequests::submit(NpcTargetRequest request) {
  if (!std::isfinite(request.strength) || !std::isfinite(request.duration) ||
      (request.target && *request.target == 0) ||
      (request.knowledge != TargetKnowledge::unknown &&
       request.knowledge != TargetKnowledge::searching &&
       request.knowledge != TargetKnowledge::known)) {
    throw std::invalid_argument("Invalid original target request");
  }
  if (request.strength <= 0) return;
  if (count_ == pending_.size()) {
    throw std::length_error("NPC target request capacity exhausted");
  }
  std::size_t at = count_;
  while (at && pending_.at(at - 1).strength < request.strength) {
    pending_.at(at) = pending_.at(at - 1);
    --at;
  }
  pending_.at(at) = request;
  ++count_;
}
std::optional<NpcTargetRequest> NpcTargetRequests::consume() noexcept {
  const auto result = count_ && pending_[0].strength >= 1
                          ? std::optional{pending_[0]}
                          : std::nullopt;
  count_ = 0;
  return result;
}
}  // namespace bw::game
