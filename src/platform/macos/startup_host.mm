// Native window/input/media-audio integration. Rendering producers and final
// output composition have independent owners: WorldRenderer, MovieRenderer,
// SpriteRenderer and OutputCompositor. Existing producer-facing APIs forward
// to those owners; the host only updates drawable size and submits immutable
// opacity/timing snapshots. No game timing or rendering algorithm lives here.
// Main-thread event pump preserves physical/action edges and focus loss;
// native fullscreen completes before any media starts. MediaAudio retains its
// independent device execution and callback lifetime. The window owns its
// layer; the compositor borrows sibling renderers and is destroyed first.
// Every completed draw has retired native work before future upload/release.
// Native owner state/path scratch use the injected CPU domain; Cocoa objects
// remain external. Errors unwind owned resources; no async work is introduced.
// Both design and implementation retrieved Core R.1 (#rr-raii), I.11
// (#ri-raw), performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md), and GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md): stable resource owners,
// explicit borrowed encoders, no intermediate CPU copies. Native ARC handles
// and budgeted allocations release automatically after synchronous completion.
#include "bw/platform/startup_host.hpp"
#include <IOKit/hidsystem/IOLLEvent.h>
#include "bw/input/action_repeat.hpp"
#include "bw/memory/allocator.hpp"
#include "bw/platform/media_audio.hpp"
#include "gamepad_actions.hpp"
#include "physical_keys.hpp"
#include "platform/macos/output_compositor.hpp"
#include "platform/macos/world_renderer.hpp"

#import <Cocoa/Cocoa.h>
#import <GameController/GameController.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#include <mach-o/dyld.h>

// Native transitions publish window state without calling game objects.
@interface BWStartupWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) BOOL closeEnabled;
@property(nonatomic) BOOL fullscreen;
@property(nonatomic) BOOL fullscreenTransition;
@property(nonatomic) BOOL fullscreenFailed;
@end
@implementation BWStartupWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
  (void)sender;
  return self.closeEnabled;
}
- (void)windowWillEnterFullScreen:(NSNotification*)notification {
  (void)notification;
  self.fullscreenTransition = YES;
  self.fullscreenFailed = NO;
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
  (void)notification;
  self.fullscreen = YES;
  self.fullscreenTransition = NO;
}
- (void)windowDidFailToEnterFullScreen:(NSWindow*)window {
  (void)window;
  self.fullscreenFailed = YES;
  self.fullscreenTransition = NO;
}
- (void)windowWillExitFullScreen:(NSNotification*)notification {
  (void)notification;
  self.fullscreenTransition = YES;
}
- (void)windowDidExitFullScreen:(NSNotification*)notification {
  (void)notification;
  self.fullscreen = NO;
  self.fullscreenTransition = NO;
}
- (void)windowDidFailToExitFullScreen:(NSWindow*)window {
  (void)window;
  self.fullscreenTransition = NO;
}
@end

namespace bw::platform {
namespace {
constexpr std::uint32_t kConfirmAction = 0x1000U;
constexpr std::uint32_t kStartAction = 0x10U;
constexpr std::uint32_t kCancelAction = 0x2000U;
constexpr unsigned short kReturnKey = 36;
constexpr unsigned short kSpaceKey = 49;
constexpr unsigned short kEscapeKey = 53;
constexpr unsigned short kFullscreenKey = 3;
constexpr unsigned short kUpKey = 126, kDownKey = 125, kLeftKey = 123, kRightKey = 124;
constexpr std::uint32_t kUpAction = 0x01000000U;
constexpr std::uint32_t kDownAction = 0x02000000U;
constexpr std::uint32_t kLeftAction = 0x04000000U;
constexpr std::uint32_t kRightAction = 0x08000000U;
constexpr std::uint32_t kDirections = 0x0f000000U;
std::uint32_t key_action(unsigned short code) {
  switch (code) {
    case kReturnKey:
    case kSpaceKey:
      return kConfirmAction;
    case kEscapeKey:
      return kCancelAction;
    case kUpKey:
      return kUpAction;
    case kDownKey:
      return kDownAction;
    case kLeftKey:
      return kLeftAction;
    case kRightKey:
      return kRightAction;
    default:
      return 0;
  }
}
void require_main_thread() {
  if (![NSThread isMainThread]) {
    throw std::runtime_error("StartupHost requires the main thread");
  }
}

}  // namespace

struct StartupHost::State {
  NSWindow* window{};
  BWStartupWindowDelegate* window_delegate{};
  CAMetalLayer* layer{};
  std::shared_ptr<macos::WorldRenderer> world;
  std::optional<macos::MovieRenderer> movie;
  std::optional<macos::SpriteRenderer> sprites;
  // Declared after its producers: destroy the compositor before them.
  std::optional<macos::OutputCompositor> output;
  MediaAudio media_audio;
  input::PhysicalState physical;
  std::uint32_t keyboard_keys{};
  macos::GamepadButtonEdges pad_edges;
  std::uint32_t previous_held{};
  std::optional<timing::FrameTiming> frame_timing;
  input::ActionRepeat action_repeat;
  explicit State(const std::shared_ptr<memory::Domain>& cpu_domain) : media_audio(cpu_domain) {}
  ~State() {
    media_audio.stop();
    [window close];
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
};

StartupHost::StartupHost(const std::shared_ptr<memory::Domain>& cpu_domain,
                         std::shared_ptr<memory::DeviceBudget> device_budget, WindowOptions options,
                         bool texture_mips)
    : state_(std::allocate_shared<State>(memory::Allocator<State>(cpu_domain), cpu_domain)) {
  if (!device_budget) throw std::invalid_argument("Missing device memory budget");
  require_main_thread();
  constexpr std::uint32_t maximum_window_dimension = 16384;
  if (options.width == 0 || options.height == 0 || options.width > maximum_window_dimension ||
      options.height > maximum_window_dimension) {
    throw std::runtime_error("Invalid startup window dimensions");
  }
  @autoreleasepool {
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    auto& state = *state_;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
      throw std::runtime_error("No Metal device is available");
    }
    id<MTLCommandQueue> commands = [device newCommandQueue];
    if (commands == nil) throw std::runtime_error("Could not create Metal output queue");
    state.world = std::allocate_shared<macos::WorldRenderer>(
        memory::Allocator<macos::WorldRenderer>(cpu_domain), device, commands, cpu_domain,
        device_budget);
    NSError* error = nil;
    // Resolve compiled shaders beside the executable, independent of working directory.
    std::uint32_t path_size = 0;
    (void)_NSGetExecutablePath(nullptr, &path_size);
    memory::Vector<char> executable_path(path_size, memory::Allocator<char>(cpu_domain));
    if (_NSGetExecutablePath(executable_path.data(), &path_size) != 0) {
      throw std::runtime_error("Could not locate startup executable");
    }
    const auto shader_path =
        std::filesystem::canonical(executable_path.data()).parent_path() / "movie.metallib";
    NSString* shader_name = [NSString stringWithUTF8String:shader_path.c_str()];
    if (shader_name == nil) {
      throw std::runtime_error("Shader library path is not valid UTF-8");
    }
    NSURL* shader_url = [NSURL fileURLWithPath:shader_name];
    id<MTLLibrary> library = [device newLibraryWithURL:shader_url error:&error];
    if (library == nil) {
      throw std::runtime_error("Metal movie shader: " +
                               std::string(error.localizedDescription.UTF8String));
    }
    state.movie.emplace(device, library, device_budget);
    state.sprites.emplace(device, library, std::move(device_budget), texture_mips);
    const NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                                    NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
    // Clamp requested content points to the display before native fullscreen transition.
    const auto visible = [NSWindow contentRectForFrameRect:NSScreen.mainScreen.visibleFrame
                                                 styleMask:style];
    const CGFloat fit =
        std::min({1.0, visible.size.width / options.width, visible.size.height / options.height});
    const CGFloat width = options.width * fit;
    const CGFloat height = options.height * fit;
    state.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, width, height)
                                               styleMask:style
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
    state.window.releasedWhenClosed = NO;
    state.window.acceptsMouseMovedEvents = YES;
    state.window_delegate = [[BWStartupWindowDelegate alloc] init];
    state.window.delegate = state.window_delegate;
    state.window.title =
        NSLocalizedString(@"Bullet Witch — startup reconstruction", @"Startup window title");
    state.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    state.layer = [CAMetalLayer layer];
    state.layer.device = device;
    state.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    state.layer.framebufferOnly = YES;
    state.layer.opaque = YES;
    state.output.emplace(commands, library, state.layer, *state.world, *state.movie,
                         *state.sprites);
    state.window.contentView.wantsLayer = YES;
    state.window.contentView.layer = state.layer;
    [state.window center];
    [state.window makeKeyAndOrderFront:nil];
    [NSApp finishLaunching];
    [NSApp activate];
    if (options.fullscreen) {
      // SDL's Cocoa backend similarly pumps the native transition to completion.
      // The deadline bounds a failed transition; it does not schedule playback.
      state.window_delegate.fullscreenTransition = YES;
      [state.window toggleFullScreen:nil];
      constexpr auto maximum_transition = std::chrono::seconds(10);
      constexpr NSTimeInterval event_wait = 0.01;
      const auto deadline = std::chrono::steady_clock::now() + maximum_transition;
      while (state.window_delegate.fullscreenTransition &&
             std::chrono::steady_clock::now() < deadline) {
        @autoreleasepool {
          NSEvent* event =
              [NSApp nextEventMatchingMask:NSEventMaskAny
                                 untilDate:[NSDate dateWithTimeIntervalSinceNow:event_wait]
                                    inMode:NSDefaultRunLoopMode
                                   dequeue:YES];
          if (event != nil) {
            [NSApp sendEvent:event];
          }
          [NSApp updateWindows];
        }
      }
      if (!state.window_delegate.fullscreen || state.window_delegate.fullscreenFailed ||
          state.window_delegate.fullscreenTransition) {
        throw std::runtime_error(
            "Native fullscreen startup failed: rejected=" +
            std::to_string(state.window_delegate.fullscreenFailed) +
            " pending=" + std::to_string(state.window_delegate.fullscreenTransition) +
            " entered=" + std::to_string(state.window_delegate.fullscreen) + " active=" +
            std::to_string(NSApp.active) + " key=" + std::to_string(state.window.keyWindow));
      }
    }
  }
}
StartupHost::~StartupHost() = default;

StartupInput StartupHost::pump(bool window_close_enabled) {
  require_main_thread();
  @autoreleasepool {
    auto& state = *state_;
    state.window_delegate.closeEnabled = window_close_enabled;
    std::uint32_t key_edges = 0;
    auto& physical = state.physical;
    physical.keys_pressed.reset();
    physical.pad_held = 0;
    physical.mouse_pressed = 0;
    physical.mouse_delta = {};
    physical.wheel = 0;
    physical.movement = {};
    physical.look = {};
    physical.left_trigger = physical.right_trigger = 0;
    while (NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                               untilDate:[NSDate distantPast]
                                                  inMode:NSDefaultRunLoopMode
                                                 dequeue:YES]) {
      if (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp) {
        const auto code = event.keyCode;
        constexpr NSEventModifierFlags fullscreen_modifiers =
            NSEventModifierFlagControl | NSEventModifierFlagCommand;
        if (code == kFullscreenKey &&
            (event.modifierFlags & fullscreen_modifiers) == fullscreen_modifiers) {
          if (event.type == NSEventTypeKeyDown && !event.isARepeat &&
              !state.window_delegate.fullscreenTransition) {
            state.window_delegate.fullscreenTransition = YES;
            [state.window toggleFullScreen:nil];
          }
          continue;
        }
        const auto usage = macos::keyboard_usage(code);
        if (usage != 0) {
          const bool down = event.type == NSEventTypeKeyDown;
          if (down && !event.isARepeat) physical.keys_pressed.set(usage);
          physical.keys_held.set(usage, down);
        }
        const auto action = key_action(code);
        const std::uint32_t key =
            code == kReturnKey ? 1U : (code == kSpaceKey ? 2U : (code == kEscapeKey ? 4U : action));
        if (event.type == NSEventTypeKeyDown) {
          if (!event.isARepeat) {
            key_edges |= action;
          }
          state.keyboard_keys |= key;
        } else {
          state.keyboard_keys &= ~key;
        }
      } else {
        if (event.type == NSEventTypeFlagsChanged) {
          // Device-specific modifier bits retain left/right key identity even
          // while both keys are held. The independent mask collapses that state.
          constexpr std::array<std::pair<NSEventModifierFlags, std::uint16_t>, 8> kModifiers{
              {{NX_DEVICELSHIFTKEYMASK, 225},
               {NX_DEVICERSHIFTKEYMASK, 229},
               {NX_DEVICELCTLKEYMASK, 224},
               {NX_DEVICERCTLKEYMASK, 228},
               {NX_DEVICELALTKEYMASK, 226},
               {NX_DEVICERALTKEYMASK, 230},
               {NX_DEVICELCMDKEYMASK, 227},
               {NX_DEVICERCMDKEYMASK, 231}}};
          for (const auto& [flag, usage] : kModifiers) {
            const bool down = (event.modifierFlags & flag) != 0;
            if (down && !physical.keys_held.test(usage)) physical.keys_pressed.set(usage);
            physical.keys_held.set(usage, down);
          }
        }
        if (event.type == NSEventTypeMouseMoved || event.type == NSEventTypeLeftMouseDragged ||
            event.type == NSEventTypeRightMouseDragged ||
            event.type == NSEventTypeOtherMouseDragged) {
          physical.mouse_delta[0] += static_cast<float>(event.deltaX);
          physical.mouse_delta[1] -= static_cast<float>(event.deltaY);
        }
        if (event.type == NSEventTypeScrollWheel) {
          physical.wheel += event.scrollingDeltaY > 0 ? 1 : (event.scrollingDeltaY < 0 ? -1 : 0);
        }
        const bool mouse_down = event.type == NSEventTypeLeftMouseDown ||
                                event.type == NSEventTypeRightMouseDown ||
                                event.type == NSEventTypeOtherMouseDown;
        const bool mouse_up = event.type == NSEventTypeLeftMouseUp ||
                              event.type == NSEventTypeRightMouseUp ||
                              event.type == NSEventTypeOtherMouseUp;
        constexpr NSInteger kMouseButtonCount = 8;
        if ((mouse_down || mouse_up) && event.buttonNumber >= 0 &&
            event.buttonNumber < kMouseButtonCount) {
          const auto bit = static_cast<std::uint8_t>(1U << event.buttonNumber);
          if (mouse_down) {
            physical.mouse_pressed |= bit;
            physical.mouse_held |= bit;
          } else {
            physical.mouse_held &= static_cast<std::uint8_t>(~bit);
          }
        }
        [NSApp sendEvent:event];
      }
    }
    [NSApp updateWindows];
    // Merge keyboard and controller holds into the recovered action bit layout.
    std::uint32_t held = ((state.keyboard_keys & 3U) != 0 ? kConfirmAction : 0U) |
                         ((state.keyboard_keys & 4U) != 0 ? kCancelAction : 0U) |
                         (state.keyboard_keys & kDirections);
    NSArray<GCController*>* controllers = [GCController controllers];
    bool controller_connected = false;
    for (NSUInteger index = 0; index < controllers.count; ++index) {
      controller_connected = controller_connected || controllers[index].extendedGamepad != nil;
    }
    if (NSApp.active && state.window.keyWindow) {
      for (NSUInteger index = 0; index < controllers.count; ++index) {
        GCController* controller = controllers[index];
        GCExtendedGamepad* pad = controller.extendedGamepad;
        if (pad == nil) continue;
        const auto merge_axis = [](float keyboard, float controller_axis) {
          return std::abs(controller_axis) > std::abs(keyboard) ? controller_axis : keyboard;
        };
        physical.movement[0] = merge_axis(physical.movement[0], pad.leftThumbstick.xAxis.value);
        physical.movement[1] = merge_axis(physical.movement[1], pad.leftThumbstick.yAxis.value);
        physical.look[0] = merge_axis(physical.look[0], pad.rightThumbstick.xAxis.value);
        physical.look[1] = merge_axis(physical.look[1], pad.rightThumbstick.yAxis.value);
        physical.left_trigger = std::max(physical.left_trigger, pad.leftTrigger.value);
        physical.right_trigger = std::max(physical.right_trigger, pad.rightTrigger.value);
        physical.pad_held |= macos::gamepad_physical_buttons(pad);
        if (pad.buttonA.pressed) {
          held |= kConfirmAction;
        }
        if (pad.buttonMenu.pressed) {
          held |= kStartAction;
        }
        if (pad.buttonB.pressed) {
          held |= kCancelAction;
        }
        constexpr float navigation_threshold = 0.5F;
        if (pad.dpad.up.pressed || pad.leftThumbstick.yAxis.value > navigation_threshold) {
          held |= kUpAction;
        }
        if (pad.dpad.down.pressed || pad.leftThumbstick.yAxis.value < -navigation_threshold) {
          held |= kDownAction;
        }
        if (pad.dpad.left.pressed || pad.leftThumbstick.xAxis.value < -navigation_threshold) {
          held |= kLeftAction;
        }
        if (pad.dpad.right.pressed || pad.leftThumbstick.xAxis.value > navigation_threshold) {
          held |= kRightAction;
        }
      }
    } else {
      // Focus loss retires held input rather than carrying it into the next activation.
      physical = {};
      state.keyboard_keys = 0;
      held = 0;
      key_edges = 0;
    }
    // Keep press edges distinct from the recovered per-bit repeat timing.
    const auto pressed = key_edges | (held & ~state.previous_held);
    if (!state.frame_timing) throw std::logic_error("Input sampling has no frame timing");
    const auto repeated =
        state.action_repeat.advance({held, pressed}, state.frame_timing->presentation_seconds);
    state.previous_held = held;
    physical.controller_connected = controller_connected;
    physical.pad_pressed = state.pad_edges.sample(physical.pad_held);
    return {.physical = physical,
            .pressed_actions = pressed,
            .repeated_actions = repeated,
            .close_requested = !state.window.visible,
            .controller_connected = controller_connected};
  }
}
void StartupHost::upload(const video::VideoFrame& frame) {
  require_main_thread();
  state_->movie->upload(frame);
}
void StartupHost::clear_video() noexcept { state_->movie->clear(); }
void StartupHost::set_frame_timing(timing::FrameTiming frame) {
  require_main_thread();
  if (!std::isfinite(frame.presentation_seconds) || frame.presentation_seconds < 0 ||
      !std::isfinite(frame.wall_seconds) || frame.wall_seconds < 0) {
    throw std::invalid_argument("Invalid host frame timing");
  }
  state_->frame_timing = frame;
}
bool StartupHost::draw(float opacity) {
  require_main_thread();
  auto& state = *state_;
  const auto bounds = state.window.contentView.bounds;
  const auto scale = state.window.backingScaleFactor;
  state.layer.drawableSize = CGSizeMake(bounds.size.width * scale, bounds.size.height * scale);
  return state.output->present({.opacity = opacity, .timing = state.frame_timing});
}
render::TextureHandle StartupHost::upload_texture(std::uint32_t width, std::uint32_t height,
                                                  std::span<const std::uint8_t> rgba) {
  require_main_thread();
  return state_->sprites->upload_texture(width, height, rgba);
}
render::TextureHandle StartupHost::upload_texture(const formats::WorldTextureImage& image) {
  require_main_thread();
  return state_->sprites->upload_texture(image);
}
void StartupHost::release_texture(render::TextureHandle texture) noexcept {
  state_->sprites->release_texture(texture);
}
void StartupHost::set_sprites(std::span<const render::Sprite> sprites) {
  require_main_thread();
  state_->sprites->set_sprites(sprites);
}
void StartupHost::clear_sprites() noexcept { state_->sprites->clear_sprites(); }

render::WorldModelHandle StartupHost::upload_model(const formats::ModelDefinition& model) {
  require_main_thread();
  return state_->world->upload_model(model);
}
render::WorldTextureHandle StartupHost::upload_world_texture(
    const formats::WorldTextureImage& image) {
  require_main_thread();
  return state_->world->upload_world_texture(image);
}
void StartupHost::release_world_texture(render::WorldTextureHandle texture) noexcept {
  state_->world->release_world_texture(texture);
}
void StartupHost::bind_model_textures(render::WorldModelHandle model,
                                      std::span<const render::WorldTextureHandle> textures) {
  require_main_thread();
  state_->world->bind_model_textures(model, textures);
}
render::WorldGeometryStatus StartupHost::geometry_status(render::WorldModelHandle model) const {
  require_main_thread();
  return state_->world->geometry_status(model);
}
// Keep world configuration and presentation on the same native owner.
void StartupHost::set_world_output_parameters(const render::WorldOutputParameters& parameters) {
  require_main_thread();
  state_->world->set_world_output_parameters(parameters);
}

void StartupHost::set_world_draws(std::span<const render::WorldColorDraw> draws) {
  require_main_thread();
  state_->world->set_world_draws(draws);
}
// Publish one prepared component without rebuilding unrelated scene resources.
void StartupHost::replace_world_model_draws(render::WorldModelHandle previous,
                                            std::span<const render::WorldColorDraw> replacement) {
  require_main_thread();
  state_->world->replace_world_model_draws(previous, replacement);
}
// Forward only mutable material constants; retained scene resources stay owned.
void StartupHost::update_world_texture_offsets(
    std::span<const render::WorldTextureOffsetUpdate> updates) {
  require_main_thread();
  state_->world->update_world_texture_offsets(updates);
}
// Copy completed actor palettes into the retained scene before native encoding.
void StartupHost::update_world_lighting(std::span<const render::WorldLightingUpdate> updates) {
  require_main_thread();
  state_->world->update_world_lighting(updates);
}
void StartupHost::update_world_poses(std::span<const render::WorldPoseUpdate> updates) {
  require_main_thread();
  state_->world->update_world_poses(updates);
}
void StartupHost::update_shape_positions(
    std::span<const render::WorldShapePositionUpdate> updates) {
  if (!state_->world) throw std::logic_error("World renderer is unavailable");
  state_->world->update_shape_positions(updates);
}
void StartupHost::update_world_visibility(std::span<const render::WorldVisibilityUpdate> updates) {
  require_main_thread();
  state_->world->update_world_visibility(updates);
}
// Publish the complete camera packet on the renderer's owning thread.
void StartupHost::update_world_view(const render::WorldViewUpdate& update) {
  require_main_thread();
  state_->world->update_world_view(update);
}
void StartupHost::clear_world_draws() noexcept { state_->world->clear_world_draws(); }
void StartupHost::release_model(render::WorldModelHandle model) noexcept {
  state_->world->release_model(model);
}
video::AudioOutput& StartupHost::audio() noexcept { return state_->media_audio; }
}  // namespace bw::platform
