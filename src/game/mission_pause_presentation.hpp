// Restricted mission pause presentation/resource-switch capability. Game policy
// selects a view; the presentation owner makes that one view resident before
// accepting copied draw values. Child asset switches are explicit transactions,
// reported to host pacing, not ordinary frame allocations. ResourceService and
// target output remain private to implementations. Prepared Help line counts
// are immutable content observations valid until the next view switch. Main
// thread only; failed activation throws and cannot claim readiness. Core I.1
// (#ri-explicit), E.6 (#re-raii), and performance MEM.9 (guidelines/memory/
// MEM.9-embedded-allocate-at-init.md) were read in design/implementation to
// separate bounded publication from owner setup/retirement. See
// native-pause-menu.md.
#pragma once
#include "pause_help.hpp"
#include "pause_menu.hpp"
#include "pause_options.hpp"

namespace bw::game {
enum class MissionPauseView : std::uint8_t {
  none,
  root,
  help,
  options,
  controls,
  costume,
  developer
};
class MissionPauseOutput {
 public:
  MissionPauseOutput() = default;
  virtual ~MissionPauseOutput() = default;
  MissionPauseOutput(const MissionPauseOutput&) = delete;
  MissionPauseOutput& operator=(const MissionPauseOutput&) = delete;
  MissionPauseOutput(MissionPauseOutput&&) = delete;
  MissionPauseOutput& operator=(MissionPauseOutput&&) = delete;
  // Returns whether a bounded resource transition occurred. Repeated admission
  // of the current view performs no work and does not clear visible commands.
  virtual bool activate(MissionPauseView view) = 0;
  virtual const std::array<unsigned, help_topic_count>& help_line_counts()
      const = 0;
  virtual void show(const PauseMenuPresentation& view) = 0;
  virtual void show(const PauseHelpPresentation& view) = 0;
  virtual void show(const PauseOptionsPresentation& view) = 0;
  virtual void show(const ControllerOptionsPresentation& view) = 0;
  virtual void show(const MenuPresentation& view) = 0;
};
}  // namespace bw::game
