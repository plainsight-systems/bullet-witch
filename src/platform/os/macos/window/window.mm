// The macOS window adapter (bw/platform/os/macos/window.hpp): an NSWindow
// whose content view hosts a CAMetalLayer, the AppKit event pump, the
// delegates that turn AppKit notifications into WindowEvents, and the
// content view's overrides that turn mouse, scroll and key events into the
// input kinds (window.hpp, Input).
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
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>

#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
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

private:
  // Into the queue while pump() runs, into the pending queue otherwise.
  void push(const WindowEvent& event);
  // The pending events into the queue, in arrival order, then the pending
  // queue cleared.
  void push_pending();
  // Sends the full-screen toggle and records the transition as in flight.
  void request_toggle();

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
MacWindow::~MacWindow() {
  @autoreleasepool {
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
void MacWindow::pump() {
  require_main_thread("pump");
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
void MacWindow::notice_focus(bool focused) { push(focus_event(focused)); }

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
