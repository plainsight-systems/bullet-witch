// Original nonmodal mission text owns only playback state and a read-only
// prepared text-table borrow. The table and its resource lease must outlive
// this main-thread owner and every published view. Control time is supplied
// by MissionScriptRuntime, including while world simulation is paused. No
// clock, gameplay authority, output device, or heap allocation is owned here.
// Resolve namespace/pages on start; each page follows original start/play/
// begin/end timing and linear opacity. Core R.4 (#rr-ref), performance MEM.9
// (guidelines/memory/MEM.9-embedded-allocate-at-init.md), design consulted.
#pragma once
#include "bw/content/formats/text_table.hpp"

namespace bw::game {
struct SystemMessageView {
  bool active{};
  std::uint32_t index{};
  float opacity{};
  std::string_view text;
};
class MissionSystemMessage final {
 public:
  explicit MissionSystemMessage(const formats::TextTable& table);
  ~MissionSystemMessage() = default;
  MissionSystemMessage(const MissionSystemMessage&) = delete;
  MissionSystemMessage& operator=(const MissionSystemMessage&) = delete;
  MissionSystemMessage(MissionSystemMessage&&) = delete;
  MissionSystemMessage& operator=(MissionSystemMessage&&) = delete;
  void start(std::uint32_t index);
  void cancel() noexcept;
  void advance(float control_seconds);
  SystemMessageView view() const;

 private:
  enum class Phase : std::uint8_t {
    initial,
    start_wait,
    begin_wait,
    playing,
    end_wait,
    done
  };
  void prepare_page();
  std::string_view page_text() const;
  const formats::TextTable& table_;
  formats::TextLink pages_;
  std::size_t page_{};
  std::uint32_t index_{};
  Phase phase_{Phase::done};
  float elapsed_{}, start_time_{}, play_remaining_{}, begin_remaining_{};
  float end_remaining_{}, opacity_{}, target_{};
};
}  // namespace bw::game
