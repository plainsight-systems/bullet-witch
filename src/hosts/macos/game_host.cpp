// game_host: the first stage of the game executable. It composes: the
// window (bw/platform/os/macos/window.hpp), the Metal device and the
// viewport on the window's surface (bw/rendering/rhi/device.hpp,
// viewport.hpp), and a frame loop on the main thread that runs until the
// window is asked to close or the seconds limit passes. What it draws is
// chosen by its options: without --asset the cube scene (cube_scene.hpp);
// with --cache and --asset the model scene (model_scene.hpp). The shell
// holds the one scene through host_scene.hpp and records it each frame
// between the back buffer's two transitions.
//
// Command line: bw/hosts/game_host_options.hpp states the rows and parses
// them. Without --shaders the directory is
// <executable directory>/shaders/metal, the executable's path read from the
// OS, not from argv[0], and resolved through symbolic links
// (bw/platform/process/executable_directory.hpp).
//
// Exit codes: 0 when the window asked to close or the seconds limit passed,
// with one line on stdout, `frames=N elapsed=S waited=W`: the frames drawn,
// the elapsed seconds and how many end_frame calls blocked on the oldest
// frame in flight (with --asset, the base_color_fallback line of
// model_scene.hpp precedes it); 1 on a named failure, one line on stderr
// naming what failed and, for an error code, the enumerator's number and
// the detail; 2 on a usage error, with a line naming the error and the
// argument, then the usage line, on stderr. Named failures include an
// executable path the OS cannot give or resolve when --shaders is absent,
// the OS refusing the borderless fullscreen --fullscreen asked for, and for
// --asset: layers that do not open (with a second line naming the
// content_converter command for every failure of the converted layer), a
// model that does not load (the id concerned and the failing layer's own
// error) and a resource the RHI refuses. No failure falls back: every
// failure ends the run.
//
// Window events: Resized resizes the viewport to the event's pixel size;
// CloseRequested ends the run; ModeChanged, FocusChanged and, without
// --fullscreen, ModeRefused print one stdout line each. A frame whose back
// buffer's size does not match the viewport's (BackBufferSizeMismatch) is
// not drawn: the viewport is resized to the window's pixel size and the
// next frame is drawn (viewport.hpp, invariant 4); a second mismatch before
// any frame is drawn means the resize did not take, and is a named failure.
//
// The host owns its loop; the headless engine loop stays engine_host's. One
// thread, the process main thread, owns the window, the viewport and the
// submission (window.hpp, Threads; device.hpp, Viewports).
#include "cube_scene.hpp"
#include "host_failure.hpp"
#include "host_scene.hpp"
#include "model_scene.hpp"

#include "bw/hosts/game_host_options.hpp"
#include "bw/hosts/option_table.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/process/executable_directory.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace platform = bw::platform;
namespace rhi = bw::rhi;

using bw::hosts::fail;

// ---- The host ------------------------------------------------------------------------

const char* mode_name(platform::WindowMode mode) {
  return mode == platform::WindowMode::BorderlessFullscreen ? "borderless_fullscreen" : "windowed";
}

class Host {
public:
  explicit Host(bw::hosts::GameHostOptions options) : options_(std::move(options)) {}

  // Creates everything and runs the frame loop; the exit code.
  int run();

  // Waits for the GPU, then destroys in order: command list, resources,
  // viewport, device, window.
  void shut_down();

private:
  // What one frame did. Resized: the back buffer's size did not match, the
  // frame was not drawn and the viewport now has the window's pixel size.
  // Failed: the failure line is printed and the run ends with exit 1.
  enum class FrameOutcome : std::uint8_t { Drawn, Resized, Failed };

  int run_frames();
  // One frame: the recorded list, execute, end_frame.
  FrameOutcome draw_frame(double elapsed);
  // The answer to BackBufferSizeMismatch (viewport.hpp, invariant 4).
  FrameOutcome resize_after_mismatch();

  bw::hosts::GameHostOptions options_;
  bw::platform::macos::PlatformTime time_{};
  std::unique_ptr<platform::Window> window_{};
  std::unique_ptr<rhi::Device> device_{};
  rhi::Ref<rhi::Viewport> viewport_{};
  // The one scene: the asset's with --asset, else the cube's.
  std::unique_ptr<bw::hosts::HostScene> scene_{};
  std::unique_ptr<rhi::CommandList> list_{};
  std::uint64_t frames_{};
  std::uint64_t waited_frames_{};
  // A mismatch resized the viewport and no frame has been drawn since. Only
  // a drawn frame clears it: a resize from a Resized event in between is
  // another explicit resize, and a mismatch after it still means the
  // resize did not take.
  bool resized_after_mismatch_{};
};

int Host::run() {
  platform::WindowDescription window_description{};
  window_description.title = "Bullet Witch";
  window_description.width = 1280;
  window_description.height = 720;
  window_description.mode = options_.fullscreen ? platform::WindowMode::BorderlessFullscreen
                                                : platform::WindowMode::Windowed;
  platform::WindowError window_error = platform::WindowError::None;
  window_ = bw::platform::macos::create_window(window_description, window_error);
  if (window_ == nullptr) {
    return fail("window creation", window_error, "");
  }

  // The defaults: the Metal backend, and in debug builds the validation layer
  // with the default reporter, which aborts on a failure.
  rhi::DeviceResult device = rhi::Device::create(rhi::DeviceDescription{});
  if (!device.ok()) {
    return fail("device creation", device.error(), device.detail());
  }
  device_ = std::move(device.value());

  const platform::PixelSize pixels = window_->pixel_size();
  rhi::ViewportDescription viewport_description{};
  viewport_description.window = window_->native_handle();
  viewport_description.pixel_size = rhi::Extent2{pixels.width, pixels.height};
  viewport_description.format = rhi::Format::B8G8R8A8UnormSrgb;
  viewport_description.vsync = true;
  viewport_description.debug_name = "game_host viewport";
  rhi::ViewportResult viewport = device_->create_viewport(viewport_description);
  if (!viewport.ok()) {
    return fail("viewport creation", viewport.error(), viewport.detail());
  }
  viewport_ = std::move(viewport.value());

  // The scene is held before it is built, so a scene whose build failed is
  // destroyed by shut_down in the same place as a built one.
  if (options_.asset.empty()) {
    auto cube = std::make_unique<bw::hosts::Scene>();
    bw::hosts::Scene& scene = *cube;
    scene_ = std::move(cube);
    if (!bw::hosts::build_scene(*device_, options_.shaders, viewport_->format(), scene)) {
      return 1;
    }
  } else {
    auto asset = std::make_unique<bw::hosts::AssetScene>();
    bw::hosts::AssetScene& scene = *asset;
    scene_ = std::move(asset);
    if (!bw::hosts::build_asset_scene(*device_, options_, viewport_->format(), scene)) {
      return 1;
    }
  }
  list_ = std::make_unique<rhi::CommandList>();
  return run_frames();
}

int Host::run_frames() {
  const double start = time_.seconds();
  double elapsed = 0.0;
  for (;;) {
    window_->pump();
    bool close = false;
    platform::WindowEvent event{};
    while (window_->poll_event(event)) {
      switch (event.kind) {
      case platform::WindowEventKind::Resized: {
        const rhi::Status<rhi::ViewportError> resized =
            device_->resize_viewport(*viewport_, rhi::Extent2{event.size.width, event.size.height});
        if (!resized.ok()) {
          return fail("viewport resize", resized.error(), resized.detail());
        }
        break;
      }
      case platform::WindowEventKind::CloseRequested:
        close = true;
        break;
      case platform::WindowEventKind::ModeChanged:
        std::cout << "mode_changed mode=" << mode_name(event.mode) << '\n';
        break;
      case platform::WindowEventKind::ModeRefused:
        // The one mode request this host makes is --fullscreen's, at window
        // creation; its refusal ends the run rather than running windowed.
        if (options_.fullscreen) {
          std::cerr << "game_host: borderless fullscreen failed: the OS refused the transition and "
                       "the window stays "
                    << mode_name(event.mode) << '\n';
          return 1;
        }
        std::cout << "mode_refused mode=" << mode_name(event.mode) << '\n';
        break;
      case platform::WindowEventKind::FocusChanged:
        std::cout << "focus_changed focused=" << (event.focused ? 1 : 0) << '\n';
        break;
      }
    }
    if (close) {
      break;
    }
    elapsed = time_.seconds() - start;
    if (draw_frame(elapsed) == FrameOutcome::Failed) {
      return 1;
    }
    elapsed = time_.seconds() - start;
    if (bw::hosts::seconds_limit_reached(options_.seconds, elapsed)) {
      break;
    }
  }
  std::cout << "frames=" << frames_ << " elapsed=" << elapsed << " waited=" << waited_frames_
            << '\n';
  return 0;
}

Host::FrameOutcome Host::draw_frame(double elapsed) {
  rhi::Texture& back_buffer = viewport_->back_buffer();
  const rhi::Extent2 size = viewport_->size();

  rhi::CommandList& list = *list_;
  list.reset();
  const rhi::Transition to_target{&back_buffer, rhi::Access::Present, rhi::Access::RenderTarget};
  list.transition(rhi::TransitionBatch{std::span<const rhi::Transition>{&to_target, 1}});
  if (!scene_->record(*device_, list, back_buffer, size, elapsed)) {
    return FrameOutcome::Failed;
  }
  const rhi::Transition to_present{&back_buffer, rhi::Access::RenderTarget, rhi::Access::Present};
  list.transition(rhi::TransitionBatch{std::span<const rhi::Transition>{&to_present, 1}});

  const rhi::Status<rhi::ExecuteError> executed = device_->execute(list);
  if (!executed.ok()) {
    if (executed.error() == rhi::ExecuteError::BackBufferSizeMismatch) {
      return resize_after_mismatch();
    }
    fail("frame execute", executed.error(), executed.detail());
    return FrameOutcome::Failed;
  }
  const rhi::EndFrameResult ended = device_->end_frame();
  ++frames_;
  resized_after_mismatch_ = false;
  if (ended.waited) {
    ++waited_frames_;
  }
  // The frame's results are undefined after a GPU fault (device.hpp,
  // EndFrameResult::gpu_fault); the host does not draw on past one.
  if (ended.gpu_fault) {
    std::cerr << "game_host: the GPU reported a fault in work completed by frame "
              << ended.completed << '\n';
    return FrameOutcome::Failed;
  }
  return FrameOutcome::Drawn;
}

// The replay stopped at the pass that would have acquired the back buffer,
// and a mismatch holds nothing (viewport.hpp, after_acquire), so the resize
// is accepted in the frame being recorded (validate_resize). The frame is
// not drawn: it gets no end_frame and is not counted, and the next loop
// iteration records it again under the same frame number; what the replay
// ran before the failed pass (the transition of the replaced back buffer)
// is submitted with that frame, and the replaced back buffer stays alive in
// the delete queue until the frame completes. The size is the window's
// current pixel size, not a size from an earlier event.
Host::FrameOutcome Host::resize_after_mismatch() {
  if (resized_after_mismatch_) {
    std::cerr << "game_host: frame execute failed: the back buffer's size still differs from the "
                 "viewport's after the viewport was resized to the window's pixel size\n";
    return FrameOutcome::Failed;
  }
  const platform::PixelSize pixels = window_->pixel_size();
  const rhi::Status<rhi::ViewportError> resized =
      device_->resize_viewport(*viewport_, rhi::Extent2{pixels.width, pixels.height});
  if (!resized.ok()) {
    fail("viewport resize after a back buffer size mismatch", resized.error(), resized.detail());
    return FrameOutcome::Failed;
  }
  resized_after_mismatch_ = true;
  return FrameOutcome::Resized;
}

void Host::shut_down() {
  if (device_ != nullptr) {
    device_->wait_idle();
  }
  list_.reset();
  scene_.reset();
  viewport_.reset();
  device_.reset();
  window_.reset();
}

} // namespace

// An exception (allocation, path handling) is a failure like any other: one
// line on stderr and exit 1. The host's members are declared in teardown
// order, so leaving by an exception destroys the command list, the
// resources, the viewport, the device (which waits for the GPU) and the
// window in that order.
int main(int argc, char** argv) {
  try {
    const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
    std::vector<std::string_view> arguments;
    arguments.reserve(raw.size());
    for (std::size_t i = 1; i < raw.size(); ++i) {
      arguments.emplace_back(raw[i]);
    }
    bw::hosts::GameHostParse parse = bw::hosts::parse_game_host_options(arguments);
    if (parse.error != bw::hosts::OptionError::None) {
      std::cerr << "game_host: usage error " << static_cast<unsigned>(parse.error) << ": "
                << parse.argument;
      if (!parse.reason.empty()) {
        std::cerr << " " << parse.reason;
      }
      std::cerr << '\n' << bw::hosts::game_host_usage() << '\n';
      return 2;
    }
    bw::hosts::GameHostOptions options = std::move(parse.options);
    if (options.shaders.empty()) {
      std::filesystem::path directory;
      std::string detail;
      if (!bw::platform::executable_directory(directory, detail)) {
        std::cerr << "game_host: resolving the executable's directory for the default --shaders "
                     "failed: "
                  << detail << '\n';
        return 1;
      }
      options.shaders = directory / "shaders" / "metal";
    }
    Host host(std::move(options));
    const int code = host.run();
    host.shut_down();
    return code;
  } catch (const std::exception& error) {
    std::cerr << "game_host: unexpected exception: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "game_host: unexpected exception of unknown type\n";
    return 1;
  }
}
