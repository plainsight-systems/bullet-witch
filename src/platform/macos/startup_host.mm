// Action input extends the existing main-thread snapshot: retain keyboard taps,
// sample controller holds, clear on focus loss, then derive one-shot edges.
// RT/F stays normalized held fire intent; the game owns acceptance and cadence.
// Both MCP phases apply I.1 (#ri-explicit) to this platform/game boundary and
// MEM.9 (guidelines/memory/MEM.9-embedded-allocate-at-init.md) to fixed masks.
// No callback, allocation, repeat timer or gameplay service is added per action.
// Component replacement applies I.1 (#ri-explicit), C.85 (#rc-swap-noexcept),
// and GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md): validate/prepare before
// nonthrowing publication, keeping GPU assets resident. Both MCP phases read.
// Hardware evade is held LT above 0.1; Space retains down/up taps within one
// event pump. A stays menu confirmation. Both-phase I.1/E.4 expose distinct
// hold/edge meanings; MEM.9 stores fixed input state without frame allocation.
// Keyboard gameplay presses retain event edges separately from final held state
// (P.1 #rp-direct); MEM.9 uses two stack flags without frame-time allocation.
// Sprite canvas fit is independent of world residency. Implementation ES.22
// (#res-init) initializes the complete viewport before either pass uses it;
// MEM.9 keeps this per-frame arithmetic in fixed stack storage.
// Material animation forwards an explicit borrowed constant-update batch
// (both-phase I.1 #ri-explicit and GPU.1 retained-device-data guidance).
// The host preserves the native owner and main-thread access boundary.
// Forward authored display policy to the retained world renderer used by draw.
// C.128 (#rh-override) makes the adapter override explicit; GPU.9
// (guidelines/gpu/GPU.9-suballocate-and-alias-transient-gpu-memory.md) retains
// the existing target pool rather than allocating another output owner.
// World uploads delegate to the native immutable geometry owner. They neither
// change movie/sprite ordering nor publish color-ready materials. E.6 and GPU.1
// were retrieved in both phases; see render/world.hpp and world-renderer.hpp.
// Memory ownership: host State/control and path scratch use the injected CPU
// domain. Native texture owners separately admit/reconcile logical and reported
// Metal sizes, retain charges through use, and release after synchronous command
// completion. Existing sprite cap is preserved. Cocoa/pipeline/drawable storage
// remains external. E.6 (#re-raii), SL.con.3 (#rsl-bounds), GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md), COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md) apply in
// both consulted phases. See device-memory-ownership.md.
// Input implementation: original 0x41cfd0 per-bit elapsed-time repeat, fixed
// 32 timers; focus loss clears held/repeat state. Retrieved SL.con.3 bounds
// iteration, MEM.9 confines storage to setup. See frontend-presentation.md.
// Native full screen uses completed AppKit delegate transitions before returning
// from construction; no media session runs during startup animation. SDL Cocoa
// source and the bounded event-pump design are in sprite-presentation-design.md.
// Sprite implementation MCP: cpp-guidelines SL.con.3 (#rsl-bounds) validates
// complete bounded submissions before mutation; cpp-performance GPU.1
// (guidelines/gpu/GPU.1-keep-data-on-device.md) retains uploaded sprite textures.
// Sprite state owns at most 128 textures/128 MiB and 511 ordered quads; each
// submission copies commands into fixed storage, resolves texture slots once,
// and performs no heap allocation. Release removes referencing queued quads.
// Native allocations remain explicit setup costs pending engine memory work.
// macOS startup window/input and Metal presentation; reusable audio is composed
// through MediaAudio, with its own execution and lifetime contract. Main-thread
// calls own Cocoa, one retained BGRA texture and the compiled movie pipeline.
// Events become development action edges; upload changes the texture; draw
// aspect-fits and applies game-supplied opacity, waiting for GPU completion before
// reuse. No game timing, media buffering or audio callback work lives here.
// Cost: one width*height*4 upload per selected frame, no video readback. Shaders
// load from executable-relative movie.metallib. ARC owns native ObjC handles.
// Design/implementation MCP: cpp-guidelines R.1 (#rr-raii) pairs platform
// ownership; cpp-performance COPY.5 (guidelines/copy-move/
// COPY.5-resource-owning-types-are-move-only.md) fixes native owner identity;
// GPU.1 (guidelines/gpu/GPU.1-keep-data-on-device.md) retains uploaded frames.
#include "bw/platform/startup_host.hpp"
#include "bw/memory/allocator.hpp"
#include "bw/platform/media_audio.hpp"
#include "gamepad_actions.hpp"
#include "platform/macos/texture_allocation.hpp"
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
constexpr std::uint32_t kConfirmAction = 0x1010U;
constexpr std::uint32_t kCancelAction = 0x2020U;
constexpr unsigned short kReturnKey = 36;
constexpr unsigned short kSpaceKey = 49;
constexpr unsigned short kCrouchKey = 8;
constexpr unsigned short kEscapeKey = 53;
constexpr unsigned short kFullscreenKey = 3;
constexpr unsigned short kUpKey = 126, kDownKey = 125, kLeftKey = 123, kRightKey = 124;
constexpr std::size_t kActionBits = 32;
constexpr std::uint32_t kUpAction = 0x01000000U;
constexpr std::uint32_t kDownAction = 0x02000000U;
constexpr std::uint32_t kLeftAction = 0x04000000U;
constexpr std::uint32_t kRightAction = 0x08000000U;
constexpr std::uint32_t kDirections = 0x0f000000U;
constexpr std::uint32_t kMoveForward = 1U << 0, kMoveBackward = 1U << 1;
constexpr std::uint32_t kMoveLeft = 1U << 2, kMoveRight = 1U << 3;
constexpr std::uint32_t kLookUp = 1U << 4, kLookDown = 1U << 5;
constexpr std::uint32_t kLookLeft = 1U << 6, kLookRight = 1U << 7;
constexpr std::uint32_t kFire = 1U << 8;
constexpr std::uint32_t kReload = macos::kReloadButton;
constexpr std::uint32_t kAttack = macos::kAttackButton;
constexpr std::uint32_t kWeaponCycle = macos::kWeaponButton;
constexpr std::uint32_t kAimMode = macos::kTargetButton;
constexpr std::uint32_t kActionPresses = kReload | kAttack | kWeaponCycle | kAimMode;
std::uint32_t gameplay_key(unsigned short code) {
  constexpr std::array<std::pair<unsigned short, std::uint32_t>, 13> bindings{{{13, kMoveForward},
                                                                               {1, kMoveBackward},
                                                                               {0, kMoveLeft},
                                                                               {2, kMoveRight},
                                                                               {34, kLookUp},
                                                                               {40, kLookDown},
                                                                               {38, kLookLeft},
                                                                               {37, kLookRight},
                                                                               {3, kFire},
                                                                               {15, kReload},
                                                                               {14, kAttack},
                                                                               {12, kWeaponCycle},
                                                                               {17, kAimMode}}};
  const auto found =
      std::ranges::find_if(bindings, [code](const auto& binding) { return binding.first == code; });
  return found == bindings.end() ? 0U : found->second;
}
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
constexpr std::size_t kTextureByteBudget = std::size_t{128} * 1024 * 1024;
struct SpriteParameters {
  std::array<float, 4> rectangle;
  std::array<float, 4> uv;
  std::array<float, 4> color;
  std::array<float, 4> sampling;
  std::array<float, 4> transform;
};
bool valid_rectangle(const render::Rect& rect) {
  return std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
         std::isfinite(rect.height) && rect.width > 0 && rect.height > 0 &&
         std::isfinite(rect.x + rect.width) && std::isfinite(rect.y + rect.height);
}
bool unit_value(float value) { return std::isfinite(value) && value >= 0 && value <= 1; }
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
  id<MTLDevice> device{};
  id<MTLCommandQueue> commands{};
  id<MTLRenderPipelineState> pipeline{};
  id<MTLLibrary> shader_library{};
  macos::TextureAllocation video;
  std::shared_ptr<memory::DeviceBudget> device_budget;
  std::shared_ptr<macos::WorldRenderer> world;
  id<MTLRenderPipelineState> sprite_pipeline{};
  struct Texture {
    render::TextureHandle handle;
    macos::TextureAllocation native;
    std::size_t bytes{};
  };
  struct SubmittedSprite {
    render::Sprite sprite;
    std::size_t texture_slot{};
  };
  std::array<Texture, render::kMaximumTextures> textures;
  std::array<SubmittedSprite, render::kMaximumSprites> sprites;
  std::size_t sprite_count{};
  std::size_t texture_bytes{};
  std::uint64_t next_texture{1};
  MediaAudio media_audio;
  std::uint32_t keyboard_keys{};
  std::uint32_t gameplay_keys{};
  bool previous_pause{}, previous_crouch{};
  macos::GameplayButtonEdges action_edges;
  std::uint32_t previous_held{};
  std::array<double, kActionBits> repeat_elapsed{};
  std::chrono::steady_clock::time_point input_time{std::chrono::steady_clock::now()};
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
                         std::shared_ptr<memory::DeviceBudget> device_budget, WindowOptions options)
    : state_(std::allocate_shared<State>(memory::Allocator<State>(cpu_domain), cpu_domain)) {
  if (!device_budget) throw std::invalid_argument("Missing device memory budget");
  state_->device_budget = std::move(device_budget);
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
    state.device = MTLCreateSystemDefaultDevice();
    if (state.device == nil) {
      throw std::runtime_error("No Metal device is available");
    }
    state.commands = [state.device newCommandQueue];
    state.world = std::allocate_shared<macos::WorldRenderer>(
        memory::Allocator<macos::WorldRenderer>(cpu_domain), state.device, state.commands,
        cpu_domain, state.device_budget);
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
    id<MTLLibrary> library = [state.device newLibraryWithURL:shader_url error:&error];
    if (library == nil) {
      throw std::runtime_error("Metal movie shader: " +
                               std::string(error.localizedDescription.UTF8String));
    }
    state.shader_library = library;
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"movie_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"movie_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    state.pipeline = [state.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (state.commands == nil || state.pipeline == nil) {
      throw std::runtime_error("Could not initialize Metal movie pipeline");
    }
    // Sprites composite straight-alpha color after the opaque movie pass.
    descriptor.vertexFunction = [library newFunctionWithName:@"sprite_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"sprite_fragment"];
    descriptor.colorAttachments[0].blendingEnabled = YES;
    descriptor.colorAttachments[0].sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
    descriptor.colorAttachments[0].destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    descriptor.colorAttachments[0].sourceAlphaBlendFactor = MTLBlendFactorOne;
    descriptor.colorAttachments[0].destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
    state.sprite_pipeline = [state.device newRenderPipelineStateWithDescriptor:descriptor
                                                                         error:&error];
    if (state.sprite_pipeline == nil) {
      throw std::runtime_error("Could not initialize Metal sprite pipeline");
    }
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
    state.window_delegate = [[BWStartupWindowDelegate alloc] init];
    state.window.delegate = state.window_delegate;
    state.window.title =
        NSLocalizedString(@"Bullet Witch — startup reconstruction", @"Startup window title");
    state.window.collectionBehavior = NSWindowCollectionBehaviorFullScreenPrimary;
    state.layer = [CAMetalLayer layer];
    state.layer.device = state.device;
    state.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    state.layer.framebufferOnly = YES;
    state.layer.opaque = YES;
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
            "Could not complete native full screen transition; retry with --windowed");
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
    std::uint32_t action_key_edges = 0;
    bool evade_key_edge = false, pause_key_edge = false, crouch_key_edge = false;
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
        const auto action = key_action(code);
        const std::uint32_t key =
            code == kReturnKey ? 1U : (code == kSpaceKey ? 2U : (code == kEscapeKey ? 4U : action));
        if (event.type == NSEventTypeKeyDown) {
          if (!event.isARepeat) {
            key_edges |= action;
            action_key_edges |= gameplay_key(code) & kActionPresses;
            // A down/up pair drained together must still produce one press.
            evade_key_edge = evade_key_edge || code == kSpaceKey;
            pause_key_edge = pause_key_edge || code == kEscapeKey;
            crouch_key_edge = crouch_key_edge || code == kCrouchKey;
          }
          state.keyboard_keys |= key;
          state.gameplay_keys |= gameplay_key(code);
        } else {
          state.keyboard_keys &= ~key;
          state.gameplay_keys &= ~gameplay_key(code);
        }
      } else {
        [NSApp sendEvent:event];
      }
    }
    [NSApp updateWindows];
    // Merge keyboard and controller holds into the recovered action bit layout.
    std::uint32_t held = ((state.keyboard_keys & 3U) != 0 ? kConfirmAction : 0U) |
                         ((state.keyboard_keys & 4U) != 0 ? kCancelAction : 0U) |
                         (state.keyboard_keys & kDirections);
    const auto key_axis = [&](std::uint32_t positive, std::uint32_t negative) {
      return static_cast<float>((state.gameplay_keys & positive) != 0) -
             static_cast<float>((state.gameplay_keys & negative) != 0);
    };
    std::array<float, 2> movement{key_axis(kMoveRight, kMoveLeft),
                                  key_axis(kMoveForward, kMoveBackward)};
    std::array<float, 2> look{key_axis(kLookRight, kLookLeft), key_axis(kLookUp, kLookDown)};
    bool evade = (state.keyboard_keys & 2U) != 0;
    bool pause = (state.keyboard_keys & 4U) != 0;
    bool crouch = false;
    float fire_trigger = (state.gameplay_keys & kFire) != 0 ? 1.0F : 0.0F;
    std::uint32_t action_holds = state.gameplay_keys & kActionPresses;
    if (NSApp.active && state.window.keyWindow) {
      NSArray<GCController*>* controllers = [GCController controllers];
      for (NSUInteger index = 0; index < controllers.count; ++index) {
        GCController* controller = controllers[index];
        GCExtendedGamepad* pad = controller.extendedGamepad;
        const auto merge_axis = [](float keyboard, float controller_axis) {
          return std::abs(controller_axis) > std::abs(keyboard) ? controller_axis : keyboard;
        };
        movement[0] = merge_axis(movement[0], pad.leftThumbstick.xAxis.value);
        movement[1] = merge_axis(movement[1], pad.leftThumbstick.yAxis.value);
        look[0] = merge_axis(look[0], pad.rightThumbstick.xAxis.value);
        look[1] = merge_axis(look[1], pad.rightThumbstick.yAxis.value);
        constexpr float kEvadeTriggerThreshold = 0.1F;
        evade = evade || pad.leftTrigger.value > kEvadeTriggerThreshold;
        pause = pause || pad.buttonMenu.pressed;
        crouch = crouch || pad.leftThumbstickButton.pressed;
        fire_trigger = std::max(fire_trigger, pad.rightTrigger.value);
        action_holds |= macos::gamepad_action_holds(pad);
        if (pad.buttonA.pressed || pad.buttonMenu.pressed) {
          held |= kConfirmAction;
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
      state.keyboard_keys = 0;
      state.gameplay_keys = 0;
      movement = {};
      look = {};
      evade = false;
      pause = false;
      held = 0;
      key_edges = 0;
      evade_key_edge = false;
      pause_key_edge = false;
      crouch_key_edge = false;
      fire_trigger = 0;
      action_holds = 0;
      action_key_edges = 0;
    }
    // Keep press edges distinct from the recovered per-bit repeat timing.
    const auto pressed = key_edges | (held & ~state.previous_held);
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed = std::chrono::duration<double>(now - state.input_time).count();
    state.input_time = now;
    auto repeated = pressed;
    constexpr double repeat_delay = 0.5;
    constexpr double repeat_interval = 0.125;
    for (std::size_t bit = 0; bit < state.repeat_elapsed.size(); ++bit) {
      const auto mask = std::uint32_t{1} << bit;
      auto& timer = state.repeat_elapsed.at(bit);
      if ((held & mask) == 0) {
        timer = 0;
        continue;
      }
      // A new hold starts now; time spent idle/loading must not become a repeat.
      timer += (state.previous_held & mask) != 0 ? elapsed : 0;
      if (timer >= repeat_delay) {
        repeated |= mask;
        timer -= repeat_interval;
      }
    }
    state.previous_held = held;
    const bool evade_held = evade_key_edge || evade;
    const bool pause_pressed = pause_key_edge || (pause && !state.previous_pause);
    state.previous_pause = pause;
    const bool crouch_pressed = crouch_key_edge || (crouch && !state.previous_crouch);
    state.previous_crouch = crouch;
    const auto action_pressed = state.action_edges.sample(action_holds, action_key_edges);
    return {.pressed_actions = pressed,
            .repeated_actions = repeated,
            .close_requested = !state.window.visible,
            .movement = movement,
            .look = look,
            .evade_held = evade_held,
            .pause_pressed = pause_pressed,
            .crouch_pressed = crouch_pressed,
            .fire_trigger = fire_trigger,
            .reload_pressed = (action_pressed & kReload) != 0,
            .attack_pressed = (action_pressed & kAttack) != 0,
            .weapon_cycle_pressed = (action_pressed & kWeaponCycle) != 0,
            .aim_mode_pressed = (action_pressed & kAimMode) != 0};
  }
}
void StartupHost::upload(const video::VideoFrame& frame) {
  require_main_thread();
  constexpr std::uint32_t max_dimension = 8192;
  constexpr std::size_t channels = 4;
  if (frame.width == 0 || frame.height == 0 || frame.width > max_dimension ||
      frame.height > max_dimension ||
      frame.bgra.size() != static_cast<std::size_t>(frame.width) * frame.height * channels) {
    throw std::runtime_error("Invalid BGRA video frame");
  }
  @autoreleasepool {
    auto& state = *state_;
    if (state.video.get() == nil || state.video.get().width != frame.width ||
        state.video.get().height != frame.height) {
      // Publish resized storage only after its upload succeeds.
      auto replacement = macos::TextureAllocation::create(
          state.device, state.device_budget, frame.width, frame.height, MTLPixelFormatBGRA8Unorm);
      replacement.upload(frame.bgra);
      state.video = std::move(replacement);
    } else {
      state.video.upload(frame.bgra);
    }
  }
}
void StartupHost::clear_video() noexcept { state_->video.reset(); }
bool StartupHost::draw(float opacity) {
  require_main_thread();
  if (!std::isfinite(opacity) || opacity < 0 || opacity > 1) {
    throw std::runtime_error("Invalid movie opacity");
  }
  @autoreleasepool {
    auto& state = *state_;
    const auto bounds = state.window.contentView.bounds;
    const auto scale = state.window.backingScaleFactor;
    state.layer.drawableSize = CGSizeMake(bounds.size.width * scale, bounds.size.height * scale);
    id<CAMetalDrawable> drawable = [state.layer nextDrawable];
    if (drawable == nil) {
      return false;  // Minimized/occluded windows may have no drawable.
    }
    MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
    pass.colorAttachments[0].texture = drawable.texture;
    pass.colorAttachments[0].loadAction = MTLLoadActionClear;
    pass.colorAttachments[0].storeAction = MTLStoreActionStore;
    pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
    id<MTLCommandBuffer> command = [state.commands commandBuffer];
    const double canvas_width = static_cast<double>(drawable.texture.width);
    const double canvas_height = static_cast<double>(drawable.texture.height);
    const double canvas_fit =
        std::min(canvas_width / render::kCanvasWidth, canvas_height / render::kCanvasHeight);
    const auto scene_width = static_cast<std::uint32_t>(render::kCanvasWidth * canvas_fit);
    const auto scene_height = static_cast<std::uint32_t>(render::kCanvasHeight * canvas_fit);
    // Fit the logical canvas even when only title/menu sprites are resident.
    // Pixel alignment also preserves the world pass's destination sample offset.
    const MTLViewport canvas_viewport{std::floor((canvas_width - scene_width) / 2),
                                      std::floor((canvas_height - scene_height) / 2),
                                      static_cast<double>(scene_width),
                                      static_cast<double>(scene_height),
                                      0,
                                      1};
    if (state.world->has_color_draws()) {
      state.world->prepare_presentation(command, state.shader_library, scene_width, scene_height);
    }
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (command == nil || encoder == nil) {
      throw std::runtime_error("Could not encode movie presentation");
    }
    if (state.world->has_color_draws()) {
      // Preserve the inspection projection's logical aspect through the same
      // centered canvas fit used by menu presentation; clear retains black bars.
      [encoder setViewport:canvas_viewport];
      state.world->encode_presentation(encoder, state.shader_library, opacity);
      [encoder endEncoding];
      pass.colorAttachments[0].loadAction = MTLLoadActionLoad;
      pass.depthAttachment.texture = nil;
      encoder = [command renderCommandEncoderWithDescriptor:pass];
      if (encoder == nil) {
        throw std::runtime_error("Could not encode overlays after world presentation");
      }
    }
    // Aspect-fit the movie into the cleared drawable, preserving black bars.
    if (state.video.get() != nil) {
      const double width = static_cast<double>(drawable.texture.width);
      const double height = static_cast<double>(drawable.texture.height);
      const double video_width = static_cast<double>(state.video.get().width);
      const double video_height = static_cast<double>(state.video.get().height);
      const double fit = std::min(width / video_width, height / video_height);
      [encoder setViewport:MTLViewport{(width - video_width * fit) / 2,
                                       (height - video_height * fit) / 2, video_width * fit,
                                       video_height * fit, 0, 1}];
      [encoder setRenderPipelineState:state.pipeline];
      [encoder setFragmentTexture:state.video.get() atIndex:0];
      [encoder setFragmentBytes:&opacity length:sizeof(opacity) atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    // Overlay ordered quads in the shared logical canvas with the same fade.
    if (state.sprite_count != 0) {
      [encoder setViewport:canvas_viewport];
      [encoder setRenderPipelineState:state.sprite_pipeline];
      for (const auto& submitted : std::span(state.sprites).first(state.sprite_count)) {
        const auto& sprite = submitted.sprite;
        const SpriteParameters parameters{
            .rectangle = {sprite.destination.x, sprite.destination.y, sprite.destination.width,
                          sprite.destination.height},
            .uv = {sprite.uv.x, sprite.uv.y, sprite.uv.width, sprite.uv.height},
            .color = {sprite.color.red * opacity, sprite.color.green * opacity,
                      sprite.color.blue * opacity, sprite.color.alpha},
            .sampling = {static_cast<float>(sprite.channel), 0, 0, 0},
            .transform = {std::cos(sprite.rotation_radians), std::sin(sprite.rotation_radians),
                          sprite.rotation_origin_x, sprite.rotation_origin_y}};
        [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:0];
        [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
        [encoder setFragmentTexture:state.textures.at(submitted.texture_slot).native.get()
                            atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      }
    }
    [encoder endEncoding];
    [command presentDrawable:drawable];
    [command commit];
    // Retire all GPU consumers before uploads, releases or frame reuse can follow.
    [command waitUntilCompleted];
    if (command.status == MTLCommandBufferStatusError) {
      throw std::runtime_error("Metal movie command failed");
    }
    return true;
  }
}
render::TextureHandle StartupHost::upload_texture(std::uint32_t width, std::uint32_t height,
                                                  std::span<const std::uint8_t> rgba) {
  require_main_thread();
  constexpr std::uint32_t max_dimension = 8192;
  constexpr std::size_t channels = 4;
  if (width == 0 || height == 0 || width > max_dimension || height > max_dimension ||
      rgba.size() != static_cast<std::size_t>(width) * height * channels) {
    throw std::runtime_error("Invalid RGBA sprite texture");
  }
  auto& state = *state_;
  const auto slot = std::ranges::find_if(
      state.textures, [](const auto& texture) { return texture.native.get() == nil; });
  if (slot == state.textures.end() || rgba.size() > kTextureByteBudget - state.texture_bytes ||
      state.next_texture == std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("Sprite texture residency limit exceeded");
  }
  @autoreleasepool {
    auto texture = macos::TextureAllocation::create(state.device, state.device_budget, width,
                                                    height, MTLPixelFormatRGBA8Unorm);
    texture.upload(rgba);
    // Publish the identity and residency counters only after acquisition and upload.
    slot->handle = {state.next_texture++};
    slot->native = std::move(texture);
    slot->bytes = rgba.size();
    state.texture_bytes += rgba.size();
    return slot->handle;
  }
}
void StartupHost::release_texture(render::TextureHandle texture) noexcept {
  auto& state = *state_;
  const auto slot = std::ranges::find_if(state.textures, [texture](const auto& entry) {
    return entry.handle == texture && entry.native.get() != nil;
  });
  if (slot == state.textures.end()) {
    return;
  }
  state.texture_bytes -= slot->bytes;
  *slot = {};
  // Remove queued users of the retired identity before the next draw.
  auto commands = std::span(state.sprites).first(state.sprite_count);
  const auto removed = std::ranges::remove_if(
      commands, [texture](const auto& entry) { return entry.sprite.texture == texture; });
  state.sprite_count -= static_cast<std::size_t>(removed.size());
}
void StartupHost::set_sprites(std::span<const render::Sprite> sprites) {
  require_main_thread();
  if (sprites.size() > render::kMaximumSprites) {
    throw std::runtime_error("Sprite command limit exceeded");
  }
  auto& state = *state_;
  // Validate and resolve every texture before mutating the previous submission.
  std::array<std::size_t, render::kMaximumSprites> slots{};
  std::size_t index = 0;
  for (const auto& sprite : sprites) {
    const auto& uv = sprite.uv;
    const auto& color = sprite.color;
    if (!std::isfinite(sprite.rotation_radians) || !std::isfinite(sprite.rotation_origin_x) ||
        !std::isfinite(sprite.rotation_origin_y) || !valid_rectangle(sprite.destination) ||
        !valid_rectangle(uv) || !unit_value(uv.x) || !unit_value(uv.y) ||
        !unit_value(uv.x + uv.width) || !unit_value(uv.y + uv.height) || !unit_value(color.red) ||
        !unit_value(color.green) || !unit_value(color.blue) || !unit_value(color.alpha) ||
        sprite.channel > render::TextureChannel::alpha) {
      throw std::runtime_error("Invalid sprite geometry or color");
    }
    const auto texture = std::ranges::find_if(state.textures, [&sprite](const auto& entry) {
      return entry.handle == sprite.texture && entry.native.get() != nil;
    });
    if (texture == state.textures.end()) {
      throw std::runtime_error("Unknown sprite texture");
    }
    slots.at(index++) = static_cast<std::size_t>(texture - state.textures.begin());
  }
  // Commit the complete command list into fixed storage after validation succeeds.
  index = 0;
  for (const auto& sprite : sprites) {
    state.sprites.at(index) = {.sprite = sprite, .texture_slot = slots.at(index)};
    ++index;
  }
  state.sprite_count = sprites.size();
}
void StartupHost::clear_sprites() noexcept { state_->sprite_count = 0; }

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
