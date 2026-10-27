// Shared game-menu cue policy over the existing prepared audio runtime. Flows
// publish state-transition events; this owner maps original bank/cue values and
// schedules their supplied presentation-time delays. It never loads assets,
// pumps the backend, reads a clock, or changes world pause. The borrowed output
// outlives this stable control-thread owner. Batch preconditions and delayed
// capacity are checked before any scheduling/playback mutation. Backend cue
// exhaustion retains CueOutput's optional rejection; it does not grow storage.
// Advance existing requests before processing inputs; submit new requests after
// inputs so source opening delays are not shortened by the preceding frame.
// O(32) scheduling, fixed storage, no queue allocations. Both design corpora:
// I.1 (#ri-explicit) exposes ownership/timing; MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) bounds retained work. Original evidence
// and the explicit pause-domain translation: native-pause/menu-audio.md.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "bw/audio/output.hpp"

namespace bw::game {
enum class MenuSound : std::uint8_t {
  pause,
  panel,
  confirm,
  back,
  cursor,
  error,
  dialog_open,
  dialog_close,
  dialog_page,
  save,
  title_start,
  mission_start,
  upgrade_purchase
};
struct MenuSoundRequest {
  MenuSound sound{};
  float delay_seconds{};
};
struct MenuSoundRequests {
  std::array<MenuSoundRequest, 4> requests{};
  std::size_t size{};
  void add(MenuSound sound, float delay_seconds = 0);
};
class MenuAudio {
 public:
  explicit MenuAudio(audio::CueOutput& output) : output_(output) {}
  ~MenuAudio() = default;
  MenuAudio(const MenuAudio&) = delete;
  MenuAudio& operator=(const MenuAudio&) = delete;
  MenuAudio(MenuAudio&&) = delete;
  MenuAudio& operator=(MenuAudio&&) = delete;
  void submit(const MenuSoundRequests& requests);
  void advance(float presentation_seconds);

 private:
  void play(MenuSound sound);
  struct Pending {
    MenuSound sound{};
    double remaining_seconds{};
  };
  audio::CueOutput& output_;
  static constexpr std::size_t kPendingCapacity = 32;
  std::array<Pending, kPendingCapacity> pending_{};
  std::size_t size_{};
};
}  // namespace bw::game
