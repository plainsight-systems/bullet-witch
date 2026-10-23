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
#include <climits>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <mach-o/dyld.h>

@interface BWStartupWindowDelegate : NSObject <NSWindowDelegate>
@property(nonatomic) BOOL closeEnabled;
@end
@implementation BWStartupWindowDelegate
- (BOOL)windowShouldClose:(NSWindow*)sender {
  (void)sender;
  return self.closeEnabled;
}
@end

namespace bw::platform {
namespace {
constexpr std::uint32_t kConfirmAction = 0x1010U;
constexpr std::uint32_t kCancelAction = 0x2020U;
constexpr unsigned short kReturnKey = 36;
constexpr unsigned short kSpaceKey = 49;
constexpr unsigned short kEscapeKey = 53;
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
  MediaAudio media_audio;
  std::uint32_t keyboard_keys{};
  std::uint32_t previous_held{};
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

StartupHost::StartupHost() : state_(std::make_unique<State>()) {
  require_main_thread();
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
    constexpr CGFloat width = 960;
    constexpr CGFloat height = 540;
    state.window = [[NSWindow alloc]
        initWithContentRect:NSMakeRect(0, 0, width, height)
                  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
                            NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable
                    backing:NSBackingStoreBuffered
                      defer:NO];
    state.window.releasedWhenClosed = NO;
    state.window_delegate = [[BWStartupWindowDelegate alloc] init];
    state.window.delegate = state.window_delegate;
    state.window.title =
        NSLocalizedString(@"Bullet Witch — startup reconstruction", @"Startup window title");
    constexpr CGFloat aspect_width = 16;
    constexpr CGFloat aspect_height = 9;
    state.window.contentAspectRatio = NSMakeSize(aspect_width, aspect_height);
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
        const std::uint32_t key =
            code == kReturnKey ? 1U : (code == kSpaceKey ? 2U : (code == kEscapeKey ? 4U : 0U));
        const std::uint32_t action =
            code == kEscapeKey ? kCancelAction
                               : (code == kReturnKey || code == kSpaceKey ? kConfirmAction : 0);
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
                         ((state.keyboard_keys & 4U) != 0 ? kCancelAction : 0U);
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
      }
    } else {
      state.keyboard_keys = 0;
      held = 0;
      key_edges = 0;
    }
    const auto pressed = key_edges | (held & ~state.previous_held);
    state.previous_held = held;
    return {.pressed_actions = pressed, .close_requested = !state.window.visible};
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
video::AudioOutput& StartupHost::audio() noexcept { return state_->media_audio; }
}  // namespace bw::platform
