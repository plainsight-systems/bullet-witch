// Mission instruction ownership: resolve the purchased namespace/message tree,
// retain all authored English pages, advance only on acknowledgment, and retire
// after the shared dialog closes. Text/table/namespace/output/audio are
// borrowed and must outlive this stable single-thread owner. Only output/audio
// mutation capabilities are admitted; no world-clock or mission authority is
// retained. Missing text fails before activation. Start lookup is O(text
// records); frame updates use fixed state and allocate nothing. Core R.4
// (#rr-ref) makes borrows explicit; performance MEM.6 (guidelines/memory/
// MEM.6-inject-allocators-behind-one-interface.md) avoids hidden heap storage.
// Source and page semantics: mission-startup-source-trace.md.
#pragma once
#include "bw/content/formats/text_table.hpp"
#include "game/ui/common/menu_dialog.hpp"
namespace bw::game {
class MissionMessageOutput {
 public:
  MissionMessageOutput() = default;
  virtual ~MissionMessageOutput() = default;
  MissionMessageOutput(const MissionMessageOutput&) = delete;
  MissionMessageOutput& operator=(const MissionMessageOutput&) = delete;
  MissionMessageOutput(MissionMessageOutput&&) = delete;
  MissionMessageOutput& operator=(MissionMessageOutput&&) = delete;
  virtual void show(std::string_view text,
                    const MenuDialogPresentation& presentation) = 0;
  virtual void hide() noexcept = 0;
};
class MissionMessage {
 public:
  MissionMessage(const formats::TextTable& table, std::string_view name_space,
                 MissionMessageOutput& output, audio::CueOutput& audio);
  ~MissionMessage();
  MissionMessage(const MissionMessage&) = delete;
  MissionMessage& operator=(const MissionMessage&) = delete;
  MissionMessage(MissionMessage&&) = delete;
  MissionMessage& operator=(MissionMessage&&) = delete;
  void start(std::uint32_t index);
  void update(MenuInput input, float seconds);
  bool pending() const { return pending_; }
  void present();
  void cancel() noexcept;

 private:
  std::string_view page(std::size_t index) const;
  const formats::TextTable& table_;
  std::string_view namespace_;
  MissionMessageOutput& output_;
  MenuAudio audio_;
  MenuDialog dialog_;
  formats::TextLink pages_;
  std::size_t page_index_{};
  float gate_{};
  bool pending_{};
};
}  // namespace bw::game
