// Unit test: the pure window decisions (bw/platform/window/window.hpp,
// validate_window_description, decide_mode_request, mode_request_error), one
// table-driven scenario per CTest entry.
#include "bw/platform/window/window.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace {

using bw::platform::ModeRequest;
using bw::platform::WindowDescription;
using bw::platform::WindowError;
using bw::platform::WindowMode;

using bw::testing::require;

constexpr WindowMode kWindowed = WindowMode::Windowed;
constexpr WindowMode kFullscreen = WindowMode::BorderlessFullscreen;

// (window.hpp, validate_window_description): EmptyTitle, then ZeroSize (a
// zero width or height), in that order; the mode is not checked.
void description_validation_order() {
  struct Row {
    WindowDescription description;
    WindowError expected;
    std::string_view note;
  };
  constexpr std::array rows{
      Row{{"bw", 640, 360, kWindowed}, WindowError::None, "titled, non-zero size"},
      Row{{"bw", 640, 360, kFullscreen}, WindowError::None, "the mode is not checked"},
      Row{{"", 640, 360, kWindowed}, WindowError::EmptyTitle, "empty title"},
      Row{{"bw", 0, 360, kWindowed}, WindowError::ZeroSize, "zero width"},
      Row{{"bw", 640, 0, kWindowed}, WindowError::ZeroSize, "zero height"},
      Row{{"", 0, 0, kWindowed}, WindowError::EmptyTitle, "EmptyTitle before ZeroSize"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    require(bw::platform::validate_window_description(rows[i].description) == rows[i].expected, i,
            rows[i].note);
  }
}

// (window.hpp, decide_mode_request, mode_request_error): each outcome and its
// error, then the order TransitionPending, AlreadyInMode, NoDisplay, Toggle
// on inputs where more than one condition holds.
void mode_request_decision() {
  struct Row {
    bool pending;
    WindowMode current;
    WindowMode wanted;
    bool on_display;
    ModeRequest expected;
    WindowError error;
    std::string_view note;
  };
  constexpr std::array rows{
      Row{false, kWindowed, kFullscreen, true, ModeRequest::Toggle, WindowError::None,
          "a change on a display is Toggle"},
      Row{false, kFullscreen, kWindowed, true, ModeRequest::Toggle, WindowError::None,
          "leaving fullscreen is Toggle"},
      Row{false, kWindowed, kWindowed, true, ModeRequest::AlreadyInMode, WindowError::None,
          "the current mode is AlreadyInMode"},
      Row{true, kWindowed, kFullscreen, true, ModeRequest::TransitionPending,
          WindowError::ModeChangePending, "a transition in flight is TransitionPending"},
      Row{false, kWindowed, kFullscreen, false, ModeRequest::NoDisplay,
          WindowError::ModeUnsupported, "no display is NoDisplay"},
      Row{true, kWindowed, kWindowed, false, ModeRequest::TransitionPending,
          WindowError::ModeChangePending, "TransitionPending before AlreadyInMode and NoDisplay"},
      Row{true, kWindowed, kFullscreen, false, ModeRequest::TransitionPending,
          WindowError::ModeChangePending, "TransitionPending before NoDisplay"},
      Row{false, kFullscreen, kFullscreen, false, ModeRequest::AlreadyInMode, WindowError::None,
          "AlreadyInMode before NoDisplay"},
  };
  for (std::size_t i = 0; i < rows.size(); ++i) {
    const Row& row = rows[i];
    const ModeRequest decided =
        bw::platform::decide_mode_request(row.pending, row.current, row.wanted, row.on_display);
    require(decided == row.expected, i, row.note);
    require(bw::platform::mode_request_error(decided) == row.error, i, "the request's error");
  }
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"description_validation_order", &description_validation_order},
    Scenario{"mode_request_decision", &mode_request_decision},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
