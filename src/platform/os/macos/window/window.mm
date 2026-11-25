// The macOS window adapter (bw/platform/os/macos/window.hpp): an NSWindow
// whose content view hosts a CAMetalLayer, the AppKit event pump, the
// delegates that turn AppKit notifications into WindowEvents, and the
// content view's overrides that turn mouse, scroll and key events into the
// input kinds (window.hpp, Input) and into device samples (window.hpp,
// Device input), and the cursor capture with the game controller
// framework's raw mouse motion (window.hpp, Capture).
//
// Memory: compiled with ARC (-fobjc-arc, src/platform/CMakeLists.txt). The
// C++ window holds the NSWindow, its content view, the layer and the window
// delegate by strong references and drops them in its destructor; the layer
// lives on while a viewport holds its own retain.
//
// Events and the pump (window.hpp, Pump): the delegate methods run inside
// AppKit calls. While pump() runs they push straight into the queue. AppKit
// can also send a notification outside pump() (making the window key while
// create_window shows it, for one); such a notification becomes an event at
// once, with the values it reports at that moment, and goes into a second
// fixed-capacity queue of pending events in the order it arrived. The next
// pump() moves the pending events into the queue before it dispatches
// anything, so pump() stays the only call that produces events, two
// notifications of one kind stay two events, and the OS's order is kept. A
// full pending queue drops and reports exactly as the queue does.
//
// Frame drag (window.hpp, Pump): a drag of the window's frame or title bar
// runs AppKit's own tracking loop inside the event's dispatch (sendEvent:),
// so pump() returns when the drag ends; the host presents no frame during
// the drag, and the Resized the drag produced is queued when pump() returns:
// the drag's resize notifications arrive while pump() runs, so they go
// straight into the queue and coalesce to the last size.
//
// There is one thread: nothing blocks to flush rendering before a live
// resize, and the host resizes the viewport from the Resized event between
// frames. The window is shown at creation (window.hpp) and the layer is
// opaque, so an undrawn window shows the layer's empty contents for at most
// one frame. The host's loop runs on the main thread and pump() dequeues and
// sends the events itself.
//
// Device samples (window.hpp, Device input) go into a WindowInputBuffer the
// host drains with poll_input. The adapter keeps its own held set for the
// samples, separate from the buttons the MouseMoved events carry, and pushes
// a key or button sample only when that set changes: a repeat changes
// nothing, and a release that arrives after the window stopped being key
// (when every held element was already reported up) changes nothing either.
// Raw mouse motion arrives on the main queue, the game controller
// framework's default handler queue, so it is added on the main thread like
// every other sample.
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <GameController/GameController.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bw/platform/input/device_input.hpp"
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <optional>
#include <string_view>

namespace bw::platform::macos {
namespace {
class MacWindow;
} // namespace
} // namespace bw::platform::macos

// The window's delegate: each notification the contract names is forwarded
// to the C++ window, which pushes or records it. `owner` is cleared by the
// window's destructor before the delegate is detached.
@interface BWWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic, assign) bw::platform::macos::MacWindow* owner;
- (void)requestClose;
@end

// The application's delegate, one per process, set on the first
// create_window.
@interface BWApplicationDelegate : NSObject <NSApplicationDelegate>
@end

// The content view: it hosts the layer and receives the window's mouse,
// scroll and key events, each forwarded to the C++ window. It accepts first
// responder, so key events reach it once the window makes it first
// responder, and it keeps one tracking area over its visible rectangle so
// moves arrive with no button held. `owner` is cleared by the window's
// destructor before the view is released.
@interface BWContentView : NSView
@property(nonatomic, assign) bw::platform::macos::MacWindow* owner;
@end

namespace bw::platform::macos {
namespace {

[[nodiscard]] bool on_main_thread() noexcept { return [NSThread isMainThread] == YES; }

// The virtual key codes of the keyboard elements (device_input.hpp) that
// macos/window.hpp does not already name: positions on a US layout, as the
// OS reports them in a key event's code. Left shift is a modifier: it sends
// no key down or up, only a modifier change carrying its code.
constexpr std::uint16_t kVirtualKeyE = 0x0E;
constexpr std::uint16_t kVirtualKeyZ = 0x06;
constexpr std::uint16_t kVirtualKeyX = 0x07;
constexpr std::uint16_t kVirtualKeyC = 0x08;
constexpr std::uint16_t kVirtualKeySpace = 0x31;
constexpr std::uint16_t kVirtualKeyLeftShift = 0x38;

// The device-dependent bit of an event's modifier flags that is set while
// the left shift key, and not the right, is down. The low sixteen bits of
// the flags are the device-dependent ones.
constexpr NSEventModifierFlags kDeviceLeftShiftFlag = 0x00000002;

// A key event's code to the keyboard element at that position; the codes
// that are no element give none. Left shift is reported from its modifier
// change instead (MacWindow::notice_flags).
[[nodiscard]] constexpr std::optional<InputElement>
keyboard_element_from_virtual_code(std::uint16_t code) noexcept {
  switch (code) {
  case kVirtualKeyW:
    return InputElement::KeyW;
  case kVirtualKeyA:
    return InputElement::KeyA;
  case kVirtualKeyS:
    return InputElement::KeyS;
  case kVirtualKeyD:
    return InputElement::KeyD;
  case kVirtualKeyE:
    return InputElement::KeyE;
  case kVirtualKeyR:
    return InputElement::KeyR;
  case kVirtualKeyZ:
    return InputElement::KeyZ;
  case kVirtualKeyX:
    return InputElement::KeyX;
  case kVirtualKeyC:
    return InputElement::KeyC;
  case kVirtualKeySpace:
    return InputElement::KeySpace;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] constexpr InputElement mouse_element(MouseButton button) noexcept {
  switch (button) {
  case MouseButton::Left:
    return InputElement::MouseLeft;
  case MouseButton::Right:
    return InputElement::MouseRight;
  case MouseButton::Middle:
    return InputElement::MouseMiddle;
  }
  return InputElement::MouseLeft;
}

// The bit of an element in the adapter's held set.
[[nodiscard]] constexpr std::uint32_t held_bit(InputElement element) noexcept {
  return std::uint32_t{1} << static_cast<unsigned>(element);
}
static_assert(kInputElementCount <= 32U, "the held set has one bit per element");

// window.hpp, Threads: every window call is on the main thread. Calls whose
// signature has no error channel stop the process by name instead of
// touching AppKit from another thread.
void require_main_thread(const char* call) noexcept {
  if (!on_main_thread()) {
    std::fprintf(stderr, "bw window: %s called off the main thread\n", call);
    std::abort();
  }
}

void print_refusal(const char* what, NSString* detail) {
  const char* text = detail != nil ? detail.UTF8String : nullptr;
  std::fprintf(stderr, "bw window: %s%s%s\n", what, text != nullptr ? ": " : "",
               text != nullptr ? text : "");
}

// The process as a regular application that owns windows and receives
// events, set up once: a Dock presence, the application delegate (held here,
// because NSApplication holds its delegate weakly), launch finished, and the
// application activated so the window can become key. The host's main loop
// is the application's loop.
[[nodiscard]] WindowError set_up_application() {
  static BWApplicationDelegate* const delegate = [] {
    [NSApplication sharedApplication];
    BWApplicationDelegate* created = [[BWApplicationDelegate alloc] init];
    if ([NSApp setActivationPolicy:NSApplicationActivationPolicyRegular] == NO) {
      print_refusal("the application refused the regular activation policy", nil);
      return static_cast<BWApplicationDelegate*>(nil);
    }
    NSApp.delegate = created;
    [NSApp finishLaunching];
    [NSApp activate];
    return created;
  }();
  return delegate != nil ? WindowError::None : WindowError::SystemRefused;
}

[[nodiscard]] PixelSize backing_size(NSView* view) noexcept {
  const NSRect pixels = [view convertRectToBacking:view.bounds];
  return PixelSize{static_cast<std::uint32_t>(std::lround(pixels.size.width)),
                   static_cast<std::uint32_t>(std::lround(pixels.size.height))};
}

class MacWindow final : public platform::Window {
public:
  MacWindow() = default;
  MacWindow(const MacWindow&) = delete;
  MacWindow& operator=(const MacWindow&) = delete;
  MacWindow(MacWindow&&) = delete;
  MacWindow& operator=(MacWindow&&) = delete;
  ~MacWindow() override;

  [[nodiscard]] WindowError create(const WindowDescription& description);

  void pump() override;
  [[nodiscard]] bool poll_event(WindowEvent& event) noexcept override;
  [[nodiscard]] PixelSize pixel_size() const noexcept override;
  [[nodiscard]] float backing_scale() const noexcept override;
  [[nodiscard]] WindowMode mode() const noexcept override;
  [[nodiscard]] WindowError set_mode(WindowMode mode) override;
  [[nodiscard]] NativeWindowHandle native_handle() const noexcept override;
  [[nodiscard]] bool poll_input(InputSample& sample) noexcept override;
  [[nodiscard]] bool set_cursor_captured(bool captured) override;
  [[nodiscard]] bool cursor_captured() const noexcept override;

  // From the delegates.
  void notice_resized();
  void notice_backing_changed();
  void notice_focus(bool focused);
  void notice_mode(WindowMode mode);
  void notice_mode_refused();
  void notice_close();

  // From the content view (window.hpp, Input).
  void notice_mouse_button(NSEvent* event, bool pressed);
  void notice_mouse_moved(NSEvent* event);
  void notice_scroll(NSEvent* event);
  void notice_key(NSEvent* event, bool pressed);
  // A modifier change (window.hpp, Device input: KeyLeftShift).
  void notice_flags(NSEvent* event);
  // From a mouse's raw motion handler while captured: the device's deltas,
  // y up.
  void notice_raw_motion(float delta_x, float delta_y);

private:
  // Into the queue while pump() runs, into the pending queue otherwise.
  void push(const WindowEvent& event);
  // The pending events into the queue, in arrival order, then the pending
  // queue cleared.
  void push_pending();
  // Sends the full-screen toggle and records the transition as in flight.
  void request_toggle();
  // Records an element of the held set down or up and pushes its sample
  // when that changes the set; a sample the full buffer drops leaves the
  // set as it was.
  void set_held(InputElement element, bool down);
  // A sample into the buffer; a full buffer drops it, reported once a pump.
  bool push_input(const InputSample& sample);
  // The application's resign and the wake observers, added at creation and
  // removed by the destructor.
  void observe_application();
  // The pump's unread samples discarded, then every held element reported
  // up and the held set emptied.
  void release_held();
  // The capture ended (see the capture state below): the handlers unbound,
  // then the reconnect and the show still owed issued; true when the mouse
  // is connected and the cursor shown.
  bool release_capture();
  // On wake: the handlers unbound and the mouse reconnected; the cursor
  // left as it is.
  void wake_release();
  // One OS step; a refusal is one stderr line naming it. True when taken.
  static bool os_step(CGError result, const char* step);
  // The raw motion handler on every connected mouse, and on each that
  // connects while captured; or every handler and the observer removed.
  void bind_mice();
  void unbind_mice();

  NSWindow* window_{nil};
  BWContentView* view_{nil};
  CAMetalLayer* layer_{nil};
  BWWindowDelegate* delegate_{nil};
  WindowEventQueue queue_{};
  // Notifications AppKit sent outside pump(), as events, in arrival order.
  WindowEventQueue pending_{};
  bool pumping_{};
  // A full-screen transition this window requested has not completed:
  // set when the toggle is sent, cleared when AppKit reports the transition
  // entered, exited or failed.
  bool transition_pending_{};
  // The buttons held, as mouse_button_bit bits, from the down and up events
  // this view received.
  std::uint8_t buttons_{};
  // Device samples for poll_input (window.hpp, Device input).
  WindowInputBuffer input_{};
  // The keyboard and mouse elements down, as held_bit bits, as the samples
  // reported them.
  std::uint32_t held_{};
  // The capture state (window.hpp, Capture): three flags that record what
  // is true, so each call issues only the OS steps still owed and none
  // twice. A refused step is one stderr line and leaves its flag as it
  // was; nothing is retried on its own.
  //   capturing_     the raw-motion handlers are bound and
  //                  cursor_captured() reads true. Set by
  //                  set_cursor_captured(true) once disconnected_ and
  //                  hidden_ both hold; cleared, with the handlers unbound,
  //                  by every release and by a wake.
  //   disconnected_  the mouse is disconnected from the cursor: set when
  //                  the OS takes a capture's disconnect, cleared when it
  //                  takes a reconnect (a release or a wake).
  //   hidden_        this window hid the cursor: set when the OS takes a
  //                  capture's hide, cleared when it takes a release's
  //                  show. A wake leaves it.
  // The paths, against window.hpp, Capture:
  //   host capture   set_cursor_captured(true) on a key window: disconnect
  //                  unless disconnected_; warp a pointer outside the
  //                  content area inside (a refusal is not fatal); hide
  //                  unless hidden_; bind and set capturing_ when both hold.
  //                  Returns capturing_. Not key: false, nothing done.
  //   release        set_cursor_captured(false), key loss (notice_focus),
  //                  the application about to resign (observer), the
  //                  destructor: release_capture: unbind, clear
  //                  capturing_; reconnect if disconnected_; show if
  //                  hidden_. Returns whether both flags are clear.
  //   wake           screens or system did wake: wake_release: unbind,
  //                  clear capturing_; reconnect if disconnected_; hidden_
  //                  left.
  bool capturing_{};
  bool disconnected_{};
  bool hidden_{};
  // The observer of mouse connections while captured; nil otherwise.
  id mouse_connected_observer_{nil};
  // The observers of the application about to resign active and of the
  // screen's and the system's wake (window.hpp, Capture).
  id resign_active_observer_{nil};
  id screens_wake_observer_{nil};
  id system_wake_observer_{nil};
  // A full input buffer has been reported in this pump.
  bool input_full_reported_{};

  // An event's position in pixels of the content area, top-left origin.
  [[nodiscard]] PixelPoint pixel_point(NSEvent* event) const;
};

// The layer is assigned before wantsLayer is set, which makes the view
// layer-hosting with this layer as its root. AppKit does not keep a hosted
// layer's contentsScale current, so the window sets it to the backing scale
// here and on every backing change; the drawable size stays the viewport's
// (window.hpp, Sizes).
// The window is a normal opaque window whose content view reports input
// (BWContentView); the view is made first responder once the window is
// shown, so key events reach it. The layer is the view's root and fills
// it, its other properties keep their defaults, and its device and
// framebufferOnly are the viewport's. The delegate's windowShouldClose:
// reports a close. The window is centered; remembering placement is the
// settings slice.
WindowError MacWindow::create(const WindowDescription& description) {
  @autoreleasepool {
    NSString* title = [[NSString alloc] initWithBytes:description.title.data()
                                               length:description.title.size()
                                             encoding:NSUTF8StringEncoding];
    if (title == nil) {
      print_refusal("the title is not UTF-8 text", nil);
      return WindowError::SystemRefused;
    }
    const NSRect content = NSMakeRect(0.0, 0.0, static_cast<CGFloat>(description.width),
                                      static_cast<CGFloat>(description.height));
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    window_ = [[NSWindow alloc] initWithContentRect:content
                                          styleMask:style
                                            backing:NSBackingStoreBuffered
                                              defer:NO];
    if (window_ == nil) {
      print_refusal("AppKit refused the window", nil);
      return WindowError::SystemRefused;
    }
    // Under ARC the window must not release itself on close.
    window_.releasedWhenClosed = NO;
    window_.title = title;
    window_.collectionBehavior =
        NSWindowCollectionBehaviorFullScreenPrimary | NSWindowCollectionBehaviorDefault |
        NSWindowCollectionBehaviorManaged | NSWindowCollectionBehaviorParticipatesInCycle;
    window_.restorable = NO;
    [window_ disableSnapshotRestoration];

    layer_ = [CAMetalLayer layer];
    layer_.contentsScale = window_.backingScaleFactor;
    layer_.opaque = YES;
    layer_.magnificationFilter = kCAFilterNearest;
    layer_.minificationFilter = kCAFilterNearest;
    layer_.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    [layer_ removeAllAnimations];
    view_ = [[BWContentView alloc] initWithFrame:content];
    view_.owner = this;
    view_.layer = layer_;
    view_.wantsLayer = YES;
    view_.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    window_.contentView = view_;

    delegate_ = [[BWWindowDelegate alloc] init];
    delegate_.owner = this;
    window_.delegate = delegate_;

    observe_application();

    [window_ center];
    [window_ makeKeyAndOrderFront:nil];
    [window_ makeFirstResponder:view_];
    // The same request set_mode makes, so a set_mode during this transition
    // is ModeChangePending (window.hpp, Mode).
    if (description.mode == WindowMode::BorderlessFullscreen) {
      request_toggle();
    }
  }
  return WindowError::None;
}

// The delegate is detached first so nothing reaches this object while the
// window goes; the window is then closed, which takes it off screen, and
// released. close does not ask the delegate (windowShouldClose: is
// performClose:'s), and the window does not release itself on close
// (releasedWhenClosed is NO), so the reference dropped here is the last.
// The window is closed at once and no frame is presented into it
// afterwards, so there is no interval in which it could show anything.
// window.hpp, Capture: destroying the window releases its capture, first,
// while the view still reaches this object.
MacWindow::~MacWindow() {
  @autoreleasepool {
    static_cast<void>(release_capture());
    if (resign_active_observer_ != nil) {
      [NSNotificationCenter.defaultCenter removeObserver:resign_active_observer_];
    }
    for (id observer : {screens_wake_observer_, system_wake_observer_}) {
      if (observer != nil) {
        [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:observer];
      }
    }
    delegate_.owner = nullptr;
    view_.owner = nullptr;
    window_.delegate = nil;
    [window_ close];
    window_ = nil;
    view_ = nil;
    layer_ = nil;
    delegate_ = nil;
  }
}

// window.hpp, Pump: pending notifications first, then every event pending at
// the call, dequeued without waiting and sent to the application.
// window.hpp, Device input: samples the host has not read are discarded
// when the pump begins.
void MacWindow::pump() {
  require_main_thread("pump");
  input_.clear();
  input_full_reported_ = false;
  push_pending();
  pumping_ = true;
  @autoreleasepool {
    // A frame drag's event does not return from sendEvent: until the drag
    // ends: AppKit runs its tracking loop inside the dispatch (window.hpp,
    // Pump). Its resize notifications arrive with pumping_ set and are
    // queued for the return.
    for (;;) {
      NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                          untilDate:[NSDate distantPast]
                                             inMode:NSDefaultRunLoopMode
                                            dequeue:YES];
      if (event == nil) {
        break;
      }
      [NSApp sendEvent:event];
    }
    [NSApp updateWindows];
  }
  pumping_ = false;
}

bool MacWindow::poll_event(WindowEvent& event) noexcept {
  require_main_thread("poll_event");
  return queue_.pop(event);
}

NativeWindowHandle MacWindow::native_handle() const noexcept {
  require_main_thread("native_handle");
  return NativeWindowHandle{NativeSurfaceKind::MetalLayer, (__bridge void*)layer_};
}

// Points times the backing scale, as AppKit converts the content view's
// bounds to the backing store.
PixelSize MacWindow::pixel_size() const noexcept {
  require_main_thread("pixel_size");
  return backing_size(view_);
}

float MacWindow::backing_scale() const noexcept {
  require_main_thread("backing_scale");
  return static_cast<float>(window_.backingScaleFactor);
}

// The window's actual state: its style mask carries the full-screen bit
// while it is in its own full screen.
WindowMode MacWindow::mode() const noexcept {
  require_main_thread("mode");
  return (window_.styleMask & NSWindowStyleMaskFullScreen) != 0 ? WindowMode::BorderlessFullscreen
                                                                : WindowMode::Windowed;
}

// window.hpp, Mode: a request through the window's own full-screen toggle;
// the delegate reports completion. In order: NotMainThread before anything
// reads AppKit; ModeChangePending while a transition this window requested
// is in flight, decided before the mode because the style mask changes
// during a transition; None when the window is in the mode already; ModeUnsupported
// when the window is on no display; otherwise the toggle is sent and None
// returned (window.hpp, decide_mode_request and mode_request_error).
// There is no exclusive fullscreen (window.hpp): the window's own full
// screen is the one full-screen mode. Nothing waits for the transition;
// ModeChanged reports it (window.hpp). This window sets no size limits, so
// none need widening before the toggle.
WindowError MacWindow::set_mode(WindowMode mode) {
  if (!on_main_thread()) {
    return WindowError::NotMainThread;
  }
  // A transition in flight is not waited for: this call returns at once and
  // names the wait, so two transitions never overlap.
  // The decision and its order are window.hpp's decide_mode_request; a
  // transition in flight is decided before the mode is compared because the
  // style mask changes during a transition.
  const ModeRequest request =
      decide_mode_request(transition_pending_, this->mode(), mode, window_.screen != nil);
  if (request == ModeRequest::Toggle) {
    request_toggle();
  }
  return mode_request_error(request);
}

// The flag is set before the toggle is sent, so a refusal AppKit reports
// from inside toggleFullScreen: clears it rather than being overwritten.
void MacWindow::request_toggle() {
  transition_pending_ = true;
  [window_ toggleFullScreen:nil];
}

void MacWindow::push(const WindowEvent& event) {
  if (pumping_) {
    if (!queue_.push(event)) {
      std::fprintf(stderr, "bw window: event queue full, event of kind %u dropped\n",
                   static_cast<unsigned>(event.kind));
    }
    return;
  }
  if (!pending_.push(event)) {
    std::fprintf(stderr, "bw window: pending event queue full, event of kind %u dropped\n",
                 static_cast<unsigned>(event.kind));
  }
}

void MacWindow::push_pending() {
  WindowEvent event{};
  while (pending_.pop(event)) {
    if (!queue_.push(event)) {
      std::fprintf(stderr, "bw window: event queue full, event of kind %u dropped\n",
                   static_cast<unsigned>(event.kind));
    }
  }
  pending_.clear();
}

// A resize reaches the renderer as the content size in pixels. Nothing in
// the game reads the window's position or a resize in progress, so neither
// is reported.
void MacWindow::notice_resized() { push(resized_event(pixel_size())); }

// window.hpp, Sizes: a backing scale change produces Resized with the new
// pixel size, after the hosted layer's contents scale follows it.
void MacWindow::notice_backing_changed() {
  layer_.contentsScale = window_.backingScaleFactor;
  notice_resized();
}

// The key window is the one that receives keyboard input, which is what
// focus means to the game; for a single window it is also the main window.
// window.hpp, Device input and Capture: a window that stops being key
// reports every held key and button up and releases its capture; the host
// captures again when it reads the window key. The application's
// resignation releases it before that (observe_application); this release
// covers a window that loses key status while the application stays
// active.
void MacWindow::notice_focus(bool focused) {
  push(focus_event(focused));
  if (!focused) {
    static_cast<void>(release_capture());
    release_held();
  }
}

// The mode is recorded when the transition completes, and the completion
// reaches the renderer as a resize. The transition in flight ends here,
// when AppKit reports it, not when the event is read.
void MacWindow::notice_mode(WindowMode mode) {
  transition_pending_ = false;
  push(mode_changed_event(mode));
  push(resized_event(pixel_size()));
}

// window.hpp, Mode: AppKit refused a full-screen transition the window had
// started (entering or leaving); the event carries the mode the window
// stays in, read from its actual state, so a refusal is never silent. The
// refused transition is no longer in flight.
void MacWindow::notice_mode_refused() {
  transition_pending_ = false;
  push(mode_refused_event(mode()));
}

// The close button queues a close for the game to decide; the window stays
// open.
void MacWindow::notice_close() { push(close_requested_event()); }

// window.hpp, Input: locationInWindow is in the window's base coordinates;
// converted to the view it is in points with the origin at the view's
// bottom-left corner (the view is not flipped), which pixel_point_from_view
// turns into the contract's pixels (macos/window.hpp). locationInWindow is
// valid for every mouse event, and convertPoint:fromView: with nil converts
// from the window's coordinates.
PixelPoint MacWindow::pixel_point(NSEvent* event) const {
  const NSPoint point = [view_ convertPoint:event.locationInWindow fromView:nil];
  return pixel_point_from_view(point.x, point.y, view_.bounds.size.height,
                               window_.backingScaleFactor);
}

// window.hpp, Input: button numbers 0, 1 and 2 are the left, right and
// middle buttons; others are not reported. The held set is kept from the
// down and up events, not read from the OS's current state
// (pressedMouseButtons), which is not suitable for tracking.
void MacWindow::notice_mouse_button(NSEvent* event, bool pressed) {
  MouseButton button = MouseButton::Left;
  switch (event.buttonNumber) {
  case 0:
    button = MouseButton::Left;
    break;
  case 1:
    button = MouseButton::Right;
    break;
  case 2:
    button = MouseButton::Middle;
    break;
  default:
    return;
  }
  const std::uint8_t bit = mouse_button_bit(button);
  buttons_ = pressed ? static_cast<std::uint8_t>(buttons_ | bit)
                     : static_cast<std::uint8_t>(buttons_ & static_cast<std::uint8_t>(~bit));
  push(mouse_button_event(button, pressed, pixel_point(event)));
  set_held(mouse_element(button), pressed);
}

// A move and a drag are both MouseMoved, with the buttons held.
void MacWindow::notice_mouse_moved(NSEvent* event) {
  push(mouse_moved_event(pixel_point(event), buttons_));
}

// window.hpp, Input: scroll_lines converts the delta (macos/window.hpp). A
// delta of zero lines (a horizontal-only scroll, or a phase change) is not
// reported. hasPreciseScrollingDeltas says whether scrollingDeltaY is in
// points or in lines.
void MacWindow::notice_scroll(NSEvent* event) {
  const float lines = scroll_lines(event.scrollingDeltaY, event.hasPreciseScrollingDeltas != NO);
  if (lines == 0.0F) {
    return;
  }
  push(scrolled_event(lines));
  // window.hpp, Device input: one wheel sample per wheel event, its lines,
  // in order with the key and button samples.
  static_cast<void>(push_input(InputSample{InputElement::MouseWheel, lines}));
}

// window.hpp, Input: one pressed event per physical press; the OS's
// auto-repeats are dropped.
// The key is named by key_from_virtual_code (macos/window.hpp) from
// keyCode, the device-independent key number.
void MacWindow::notice_key(NSEvent* event, bool pressed) {
  if (pressed && event.isARepeat) {
    return;
  }
  push(key_event(key_from_virtual_code(event.keyCode), pressed));
  // window.hpp, Device input: the same press, as a keyboard element's
  // sample.
  if (const std::optional<InputElement> element =
          keyboard_element_from_virtual_code(event.keyCode)) {
    set_held(*element, pressed);
  }
}

// window.hpp, Device input: KeyLeftShift from the modifier's own change. A
// modifier change carries the code of the key that changed; the left
// shift's device-dependent flag says whether it is now down, so a right
// shift held at the same time does not confuse the two. No WindowEvent:
// modifiers are not among the input events (window.hpp, Input).
void MacWindow::notice_flags(NSEvent* event) {
  if (event.keyCode != kVirtualKeyLeftShift) {
    return;
  }
  set_held(InputElement::KeyLeftShift, (event.modifierFlags & kDeviceLeftShiftFlag) != 0);
}

// window.hpp, Device input: motion is reported only while captured, x to
// the right and y down; the framework's y points up and is negated. The
// framework's deltas are raw, not affected by the mouse sensitivity
// settings.
void MacWindow::notice_raw_motion(float delta_x, float delta_y) {
  if (!capturing_) {
    return;
  }
  input_.add_motion(delta_x, -delta_y);
}

void MacWindow::set_held(InputElement element, bool down) {
  const std::uint32_t bit = held_bit(element);
  if (((held_ & bit) != 0U) == down) {
    return;
  }
  if (push_input(InputSample{element, down ? 1.0F : 0.0F})) {
    held_ = down ? (held_ | bit) : (held_ & ~bit);
  }
}

bool MacWindow::push_input(const InputSample& sample) {
  if (input_.push(sample)) {
    return true;
  }
  if (!input_full_reported_) {
    input_full_reported_ = true;
    std::fprintf(stderr, "bw window: input buffer full, sample of element %u dropped\n",
                 static_cast<unsigned>(sample.element));
  }
  return false;
}

// window.hpp, Device input: the pump's samples not yet read are discarded,
// so the releases of the held elements (at most the keyboard's and the
// mouse's buttons, fewer than the buffer holds) always fit.
void MacWindow::release_held() {
  input_.clear();
  for (std::size_t index = 0; index < kInputElementCount; ++index) {
    const auto element = static_cast<InputElement>(index);
    if ((held_ & held_bit(element)) != 0U) {
      static_cast<void>(push_input(InputSample{element, 0.0F}));
    }
  }
  held_ = 0U;
}

bool MacWindow::poll_input(InputSample& sample) noexcept {
  require_main_thread("poll_input");
  return input_.pop(sample);
}

bool MacWindow::cursor_captured() const noexcept {
  require_main_thread("cursor_captured");
  return capturing_;
}

// window.hpp, Capture: the mouse is disconnected from the cursor, a pointer
// outside the content area moved inside, then the cursor hidden; release
// reverses both, reconnecting first. A window that is not key cannot
// capture. Each step is issued only while owed (the capture state above).
// While the mouse is disconnected, events keep a constant location and
// carry deltas. The OS counts hides, so hidden_ pairs each hide with one
// show.
bool MacWindow::set_cursor_captured(bool captured) {
  require_main_thread("set_cursor_captured");
  if (!captured) {
    return release_capture();
  }
  if (capturing_) {
    return true;
  }
  if (window_.isKeyWindow == NO) {
    return false;
  }
  // Disconnected first, then the warp, so the warp does not suppress the
  // events that follow; the reconnect comes with the release. A capture
  // after a wake disconnects the mouse again, so the hidden cursor does not
  // move under the raw motion and the pointer stays where it is, as
  // window.hpp's Capture says.
  if (!disconnected_) {
    disconnected_ = os_step(CGAssociateMouseAndMouseCursorPosition(false), "disconnect");
  }
  // window.hpp, Capture: a pointer outside the content area is first moved
  // to the nearest point inside it. Everything is in the display's global
  // points, origin at the top-left of the first screen, where the content
  // area's last row and column are its top-left plus its size minus 1. A
  // refused warp is reported and the capture goes on.
  if (NSScreen.screens.count > 0) {
    const CGFloat screen_height = NSMaxY(NSScreen.screens[0].frame);
    const NSRect content = [window_ contentRectForFrameRect:window_.frame];
    const CGFloat left = NSMinX(content);
    const CGFloat top = screen_height - NSMaxY(content);
    const CGFloat right = left + NSWidth(content) - 1.0;
    const CGFloat bottom = top + NSHeight(content) - 1.0;
    const NSPoint mouse = NSEvent.mouseLocation;
    const CGFloat x = mouse.x;
    const CGFloat y = screen_height - mouse.y;
    const CGFloat inside_x = std::fmin(std::fmax(x, left), right);
    const CGFloat inside_y = std::fmin(std::fmax(y, top), bottom);
    if (inside_x != x || inside_y != y) {
      static_cast<void>(
          os_step(CGWarpMouseCursorPosition(CGPointMake(inside_x, inside_y)), "move"));
    }
  }
  if (!hidden_) {
    hidden_ = os_step(CGDisplayHideCursor(kCGNullDirectDisplay), "hide");
  }
  if (disconnected_ && hidden_) {
    capturing_ = true;
    bind_mice();
  }
  return capturing_;
}

// window.hpp, Capture: whoever asks (the host, the window's loss of key
// status, the application about to resign active, the destructor), the
// motion stops and cursor_captured() reads false; then the reconnect and
// the show still owed are issued. True when both are done.
bool MacWindow::release_capture() {
  if (capturing_) {
    capturing_ = false;
    unbind_mice();
  }
  if (disconnected_) {
    disconnected_ = !os_step(CGAssociateMouseAndMouseCursorPosition(true), "reconnect");
  }
  if (hidden_) {
    hidden_ = !os_step(CGDisplayShowCursor(kCGNullDirectDisplay), "show");
  }
  return !disconnected_ && !hidden_;
}

// window.hpp, Capture: on wake the mouse is reconnected and the motion
// stops; the cursor stays hidden, so the next capture hides nothing and the
// next release shows it once.
void MacWindow::wake_release() {
  if (capturing_) {
    capturing_ = false;
    unbind_mice();
  }
  if (disconnected_) {
    disconnected_ = !os_step(CGAssociateMouseAndMouseCursorPosition(true), "reconnect");
  }
}

bool MacWindow::os_step(CGError result, const char* step) {
  if (result == kCGErrorSuccess) {
    return true;
  }
  std::fprintf(stderr, "bw window: the OS refused to %s the cursor (error %d)\n", step,
               static_cast<int>(result));
  return false;
}

// window.hpp, Capture: the capture is released when the application is
// about to resign active; on the screen's and the system's wake the mouse
// is reconnected and its handlers unbound (wake_release); the host
// captures again on its next loop while the window is key.
void MacWindow::observe_application() {
  __weak BWContentView* weak_view = view_;
  resign_active_observer_ = [NSNotificationCenter.defaultCenter
      addObserverForName:NSApplicationWillResignActiveNotification
                  object:nil
                   queue:NSOperationQueue.mainQueue
              usingBlock:^(NSNotification* notification) {
                static_cast<void>(notification);
                BWContentView* view = weak_view;
                if (view != nil && view.owner != nullptr) {
                  static_cast<void>(view.owner->release_capture());
                }
              }];
  const auto wake = ^(NSNotification* notification) {
    static_cast<void>(notification);
    BWContentView* view = weak_view;
    if (view != nil && view.owner != nullptr) {
      view.owner->wake_release();
    }
  };
  NSNotificationCenter* workspace = NSWorkspace.sharedWorkspace.notificationCenter;
  screens_wake_observer_ = [workspace addObserverForName:NSWorkspaceScreensDidWakeNotification
                                                  object:nil
                                                   queue:NSOperationQueue.mainQueue
                                              usingBlock:wake];
  system_wake_observer_ = [workspace addObserverForName:NSWorkspaceDidWakeNotification
                                                 object:nil
                                                  queue:NSOperationQueue.mainQueue
                                             usingBlock:wake];
}

// Every connected mouse gets the raw motion handler, and a mouse that
// connects while captured gets it on connection. The handler reaches this
// window through the view's owner, which the destructor clears, never
// through `this`: a block already queued on the main queue can run after
// the window is gone. Handlers run on the device's handler queue, main by
// default, set here so the one-thread contract does not rest on a default.
void MacWindow::bind_mice() {
  __weak BWContentView* weak_view = view_;
  GCMouseMoved handler = ^(GCMouseInput* input, float delta_x, float delta_y) {
    static_cast<void>(input);
    BWContentView* view = weak_view;
    if (view != nil && view.owner != nullptr) {
      view.owner->notice_raw_motion(delta_x, delta_y);
    }
  };
  const auto bind = [handler](GCMouse* mouse) {
    GCMouseInput* input = mouse.mouseInput;
    if (input == nil) {
      return;
    }
    mouse.handlerQueue = dispatch_get_main_queue();
    input.mouseMovedHandler = handler;
  };
  for (GCMouse* mouse in GCMouse.mice) {
    bind(mouse);
  }
  mouse_connected_observer_ =
      [NSNotificationCenter.defaultCenter addObserverForName:GCMouseDidConnectNotification
                                                      object:nil
                                                       queue:NSOperationQueue.mainQueue
                                                  usingBlock:^(NSNotification* notification) {
                                                    id object = notification.object;
                                                    if ([object isKindOfClass:[GCMouse class]]) {
                                                      GCMouse* mouse = object;
                                                      bind(mouse);
                                                    }
                                                  }];
}

void MacWindow::unbind_mice() {
  if (mouse_connected_observer_ != nil) {
    [NSNotificationCenter.defaultCenter removeObserver:mouse_connected_observer_];
    mouse_connected_observer_ = nil;
  }
  for (GCMouse* mouse in GCMouse.mice) {
    mouse.mouseInput.mouseMovedHandler = nil;
  }
}

} // namespace

// macos/window.hpp, create_window: the description's pure checks first
// (window.hpp, validate_window_description), then the OS's: the main thread,
// the application and the window.
std::unique_ptr<platform::Window> create_window(const WindowDescription& description,
                                                WindowError& error) {
  error = validate_window_description(description);
  if (error != WindowError::None) {
    return nullptr;
  }
  if (!on_main_thread()) {
    error = WindowError::NotMainThread;
    return nullptr;
  }
  error = set_up_application();
  if (error != WindowError::None) {
    return nullptr;
  }
  auto window = std::make_unique<MacWindow>();
  error = window->create(description);
  if (error != WindowError::None) {
    return nullptr;
  }
  return window;
}

} // namespace bw::platform::macos

@implementation BWWindowDelegate

- (void)requestClose {
  if (self.owner != nullptr) {
    self.owner->notice_close();
  }
}

// window.hpp, Pump: the OS's close request produces CloseRequested and the
// window stays open.
- (BOOL)windowShouldClose:(NSWindow*)sender {
  static_cast<void>(sender);
  [self requestClose];
  return NO;
}

- (void)windowDidResize:(NSNotification*)notification {
  static_cast<void>(notification);
  if (self.owner != nullptr) {
    self.owner->notice_resized();
  }
}

// A backing scale change becomes a Resized event (window.hpp, Sizes).
// windowDidChangeScreen: is not observed: the window caches no screen, and
// what the viewport needs from a screen change is the pixel size, which
// changes only when the backing scale or the frame does. A scale change
// sends windowDidChangeBackingProperties: and a frame AppKit changes sends
// windowDidResize:, each of which pushes Resized, so a screen change that
// changes neither has nothing to report.
- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
  static_cast<void>(notification);
  if (self.owner != nullptr) {
    self.owner->notice_backing_changed();
  }
}

- (void)windowDidBecomeKey:(NSNotification*)notification {
  static_cast<void>(notification);
  if (self.owner != nullptr) {
    self.owner->notice_focus(true);
  }
}

- (void)windowDidResignKey:(NSNotification*)notification {
  static_cast<void>(notification);
  if (self.owner != nullptr) {
    self.owner->notice_focus(false);
  }
}

- (void)windowDidEnterFullScreen:(NSNotification*)notification {
  static_cast<void>(notification);
  if (self.owner != nullptr) {
    self.owner->notice_mode(bw::platform::WindowMode::BorderlessFullscreen);
  }
}

- (void)windowDidExitFullScreen:(NSNotification*)notification {
  static_cast<void>(notification);
  if (self.owner != nullptr) {
    self.owner->notice_mode(bw::platform::WindowMode::Windowed);
  }
}

- (void)windowDidFailToEnterFullScreen:(NSWindow*)window {
  static_cast<void>(window);
  if (self.owner != nullptr) {
    self.owner->notice_mode_refused();
  }
}

- (void)windowDidFailToExitFullScreen:(NSWindow*)window {
  static_cast<void>(window);
  if (self.owner != nullptr) {
    self.owner->notice_mode_refused();
  }
}

@end

@implementation BWContentView

- (BOOL)acceptsFirstResponder {
  return YES;
}

// One tracking area over the visible rectangle, so moves arrive while the
// window is key with no button held; AppKit asks for it again whenever the
// view's geometry changes. NSTrackingMouseMoved sends mouseMoved: to the
// owner inside the area, ActiveInKeyWindow limits it to the key window and
// InVisibleRect follows the visible rectangle.
- (void)updateTrackingAreas {
  for (NSTrackingArea* area in [self.trackingAreas copy]) {
    if (area.owner == self) {
      [self removeTrackingArea:area];
    }
  }
  const NSTrackingAreaOptions options =
      NSTrackingMouseMoved | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
  [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                     options:options
                                                       owner:self
                                                    userInfo:nil]];
  [super updateTrackingAreas];
}

- (void)forwardButton:(NSEvent*)event pressed:(BOOL)pressed {
  if (self.owner != nullptr) {
    self.owner->notice_mouse_button(event, pressed == YES);
  }
}

- (void)forwardMove:(NSEvent*)event {
  if (self.owner != nullptr) {
    self.owner->notice_mouse_moved(event);
  }
}

- (void)mouseDown:(NSEvent*)event {
  [self forwardButton:event pressed:YES];
}
- (void)mouseUp:(NSEvent*)event {
  [self forwardButton:event pressed:NO];
}
- (void)rightMouseDown:(NSEvent*)event {
  [self forwardButton:event pressed:YES];
}
- (void)rightMouseUp:(NSEvent*)event {
  [self forwardButton:event pressed:NO];
}
- (void)otherMouseDown:(NSEvent*)event {
  [self forwardButton:event pressed:YES];
}
- (void)otherMouseUp:(NSEvent*)event {
  [self forwardButton:event pressed:NO];
}
- (void)mouseMoved:(NSEvent*)event {
  [self forwardMove:event];
}
- (void)mouseDragged:(NSEvent*)event {
  [self forwardMove:event];
}
- (void)rightMouseDragged:(NSEvent*)event {
  [self forwardMove:event];
}
- (void)otherMouseDragged:(NSEvent*)event {
  [self forwardMove:event];
}

- (void)scrollWheel:(NSEvent*)event {
  if (self.owner != nullptr) {
    self.owner->notice_scroll(event);
  }
}

// Neither key method calls super: NSResponder's keyDown: passes an
// unhandled key up the responder chain, which ends in the system's alert
// sound.
- (void)keyDown:(NSEvent*)event {
  if (self.owner != nullptr) {
    self.owner->notice_key(event, true);
  }
}
- (void)keyUp:(NSEvent*)event {
  if (self.owner != nullptr) {
    self.owner->notice_key(event, false);
  }
}
// A modifier key's change (window.hpp, Device input: KeyLeftShift). Not
// passed up the responder chain, as the key methods are not.
- (void)flagsChanged:(NSEvent*)event {
  if (self.owner != nullptr) {
    self.owner->notice_flags(event);
  }
}

@end

@implementation BWApplicationDelegate

// window.hpp, Pump: a quit request to the application produces
// CloseRequested on every window of this adapter, and the application keeps
// running: closing is the host's decision. The host ends the process by
// returning from main after destroying the window, so termination is
// cancelled here and never resumed.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
  for (NSWindow* window in sender.windows) {
    id<NSWindowDelegate> delegate = window.delegate;
    if ([delegate isKindOfClass:[BWWindowDelegate class]]) {
      BWWindowDelegate* ours = delegate;
      [ours requestClose];
    }
  }
  return NSTerminateCancel;
}

@end
