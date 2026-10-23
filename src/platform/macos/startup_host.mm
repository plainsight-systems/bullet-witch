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
#include "bw/platform/media_audio.hpp"

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
constexpr unsigned short kEscapeKey = 53;
constexpr unsigned short kFullscreenKey = 3;
constexpr unsigned short kUpKey = 126, kDownKey = 125, kLeftKey = 123, kRightKey = 124;
constexpr std::size_t kActionBits = 32;
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
constexpr std::size_t kTextureByteBudget = std::size_t{128} * 1024 * 1024;
struct SpriteParameters {
  std::array<float, 4> rectangle;
  std::array<float, 4> uv;
  std::array<float, 4> color;
  std::array<float, 4> sampling;
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
  id<MTLTexture> video{};
  id<MTLRenderPipelineState> sprite_pipeline{};
  struct Texture {
    render::TextureHandle handle;
    id<MTLTexture> native{};
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
  std::uint32_t previous_held{};
  std::array<double, kActionBits> repeat_elapsed{};
  std::chrono::steady_clock::time_point input_time{std::chrono::steady_clock::now()};
  State() = default;
  ~State() {
    media_audio.stop();
    [window close];
  }
  State(const State&) = delete;
  State& operator=(const State&) = delete;
  State(State&&) = delete;
  State& operator=(State&&) = delete;
};

StartupHost::StartupHost(WindowOptions options) : state_(std::make_unique<State>()) {
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
    NSError* error = nil;
    std::uint32_t path_size = 0;
    (void)_NSGetExecutablePath(nullptr, &path_size);
    std::vector<char> executable_path(path_size);
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
    MTLRenderPipelineDescriptor* descriptor = [[MTLRenderPipelineDescriptor alloc] init];
    descriptor.vertexFunction = [library newFunctionWithName:@"movie_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"movie_fragment"];
    descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
    state.pipeline = [state.device newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (state.commands == nil || state.pipeline == nil) {
      throw std::runtime_error("Could not initialize Metal movie pipeline");
    }
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
          }
          state.keyboard_keys |= key;
        } else {
          state.keyboard_keys &= ~key;
        }
      } else {
        [NSApp sendEvent:event];
      }
    }
    [NSApp updateWindows];
    std::uint32_t held = ((state.keyboard_keys & 3U) != 0 ? kConfirmAction : 0U) |
                         ((state.keyboard_keys & 4U) != 0 ? kCancelAction : 0U) |
                         (state.keyboard_keys & kDirections);
    if (NSApp.active && state.window.keyWindow) {
      NSArray<GCController*>* controllers = [GCController controllers];
      for (NSUInteger index = 0; index < controllers.count; ++index) {
        GCController* controller = controllers[index];
        GCExtendedGamepad* pad = controller.extendedGamepad;
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
      state.keyboard_keys = 0;
      held = 0;
      key_edges = 0;
    }
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
    return {.pressed_actions = pressed,
            .repeated_actions = repeated,
            .close_requested = !state.window.visible};
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
    if (state.video == nil || state.video.width != frame.width ||
        state.video.height != frame.height) {
      MTLTextureDescriptor* descriptor =
          [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                             width:frame.width
                                                            height:frame.height
                                                         mipmapped:NO];
      descriptor.storageMode = MTLStorageModeShared;
      descriptor.usage = MTLTextureUsageShaderRead;
      state.video = [state.device newTextureWithDescriptor:descriptor];
      if (state.video == nil) {
        throw std::runtime_error("Could not allocate movie texture");
      }
    }
    [state.video replaceRegion:MTLRegionMake2D(0, 0, frame.width, frame.height)
                   mipmapLevel:0
                     withBytes:frame.bgra.data()
                   bytesPerRow:static_cast<std::size_t>(frame.width) * channels];
  }
}
void StartupHost::clear_video() noexcept { state_->video = nil; }
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
    id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
    if (command == nil || encoder == nil) {
      throw std::runtime_error("Could not encode movie presentation");
    }
    if (state.video != nil) {
      const double width = static_cast<double>(drawable.texture.width);
      const double height = static_cast<double>(drawable.texture.height);
      const double video_width = static_cast<double>(state.video.width);
      const double video_height = static_cast<double>(state.video.height);
      const double fit = std::min(width / video_width, height / video_height);
      [encoder setViewport:MTLViewport{(width - video_width * fit) / 2,
                                       (height - video_height * fit) / 2, video_width * fit,
                                       video_height * fit, 0, 1}];
      [encoder setRenderPipelineState:state.pipeline];
      [encoder setFragmentTexture:state.video atIndex:0];
      [encoder setFragmentBytes:&opacity length:sizeof(opacity) atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
    }
    if (state.sprite_count != 0) {
      const double width = static_cast<double>(drawable.texture.width);
      const double height = static_cast<double>(drawable.texture.height);
      const double fit = std::min(width / render::kCanvasWidth, height / render::kCanvasHeight);
      [encoder
          setViewport:MTLViewport{(width - render::kCanvasWidth * fit) / 2,
                                  (height - render::kCanvasHeight * fit) / 2,
                                  render::kCanvasWidth * fit, render::kCanvasHeight * fit, 0, 1}];
      [encoder setRenderPipelineState:state.sprite_pipeline];
      for (const auto& submitted : std::span(state.sprites).first(state.sprite_count)) {
        const auto& sprite = submitted.sprite;
        const SpriteParameters parameters{
            .rectangle = {sprite.destination.x / render::kCanvasWidth,
                          sprite.destination.y / render::kCanvasHeight,
                          sprite.destination.width / render::kCanvasWidth,
                          sprite.destination.height / render::kCanvasHeight},
            .uv = {sprite.uv.x, sprite.uv.y, sprite.uv.width, sprite.uv.height},
            .color = {sprite.color.red * opacity, sprite.color.green * opacity,
                      sprite.color.blue * opacity, sprite.color.alpha},
            .sampling = {static_cast<float>(sprite.channel), 0, 0, 0}};
        [encoder setVertexBytes:&parameters length:sizeof(parameters) atIndex:0];
        [encoder setFragmentBytes:&parameters length:sizeof(parameters) atIndex:0];
        [encoder setFragmentTexture:state.textures.at(submitted.texture_slot).native atIndex:0];
        [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      }
    }
    [encoder endEncoding];
    [command presentDrawable:drawable];
    [command commit];
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
  const auto slot = std::ranges::find_if(state.textures,
                                         [](const auto& texture) { return texture.native == nil; });
  if (slot == state.textures.end() || rgba.size() > kTextureByteBudget - state.texture_bytes ||
      state.next_texture == std::numeric_limits<std::uint64_t>::max()) {
    throw std::runtime_error("Sprite texture residency limit exceeded");
  }
  @autoreleasepool {
    MTLTextureDescriptor* descriptor =
        [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm
                                                           width:width
                                                          height:height
                                                       mipmapped:NO];
    descriptor.storageMode = MTLStorageModeShared;
    descriptor.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> texture = [state.device newTextureWithDescriptor:descriptor];
    if (texture == nil) {
      throw std::runtime_error("Could not allocate sprite texture");
    }
    [texture replaceRegion:MTLRegionMake2D(0, 0, width, height)
               mipmapLevel:0
                 withBytes:rgba.data()
               bytesPerRow:static_cast<std::size_t>(width) * channels];
    slot->handle = {state.next_texture++};
    slot->native = texture;
    slot->bytes = rgba.size();
    state.texture_bytes += rgba.size();
    return slot->handle;
  }
}
void StartupHost::release_texture(render::TextureHandle texture) noexcept {
  auto& state = *state_;
  const auto slot = std::ranges::find_if(state.textures, [texture](const auto& entry) {
    return entry.handle == texture && entry.native != nil;
  });
  if (slot == state.textures.end()) {
    return;
  }
  state.texture_bytes -= slot->bytes;
  *slot = {};
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
  std::array<std::size_t, render::kMaximumSprites> slots{};
  std::size_t index = 0;
  for (const auto& sprite : sprites) {
    const auto& uv = sprite.uv;
    const auto& color = sprite.color;
    if (!valid_rectangle(sprite.destination) || !valid_rectangle(uv) || !unit_value(uv.x) ||
        !unit_value(uv.y) || !unit_value(uv.x + uv.width) || !unit_value(uv.y + uv.height) ||
        !unit_value(color.red) || !unit_value(color.green) || !unit_value(color.blue) ||
        !unit_value(color.alpha) || sprite.channel > render::TextureChannel::alpha) {
      throw std::runtime_error("Invalid sprite geometry or color");
    }
    const auto texture = std::ranges::find_if(state.textures, [&sprite](const auto& entry) {
      return entry.handle == sprite.texture && entry.native != nil;
    });
    if (texture == state.textures.end()) {
      throw std::runtime_error("Unknown sprite texture");
    }
    slots.at(index++) = static_cast<std::size_t>(texture - state.textures.begin());
  }
  index = 0;
  for (const auto& sprite : sprites) {
    state.sprites.at(index) = {.sprite = sprite, .texture_slot = slots.at(index)};
    ++index;
  }
  state.sprite_count = sprites.size();
}
void StartupHost::clear_sprites() noexcept { state_->sprite_count = 0; }
video::AudioOutput& StartupHost::audio() noexcept { return state_->media_audio; }
}  // namespace bw::platform
