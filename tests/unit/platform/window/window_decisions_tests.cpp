// Unit test: the pure window decisions (bw/platform/window/window.hpp,
// validate_window_description, decide_mode_request, mode_request_error) and
// the macOS adapter's pure input conversions (bw/platform/os/macos/window.hpp,
// key_from_virtual_code, pixel_point_from_view, scroll_lines), one
// table-driven scenario per CTest entry.
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

// (macos/window.hpp, key_from_virtual_code, pixel_point_from_view,
// scroll_lines): the fifteen codes and one other; the view's top-left and
// bottom-right corners at scale 2, a point left of and above the view, and
// halves rounded away from zero both ways; a precise delta divided into
// lines and a coarse one taken as lines.
void macos_input_conversions() {
  namespace macos = bw::platform::macos;
  using bw::platform::Key;
  using bw::platform::PixelPoint;
  struct KeyRow {
    std::uint16_t code;
    Key key;
  };
  constexpr std::array keys{
      KeyRow{0x35, Key::Escape}, KeyRow{0x0F, Key::R},           KeyRow{0x23, Key::P},
      KeyRow{0x0D, Key::W},      KeyRow{0x00, Key::A},           KeyRow{0x01, Key::S},
      KeyRow{0x02, Key::D},      KeyRow{0x7B, Key::Left},        KeyRow{0x7C, Key::Right},
      KeyRow{0x7E, Key::Up},     KeyRow{0x7D, Key::Down},        KeyRow{0x18, Key::Equal},
      KeyRow{0x1B, Key::Minus},  KeyRow{0x21, Key::LeftBracket}, KeyRow{0x1E, Key::RightBracket},
      KeyRow{0x24, Key::Other},
  };
  for (std::size_t i = 0; i < keys.size(); ++i) {
    require(macos::key_from_virtual_code(keys[i].code) == keys[i].key, i, "the key of the code");
  }
  struct PointRow {
    double x, y, height, scale;
    PixelPoint expected;
    std::string_view note;
  };
  constexpr std::array points{
      PointRow{0.0, 100.0, 100.0, 2.0, PixelPoint{0, 0}, "top-left"},
      PointRow{160.0, 0.0, 100.0, 2.0, PixelPoint{320, 200}, "bottom-right"},
      PointRow{-3.0, 120.0, 100.0, 2.0, PixelPoint{-6, -40}, "left of and above the view"},
      PointRow{1.25, 100.25, 100.0, 2.0, PixelPoint{3, -1}, "halves away from zero"},
  };
  for (std::size_t i = 0; i < points.size(); ++i) {
    const PointRow& row = points[i];
    require(macos::pixel_point_from_view(row.x, row.y, row.height, row.scale) == row.expected, i,
            row.note);
  }
  require(macos::scroll_lines(20.0, true) == 2.0F, "a precise 20 points is 2 lines");
  require(macos::scroll_lines(3.0, false) == 3.0F, "a coarse 3 is 3 lines");
}

using Scenario = bw::testing::Scenario<void>;

constexpr std::array kScenarios{
    Scenario{"description_validation_order", &description_validation_order},
    Scenario{"mode_request_decision", &mode_request_decision},
    Scenario{"macos_input_conversions", &macos_input_conversions},
};

} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, kScenarios); }
