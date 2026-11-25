// The macOS pad adapter (bw/platform/os/macos/pad.hpp): the game controller
// framework's controllers, the first with the extended gamepad profile
// taken as the pad, polled once a frame on the main thread.
//
// Memory: compiled with ARC (-fobjc-arc, src/platform/CMakeLists.txt). The
// C++ pad holds the controller, the two notification observers and its
// link object by strong references and drops them in its destructor.
//
// Notifications reach the pad through the link's owner, which the
// destructor clears, never through `this`: the blocks capture the link
// weakly, so a notification delivered after the pad is gone does nothing.
// They are delivered on the main queue, where the host's loop polls, so the
// pad's state has one thread.
#import <Foundation/Foundation.h>
#import <GameController/GameController.h>

#include "bw/platform/input/device_input.hpp"
#include "bw/platform/input/pad.hpp"
#include "bw/platform/os/macos/pad.hpp"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <span>

namespace bw::platform::macos {
namespace {
class MacPad;
} // namespace
} // namespace bw::platform::macos

// The pad's link for the notification blocks: `owner` is cleared by the
// pad's destructor before the observers are removed.
@interface BWPadLink : NSObject
@property(nonatomic, assign) bw::platform::macos::MacPad* owner;
@end

@implementation BWPadLink
@end

namespace bw::platform::macos {
namespace {

void require_main_thread(const char* call) noexcept {
  if ([NSThread isMainThread] == NO) {
    std::fprintf(stderr, "bw pad: %s called off the main thread\n", call);
    std::abort();
  }
}

// macos/pad.hpp, contract 2: the extended profile's current values. A
// stick or trigger is the element's value as the framework gives it, a
// button its pressed state. The stick buttons are optional in the profile;
// an absent one is nil and reads NO, so 0. The framework gives an axis
// value in -1 to 1, dead-zoned and saturated, and a button or trigger
// value in 0 to 1.
ExtendedGamepadValues read_values(GCExtendedGamepad* profile) {
  ExtendedGamepadValues values{};
  values.left_x = profile.leftThumbstick.xAxis.value;
  values.left_y = profile.leftThumbstick.yAxis.value;
  values.right_x = profile.rightThumbstick.xAxis.value;
  values.right_y = profile.rightThumbstick.yAxis.value;
  values.left_trigger = profile.leftTrigger.value;
  values.right_trigger = profile.rightTrigger.value;
  values.a = profile.buttonA.pressed == YES;
  values.b = profile.buttonB.pressed == YES;
  values.x = profile.buttonX.pressed == YES;
  values.y = profile.buttonY.pressed == YES;
  values.left_shoulder = profile.leftShoulder.pressed == YES;
  values.right_shoulder = profile.rightShoulder.pressed == YES;
  values.left_stick = profile.leftThumbstickButton.pressed == YES;
  values.right_stick = profile.rightThumbstickButton.pressed == YES;
  return values;
}

class MacPad final : public Pad {
public:
  MacPad();
  MacPad(const MacPad&) = delete;
  MacPad& operator=(const MacPad&) = delete;
  MacPad(MacPad&&) = delete;
  MacPad& operator=(MacPad&&) = delete;
  ~MacPad() override;

  [[nodiscard]] std::size_t poll(std::span<InputSample> out) override;
  [[nodiscard]] bool connected() const noexcept override;

  void notice_connect(GCController* controller);
  void notice_disconnect(GCController* controller);

private:
  void adopt(GCController* controller);

  BWPadLink* link_{};
  id connect_observer_{};
  id disconnect_observer_{};
  // The pad, nil when there is none.
  GCController* controller_{};
  // The pad that disconnected and whose release the next poll reports;
  // until then no controller is taken.
  GCController* departed_{};
  // A controller whose connect arrived while a disconnect was still to be
  // reported; taken after that poll if the OS's list gives none.
  GCController* arrived_while_departing_{};
  // The values the last poll reported; all 0 before the first.
  ExtendedGamepadValues previous_{};
};

// macos/pad.hpp, contract 1: the controllers already connected, then the
// connect and disconnect notifications, on the main queue. Each
// notification carries the controller as its object. The framework posts
// them on the main thread, where the poll runs, so the state changes at
// once.
MacPad::MacPad() : link_([[BWPadLink alloc] init]) {
  require_main_thread("create_pad");
  link_.owner = this;
  @autoreleasepool {
    for (GCController* controller in GCController.controllers) {
      adopt(controller);
    }
    __weak BWPadLink* weak_link = link_;
    NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
    connect_observer_ = [center addObserverForName:GCControllerDidConnectNotification
                                            object:nil
                                             queue:NSOperationQueue.mainQueue
                                        usingBlock:^(NSNotification* notification) {
                                          BWPadLink* link = weak_link;
                                          id object = notification.object;
                                          if (link != nil && link.owner != nullptr &&
                                              [object isKindOfClass:[GCController class]]) {
                                            link.owner->notice_connect(object);
                                          }
                                        }];
    disconnect_observer_ = [center addObserverForName:GCControllerDidDisconnectNotification
                                               object:nil
                                                queue:NSOperationQueue.mainQueue
                                           usingBlock:^(NSNotification* notification) {
                                             BWPadLink* link = weak_link;
                                             id object = notification.object;
                                             if (link != nil && link.owner != nullptr &&
                                                 [object isKindOfClass:[GCController class]]) {
                                               link.owner->notice_disconnect(object);
                                             }
                                           }];
  }
}

MacPad::~MacPad() {
  require_main_thread("~Pad");
  link_.owner = nullptr;
  NSNotificationCenter* center = NSNotificationCenter.defaultCenter;
  [center removeObserver:connect_observer_];
  [center removeObserver:disconnect_observer_];
}

// pad.hpp, contract 3: a controller is taken when there is no pad and no
// release is still to be reported, and only with the extended profile.
void MacPad::adopt(GCController* controller) {
  if (controller_ != nil || controller.extendedGamepad == nil) {
    return;
  }
  if (departed_ != nil) {
    if (arrived_while_departing_ == nil) {
      arrived_while_departing_ = controller;
    }
    return;
  }
  controller_ = controller;
}

void MacPad::notice_connect(GCController* controller) { adopt(controller); }

// pad.hpp, contract 4: the pad's disconnect leaves it with no controller,
// so the next poll reads all 0 and reports the release of every element
// that was not at 0; another controller is looked for after that poll.
void MacPad::notice_disconnect(GCController* controller) {
  if (controller == arrived_while_departing_) {
    arrived_while_departing_ = nil;
  }
  if (controller_ == nil || controller != controller_) {
    return;
  }
  departed_ = controller_;
  controller_ = nil;
}

// pad.hpp, contract 2; macos/pad.hpp, contract 2: the current values read
// (all 0 with no pad), their difference from the last poll's reported
// through pad_samples, and after a disconnect's release the first
// controller with the extended profile in the OS's list taken for the next
// poll, the same object included if the OS lists it again; a connect that
// arrived meanwhile is taken when the list gives none. No dead
// zone, scale or clamp is applied (pad.hpp, contract 5). While the
// application is not frontmost the framework forwards no input, and the
// adapter adds no handling of its own (pad.hpp, contract 6). The values
// are read live, one property at a time, so one that changes between two
// reads is reported by the next poll.
std::size_t MacPad::poll(std::span<InputSample> out) {
  require_main_thread("poll");
  if (out.size() < kPadElementCount) {
    std::fprintf(stderr, "bw pad: poll given %zu samples, fewer than %zu\n", out.size(),
                 kPadElementCount);
    std::abort();
  }
  std::size_t count = 0;
  @autoreleasepool {
    const ExtendedGamepadValues current =
        controller_ != nil ? read_values(controller_.extendedGamepad) : ExtendedGamepadValues{};
    count = pad_samples(previous_, current, out);
    previous_ = current;
    if (departed_ != nil) {
      departed_ = nil;
      GCController* arrived = arrived_while_departing_;
      arrived_while_departing_ = nil;
      for (GCController* controller in GCController.controllers) {
        adopt(controller);
      }
      if (arrived != nil) {
        adopt(arrived);
      }
    }
  }
  return count;
}

bool MacPad::connected() const noexcept { return controller_ != nil; }

} // namespace

std::unique_ptr<Pad> create_pad() { return std::make_unique<MacPad>(); }

} // namespace bw::platform::macos
