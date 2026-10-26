// Original desktop character-load policy over borrowed Profile/storage/fade
// owners. Startup requires all saved files, accepts the original zeroed empty
// checkpoint, and creates content only for missing System or an affirmative
// overwrite decision. No means retry the desktop storage lookup; it never
// silently continues with saving disabled. Creation/load notices use original
// mem identities and the shared native dialog. Storage failures stay pending.
// Main-thread only; all borrows outlive this owner. O(1) frame state outside
// bounded profile I/O. Core I.1 (#ri-explicit), performance MEM.9 (guidelines/
// memory/MEM.9-embedded-allocate-at-init.md), read for design/implementation,
// expose outcomes and retain inline policy. See frontend-profile-load.md.
#pragma once
#include <functional>

#include "game/profile/profile.hpp"
#include "game/ui/common/menu_dialog.hpp"
#include "startup_flows.hpp"

namespace bw::game {
enum class ProfileLoadDialog : std::uint8_t {
  none,
  loading,
  creating,
  load_failed,
  overwrite,
  created,
  enable_autosave
};
struct ProfileLoadPresentation {
  ProfileLoadDialog identity{ProfileLoadDialog::none};
  MenuDialogPresentation dialog;
  bool acknowledgment{true};
  bool interactive{};
};
class ProfileLoadFlow {
 public:
  ProfileLoadFlow(Profile& profile, platform::SaveStorage& storage,
                  LogoMedia& media);
  void start();
  void update(LogoUpdate input, bool resources_ready);
  ScreenProgress progress() const { return progress_; }
  ProfileLoadPresentation presentation() const;
  MenuSoundRequests take_sounds();
  bool error_pending() const;
  ProfileResult error() const { return error_; }

 private:
  enum class Phase : std::uint8_t {
    resources,
    revealing,
    lookup,
    loading,
    creating,
    dialog,
    conceal,
    closing,
    finish
  };
  void open_dialog(ProfileLoadDialog identity);
  void begin_progress(ProfileLoadDialog identity);
  void collect_sounds();
  void finish_dialog();
  std::reference_wrapper<Profile> profile_;
  std::reference_wrapper<platform::SaveStorage> storage_;
  std::reference_wrapper<LogoMedia> media_;
  Phase phase_{Phase::resources};
  ScreenProgress progress_;
  ProfileResult error_;
  ProfileLoadDialog identity_{ProfileLoadDialog::none};
  MenuDialog dialog_;
  MenuSoundRequests sounds_;
  float elapsed_{};
  bool create_pending_{};
};
}  // namespace bw::game
