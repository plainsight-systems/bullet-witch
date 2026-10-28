// Mission-wide named-reaction arbitration, shared by all admitted NPCs. The
// world owns this noncopyable table and alone advances its ten source timers.
// Controllers submit copied original motion-slot keys through world commands;
// they cannot reset time or retain references into the table. Admission records
// a new key for ten seconds, rejects an existing key unless bypass was
// authored, and replaces the shortest remaining entry only when full. Bypass of
// an existing key does not refresh its duration. Invalid time throws unchanged.
// Single simulation thread, fixed O(10) work and storage, no allocation. Core
// F.7 (#rf-smart) keeps synchronous services borrowed; performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md) bounds all storage.
// Both design corpora retrieved. Source coverage: first-combat-exchange.md.
#pragma once
#include <array>
#include <cstdint>
namespace bw::game {
class NpcReactionAdmission {
 public:
  NpcReactionAdmission() = default;
  ~NpcReactionAdmission() = default;
  NpcReactionAdmission(const NpcReactionAdmission&) = delete;
  NpcReactionAdmission& operator=(const NpcReactionAdmission&) = delete;
  NpcReactionAdmission(NpcReactionAdmission&&) = delete;
  NpcReactionAdmission& operator=(NpcReactionAdmission&&) = delete;
  bool admit(std::uint32_t motion_slot, bool bypass);
  void advance(float seconds);

 private:
  struct Entry {
    std::uint32_t slot{};
    float remaining{};
    bool active{};
  };
  static constexpr std::size_t kAdmissionCapacity = 10;
  std::array<Entry, kAdmissionCapacity> entries_{};
};
}  // namespace bw::game
