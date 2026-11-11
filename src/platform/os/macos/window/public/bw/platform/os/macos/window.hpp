#ifndef BW_PLATFORM_MACOS_WINDOW_HPP
#define BW_PLATFORM_MACOS_WINDOW_HPP
#include "bw/platform/window/window.hpp"

#include <cstdint>
#include <memory>

// Responsibility: the macOS adapter of bw/platform/window/window.hpp: an
// NSWindow whose content view hosts a CAMetalLayer, the AppKit event pump,
// and the window delegate that turns resize, close, focus and fullscreen
// notifications into WindowEvents. Objective-C++ under the platform
// boundary; nothing above it includes AppKit.
//
// Contract.
//   create_window runs on the main thread (NotMainThread otherwise). The
//   first call sets the process up as a regular application that can own
//   windows and receive events; later calls reuse it. The window is a
//   titled, closable, miniaturizable, resizable window whose content view
//   hosts a layer with the window's backing scale as its contents scale;
//   the view and the layer resize with the window. The window is shown and
//   the OS is asked to activate the application and make it key before
//   create_window returns; the OS grants that at its discretion
//   (window.hpp, Activation). A description with mode
//   BorderlessFullscreen is created windowed and then asked for fullscreen,
//   so the first ModeChanged and Resized events report the transition.
//   pump dequeues every pending application event and sends it to the
//   application; the delegate's notifications push events as the contract
//   of window.hpp states: a resize or a backing scale change pushes Resized
//   with the content view's size in pixels; the close button and the
//   application's quit request push CloseRequested and the window stays
//   open; key window gain and loss push FocusChanged; entering and leaving
//   fullscreen push ModeChanged then Resized.
//   set_mode toggles the window's own fullscreen, with window.hpp's three
//   results: None, ModeChangePending while a toggle it sent has not
//   completed or failed, ModeUnsupported while the window is on no display.
//   Input (window.hpp, Input): the content view accepts first responder
//   and is made it at creation, tracks the pointer over its visible rect
//   while the window is key, and turns the mouse, scroll and key events it
//   receives into the contract's events through the three pure functions
//   below, each a unit test; the adapter keeps the held buttons from the
//   down and up events it saw, reports button numbers 0, 1 and 2 only, and
//   drops the OS's key auto-repeats.
//   The destructor detaches the delegate, closes the window and releases
//   it; the layer stays alive while a viewport retains it.
//
// The layer is created by the window, not by the viewport, so the RHI never
// includes AppKit. The window reports its own size and scale and nothing
// else reads display metrics, so display reconfiguration is not observed.
namespace bw::platform::macos {

// The virtual key codes of the keys the contract names, as the OS reports
// them in a key event's code: layout-independent for Escape and the arrows,
// positional on a US layout for the letters and punctuation (window.hpp,
// Input).
inline constexpr std::uint16_t kVirtualKeyEscape = 0x35;
inline constexpr std::uint16_t kVirtualKeyR = 0x0F;
inline constexpr std::uint16_t kVirtualKeyP = 0x23;
inline constexpr std::uint16_t kVirtualKeyW = 0x0D;
inline constexpr std::uint16_t kVirtualKeyA = 0x00;
inline constexpr std::uint16_t kVirtualKeyS = 0x01;
inline constexpr std::uint16_t kVirtualKeyD = 0x02;
inline constexpr std::uint16_t kVirtualKeyLeftArrow = 0x7B;
inline constexpr std::uint16_t kVirtualKeyRightArrow = 0x7C;
inline constexpr std::uint16_t kVirtualKeyUpArrow = 0x7E;
inline constexpr std::uint16_t kVirtualKeyDownArrow = 0x7D;
inline constexpr std::uint16_t kVirtualKeyEqual = 0x18;
inline constexpr std::uint16_t kVirtualKeyMinus = 0x1B;
inline constexpr std::uint16_t kVirtualKeyLeftBracket = 0x21;
inline constexpr std::uint16_t kVirtualKeyRightBracket = 0x1E;

// The fifteen codes above to their Key; any other code is Key::Other.
[[nodiscard]] constexpr Key key_from_virtual_code(std::uint16_t code) noexcept {
  switch (code) {
  case kVirtualKeyEscape:
    return Key::Escape;
  case kVirtualKeyR:
    return Key::R;
  case kVirtualKeyP:
    return Key::P;
  case kVirtualKeyW:
    return Key::W;
  case kVirtualKeyA:
    return Key::A;
  case kVirtualKeyS:
    return Key::S;
  case kVirtualKeyD:
    return Key::D;
  case kVirtualKeyLeftArrow:
    return Key::Left;
  case kVirtualKeyRightArrow:
    return Key::Right;
  case kVirtualKeyUpArrow:
    return Key::Up;
  case kVirtualKeyDownArrow:
    return Key::Down;
  case kVirtualKeyEqual:
    return Key::Equal;
  case kVirtualKeyMinus:
    return Key::Minus;
  case kVirtualKeyLeftBracket:
    return Key::LeftBracket;
  case kVirtualKeyRightBracket:
    return Key::RightBracket;
  default:
    return Key::Other;
  }
}

// A point in the content view's coordinates (points, origin at the view's
// bottom-left corner, y up) to the contract's PixelPoint: y measured from
// the top as view_height - y, both scaled by backing_scale and rounded to
// the nearest integer, halves away from zero. The result may be negative
// or past the view's size: a held button keeps reporting a pointer that
// has left the view.
[[nodiscard]] constexpr PixelPoint pixel_point_from_view(double x, double y, double view_height,
                                                         double backing_scale) noexcept {
  const auto round = [](double value) {
    return static_cast<std::int32_t>(value + (value >= 0.0 ? 0.5 : -0.5));
  };
  return PixelPoint{round(x * backing_scale), round((view_height - y) * backing_scale)};
}

// A scroll event's vertical delta to the contract's lines: a precise delta
// is in points and is divided by kScrollPointsPerLine; a coarse one is in
// lines already (window.hpp, Input). The adapter does not report a result
// of zero.
[[nodiscard]] constexpr float scroll_lines(double delta_y, bool precise) noexcept {
  return static_cast<float>(precise ? delta_y / kScrollPointsPerLine : delta_y);
}

// Null with `error` set on failure; `error` is None on success.
[[nodiscard]] std::unique_ptr<platform::Window> create_window(const WindowDescription& description,
                                                              WindowError& error);

} // namespace bw::platform::macos
#endif
