#ifndef BW_PLATFORM_WINDOW_WINDOW_HPP
#define BW_PLATFORM_WINDOW_WINDOW_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>

// Responsibility: the platform-neutral window contract (docs/architecture/rhi.md,
// "Components and ownership": the window and its layer live under
// src/platform/os/<os> and are consumed by the backend's viewport; nothing
// above the platform boundary names an OS type). One window with a native
// surface in it, the event pump, the events the host reads (the window's
// own: resize, close, focus, mode; and the input a tool reads directly:
// mouse, scroll and a small key set), the mode toggle, and the native
// handle the RHI turns into a viewport (bw/rendering/rhi/viewport.hpp).
// The macOS adapter is selected by bw/platform/os/macos/window.hpp.
//
// Contract.
//   Threads: a window is created, pumped, read and destroyed on one thread,
//   the process main thread; the adapter never marshals to another thread.
//   The host's loop runs there.
//   Pump: pump() processes every OS event pending at the call and returns
//   without waiting, with one exception the OS imposes: a drag of the
//   window's frame or title bar runs the OS's own tracking loop inside the
//   event's dispatch, so that pump() returns when the drag ends; the host
//   presents no frame during the drag this slice, and the Resized the drag
//   produced is queued when pump() returns. pump() is the only call that
//   produces events; a notification the OS delivers outside pump() (during
//   create_window or set_mode) is queued in the order it arrived and
//   delivered by the next pump(). Events are read with poll_event in the
//   order they were queued (WindowEventQueue rules below). A window the OS
//   asks to close, by its close button or by a quit request to the
//   application, produces CloseRequested and stays open: closing is the
//   host's decision, by destroying the window.
//   Sizes: the description's width and height are the content area in
//   points; pixel_size() is the content area in pixels, points times the
//   backing scale, and every Resized event carries that pixel size. A
//   backing scale change produces a Resized event with the new pixel size.
//   The window never changes its surface's drawable size: that is the
//   viewport's, on the host's explicit resize.
//   Mode: Windowed is a titled, resizable, movable window with the standard
//   buttons; BorderlessFullscreen covers the display with no title bar.
//   set_mode requests the change and returns at once: None when the request
//   was issued or the window is in that mode already (then nothing is
//   produced); ModeChangePending while a transition it issued earlier has
//   not completed, so two transitions never overlap; ModeUnsupported when
//   the window is on no display, which is the one state in which the OS
//   cannot take the request. The transition is asynchronous: the window
//   produces ModeChanged with the new mode, then Resized with the new pixel
//   size, when it completes; when the OS refuses the transition after
//   set_mode returned, the window produces ModeRefused carrying the mode it
//   stays in, so a refusal is never silent. mode() reads the window's actual
//   state, which the user may also change through the window's own
//   controls, and the same events report that.
//   Input: the adapter produces four more kinds from the events it pumps.
//   Positions are in pixels of the content area, origin at its top-left
//   corner, x to the right and y down: the back buffer's and a dumped
//   image's row order, which the adapter converts to from the OS's view
//   coordinates and backing scale (PixelPoint). MouseMoved reports
//   the pointer's new position and the buttons held at that moment, with
//   or without a button held (the adapter tracks the pointer over the
//   whole content area while the window is key). MouseButton reports one
//   button pressed or released, with the position. Scrolled reports the
//   vertical wheel or trackpad delta in lines, with the OS's sign after
//   the user's scroll-direction preference: a wheel notch is one line, and
//   a device that reports precise deltas in points has them divided by
//   kScrollPointsPerLine, since the OS reports no line height. Key reports
//   one of the keys in Key pressed or released, by the key's position
//   (virtual key code), not the character it produces; a key not in the
//   set is Other, so a host that counts presses still sees them; a key the
//   OS auto-repeats produces one pressed event per physical press, not one
//   per repeat. Modifier keys, text input and horizontal scroll are not
//   reported (the plan's Scope: the input slice).
//   Activation: create_window shows the window and asks the OS to activate
//   the application and make the window key; the OS grants that at its
//   discretion (a process launched without an application bundle may not be
//   activated), and FocusChanged reports when it happens. Nothing here
//   waits for it.
//   Preconditions are the main thread's: create_window and set_mode return
//   NotMainThread off it; the other calls have no error channel and abort
//   with the call's name, because a window call from another thread is a
//   programming error, not a runtime condition.
//   Native handle: native_handle() names the surface the RHI renders into
//   and its kind; it is valid for the life of the window. The viewport
//   retains the surface, so destroying the window before the viewport is
//   allowed: presents then reach no display.
//   Failure: create_window (the OS header) returns a WindowError by name
//   and no window; set_mode returns ModeUnsupported for a mode the OS
//   cannot enter.
//
// The host runs its loop on the main thread, so there is one thread and
// nothing to marshal; a mode change is reported by the ModeChanged event,
// not waited for. There is no exclusive fullscreen on any target: the
// remaster runs at the display's native mode and renders its scene at a
// size of its own (viewport.hpp). A backing scale change produces a Resized
// event because the viewport's drawable size is set from the window's pixel
// size and a cached scale would leave it stale.
// Menus, cursor capture, text input, accessibility, multiple windows,
// remembering size and position between runs: not this slice (the plan's
// Scope); remembering is the settings slice. Keys are reported by position:
// the letter and punctuation codes name the keys at those positions on a
// US layout, and a layout that moves them is the input slice's (plan Scope,
// keyboard layouts).
// The OS reports a precise scroll delta in points and a coarse one in
// lines, and gives no line height; the points per line below are this
// contract's. Letter and punctuation key codes name key positions on a US
// keyboard; Escape and the arrows have layout-independent codes.
//
// Core guidelines read: C.122 (an abstract class at the platform boundary),
// R.1 and I.11 (the window is created into a unique_ptr and released by its
// destructor), I.1 (every input is in the description; the handle is an
// explicit value), E.27 (named error codes), Enum.3 (the button and key
// sets are class enums). Performance guidelines read: EMB.2 (the event
// queue is a fixed-capacity container; no allocation per event), MEM.9 (the
// adapter allocates at creation, not per pump), COPY.4 (an event is a plain
// struct copied by value).
namespace bw::platform {

enum class WindowMode : std::uint8_t { Windowed, BorderlessFullscreen };

struct PixelSize {
  std::uint32_t width{};
  std::uint32_t height{};
  [[nodiscard]] friend constexpr bool operator==(PixelSize, PixelSize) noexcept = default;
};

// A position in pixels of the content area, origin at its top-left corner,
// x to the right and y down; signed because a held button keeps reporting
// a pointer that has left the area.
struct PixelPoint {
  std::int32_t x{};
  std::int32_t y{};
  [[nodiscard]] friend constexpr bool operator==(PixelPoint, PixelPoint) noexcept = default;
};

// width and height are the content area in points. The title is copied at
// creation.
struct WindowDescription {
  std::string_view title{};
  std::uint32_t width{1280};
  std::uint32_t height{720};
  WindowMode mode{WindowMode::Windowed};
};

// The surface a viewport renders into. MetalLayer: `surface` is a
// CAMetalLayer the window's content view hosts, with no device set; the
// Metal viewport sets its device, pixel format, drawable size and vsync.
// Other kinds arrive with their backends' slices.
enum class NativeSurfaceKind : std::uint8_t { None, MetalLayer };
struct NativeWindowHandle {
  NativeSurfaceKind kind{NativeSurfaceKind::None};
  void* surface{};
};

enum class WindowEventKind : std::uint8_t {
  Resized,
  CloseRequested,
  FocusChanged,
  ModeChanged,
  // The OS refused a transition set_mode requested; mode is the mode the
  // window stays in.
  ModeRefused,
  // Input (the contract's Input paragraph).
  MouseMoved,
  MouseButton,
  Scrolled,
  Key,
};

enum class MouseButton : std::uint8_t { Left, Right, Middle };
// The bit of a button in WindowEvent::buttons: Left 1, Right 2, Middle 4.
[[nodiscard]] constexpr std::uint8_t mouse_button_bit(MouseButton button) noexcept {
  return static_cast<std::uint8_t>(1U << static_cast<unsigned>(button));
}

// The keys reported by name; every other key is Other. The letters, Equal,
// Minus and the brackets are the keys at those positions on a US layout.
enum class Key : std::uint8_t {
  Other,
  Escape,
  R,
  P,
  W,
  A,
  S,
  D,
  Left,
  Right,
  Up,
  Down,
  Equal,
  Minus,
  LeftBracket,
  RightBracket,
};

// Points of precise scroll travel that count as one line; a coarse wheel
// notch is one line as the OS reports it.
inline constexpr float kScrollPointsPerLine = 10.0F;

// Which fields a kind sets; the others are zero:
//   Resized                 size
//   ModeChanged, ModeRefused mode
//   FocusChanged            focused
//   MouseMoved              position, buttons
//   MouseButton             button, pressed, position
//   Scrolled                lines
//   Key                     key, pressed
// The nine constructors below are the only way an adapter builds an event,
// each setting its kind's fields and zeroing the others, so the rule holds
// by construction.
struct WindowEvent {
  WindowEventKind kind{WindowEventKind::Resized};
  PixelSize size{};
  WindowMode mode{WindowMode::Windowed};
  bool focused{};
  PixelPoint position{};
  // The buttons held, as mouse_button_bit bits.
  std::uint8_t buttons{};
  MouseButton button{MouseButton::Left};
  bool pressed{};
  // Vertical scroll in lines; see Input.
  float lines{};
  Key key{Key::Other};
  [[nodiscard]] friend constexpr bool operator==(const WindowEvent&,
                                                 const WindowEvent&) noexcept = default;
};
[[nodiscard]] constexpr WindowEvent resized_event(PixelSize size) noexcept {
  return WindowEvent{WindowEventKind::Resized, size, WindowMode::Windowed, false};
}
[[nodiscard]] constexpr WindowEvent close_requested_event() noexcept {
  return WindowEvent{WindowEventKind::CloseRequested, PixelSize{}, WindowMode::Windowed, false};
}
[[nodiscard]] constexpr WindowEvent focus_event(bool focused) noexcept {
  return WindowEvent{WindowEventKind::FocusChanged, PixelSize{}, WindowMode::Windowed, focused};
}
[[nodiscard]] constexpr WindowEvent mode_changed_event(WindowMode mode) noexcept {
  return WindowEvent{WindowEventKind::ModeChanged, PixelSize{}, mode, false};
}
[[nodiscard]] constexpr WindowEvent mode_refused_event(WindowMode mode) noexcept {
  return WindowEvent{WindowEventKind::ModeRefused, PixelSize{}, mode, false};
}
[[nodiscard]] constexpr WindowEvent mouse_moved_event(PixelPoint position,
                                                      std::uint8_t buttons) noexcept {
  WindowEvent event{};
  event.kind = WindowEventKind::MouseMoved;
  event.position = position;
  event.buttons = buttons;
  return event;
}
[[nodiscard]] constexpr WindowEvent mouse_button_event(MouseButton button, bool pressed,
                                                       PixelPoint position) noexcept {
  WindowEvent event{};
  event.kind = WindowEventKind::MouseButton;
  event.button = button;
  event.pressed = pressed;
  event.position = position;
  return event;
}
[[nodiscard]] constexpr WindowEvent scrolled_event(float lines) noexcept {
  WindowEvent event{};
  event.kind = WindowEventKind::Scrolled;
  event.lines = lines;
  return event;
}
[[nodiscard]] constexpr WindowEvent key_event(Key key, bool pressed) noexcept {
  WindowEvent event{};
  event.kind = WindowEventKind::Key;
  event.key = key;
  event.pressed = pressed;
  return event;
}

enum class WindowError : std::uint8_t {
  None,
  EmptyTitle,
  ZeroSize,
  // create_window or a window call on a thread other than the main thread.
  NotMainThread,
  // The OS refused the window or the application setup; the adapter
  // prints the OS's text.
  SystemRefused,
  // set_mode while the window is on no display.
  ModeUnsupported,
  // set_mode while a transition it issued earlier has not completed.
  ModeChangePending,
};

// The set_mode decision as a pure function the adapter calls, in this
// order: a transition still in flight is TransitionPending
// (ModeChangePending); the window already in the wanted mode is
// AlreadyInMode (None, nothing produced); a window on no display is
// NoDisplay (ModeUnsupported); otherwise Toggle (None, the toggle is sent).
enum class ModeRequest : std::uint8_t { Toggle, AlreadyInMode, TransitionPending, NoDisplay };
[[nodiscard]] constexpr ModeRequest decide_mode_request(bool transition_pending, WindowMode current,
                                                        WindowMode wanted,
                                                        bool on_display) noexcept {
  if (transition_pending) {
    return ModeRequest::TransitionPending;
  }
  if (current == wanted) {
    return ModeRequest::AlreadyInMode;
  }
  if (!on_display) {
    return ModeRequest::NoDisplay;
  }
  return ModeRequest::Toggle;
}
[[nodiscard]] constexpr WindowError mode_request_error(ModeRequest request) noexcept {
  switch (request) {
  case ModeRequest::Toggle:
  case ModeRequest::AlreadyInMode:
    return WindowError::None;
  case ModeRequest::TransitionPending:
    return WindowError::ModeChangePending;
  case ModeRequest::NoDisplay:
    return WindowError::ModeUnsupported;
  }
  return WindowError::None;
}

// The pure part of create_window: EmptyTitle, then ZeroSize (a zero width
// or height), in that order; the OS checks follow in the adapter.
[[nodiscard]] constexpr WindowError
validate_window_description(const WindowDescription& d) noexcept {
  if (d.title.empty()) {
    return WindowError::EmptyTitle;
  }
  if (d.width == 0U || d.height == 0U) {
    return WindowError::ZeroSize;
  }
  return WindowError::None;
}

// The pure event buffer every adapter fills during pump() and the host
// drains with poll_event. Invariants, each a unit test:
//   1. Events pop in push order.
//   2. A Resized pushed while the newest queued event is a Resized replaces
//      it: consecutive resizes coalesce to the last size.
//   3. CloseRequested is sticky: the first push queues it and sets
//      close_requested(); later pushes of it are dropped; close_requested()
//      stays true until clear().
//   4. A full queue drops the pushed event and push returns false; a
//      dropped CloseRequested still sets close_requested().
//   5. A MouseMoved pushed while the newest queued event is a MouseMoved
//      replaces it: consecutive moves coalesce to the last position and
//      buttons. No other input kind coalesces: every button, scroll and
//      key event is its own entry, and a button event between two moves
//      keeps both moves.
// The capacity holds every event one pump can produce in the game's use:
// moves coalesce, and the buttons, notches and keys of one frame are few.
inline constexpr std::size_t kWindowEventCapacity = 64;

class WindowEventQueue {
public:
  [[nodiscard]] bool push(const WindowEvent& event) noexcept;
  [[nodiscard]] bool pop(WindowEvent& event) noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return count_; }
  [[nodiscard]] bool empty() const noexcept { return count_ == 0; }
  [[nodiscard]] bool close_requested() const noexcept { return close_requested_; }
  void clear() noexcept;

private:
  std::array<WindowEvent, kWindowEventCapacity> events_{};
  std::size_t head_{};
  std::size_t count_{};
  bool close_requested_{};
  bool close_queued_{};
};

class Window {
public:
  Window(const Window&) = delete;
  Window& operator=(const Window&) = delete;
  virtual ~Window() = default;

  // Processes every pending OS event and returns; produces the events
  // poll_event reads.
  virtual void pump() = 0;
  // The next queued event, in order; false when none is queued.
  [[nodiscard]] virtual bool poll_event(WindowEvent& event) noexcept = 0;
  // The content area in pixels.
  [[nodiscard]] virtual PixelSize pixel_size() const noexcept = 0;
  // Pixels per point of the display the window is on.
  [[nodiscard]] virtual float backing_scale() const noexcept = 0;
  [[nodiscard]] virtual WindowMode mode() const noexcept = 0;
  // Requests the mode; see Mode in the contract.
  [[nodiscard]] virtual WindowError set_mode(WindowMode mode) = 0;
  [[nodiscard]] virtual NativeWindowHandle native_handle() const noexcept = 0;

protected:
  Window() = default;
};

} // namespace bw::platform
#endif
