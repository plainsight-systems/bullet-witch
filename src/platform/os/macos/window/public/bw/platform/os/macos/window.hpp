#ifndef BW_PLATFORM_MACOS_WINDOW_HPP
#define BW_PLATFORM_MACOS_WINDOW_HPP
#include "bw/platform/window/window.hpp"

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
//   The destructor detaches the delegate, closes the window and releases
//   it; the layer stays alive while a viewport retains it.
//
// The layer is created by the window, not by the viewport, so the RHI never
// includes AppKit. The window reports its own size and scale and nothing
// else reads display metrics, so display reconfiguration is not observed.
namespace bw::platform::macos {

// Null with `error` set on failure; `error` is None on success.
[[nodiscard]] std::unique_ptr<platform::Window> create_window(const WindowDescription& description,
                                                              WindowError& error);

} // namespace bw::platform::macos
#endif
