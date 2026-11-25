// game_host: the first stage of the game executable. It composes: the
// window (bw/platform/os/macos/window.hpp), the Metal device and the
// viewport on the window's surface (bw/rendering/rhi/device.hpp,
// viewport.hpp), and a frame loop on the main thread that runs until the
// window is asked to close or the seconds limit passes. What it draws is
// chosen by its options: without --asset the cube scene (cube_scene.hpp);
// with --cache and --asset the engine's model scene through its adapter
// (model_scene.hpp, bw/rendering/model_scene.hpp), whose light the post
// chain takes to the back buffer, seen through the player's camera of the
// world's tick (below). The viewport's format follows: the cube
// draws linear colour into B8G8R8A8UnormSrgb, which the surface encodes;
// the asset's chain writes values its table has already encoded into
// B8G8R8A8Unorm (bw/rendering/rhi/viewport.hpp, invariant 7). The shell
// holds the one scene through host_scene.hpp and one frame graph
// (bw/rendering/graph/builder.hpp) with the schedule policy of the device's
// backend; each frame it imports the back buffer in the state the viewport
// hands it in, marks it presented, lets the scene declare its passes, then
// compiles the graph and records it into the frame's command list, and asks
// the scene whether every pass recorded what it declared before the device
// executes the list (host_scene.hpp, frame_recorded). The
// scene is given the frame's elapsed seconds, the seconds since the last
// frame drawn, whether no frame has been drawn yet, the count of frames
// drawn and the viewport's display report (host_scene.hpp, HostFrame); the
// clock is read where it was before, once before each frame, and nothing
// else reads it. A backend without a schedule policy in this build is a
// named failure at startup.
//
// The world's tick, with --asset (bw/hosts/game_host_options.hpp): the
// world (bw/world/world.hpp) holds one object, her place,
// alicia::kBodyBelowPlace above the origin with the identity rotation; the player's input
// (bw/input/player_input.hpp) holds the game's tables (bw/game/input/actions.hpp); the player's
// controller (bw/characters/player_controller.hpp) is registered in the before-simulation stage and
// possesses the object; the evaluator (bw/camera/evaluator.hpp) is the world's camera for that
// object, with Alicia's view (bw/game/characters/alicia/view.hpp) as its director. Each loop: the
// window is pumped; without --script the window's device samples are handed to key state and the
// cursor is captured while the window is key (a refused capture prints `capture refused` once, and
// `capture held` when a later one succeeds), with --script the script's samples of the tick are
// handed over and no device is read. Without --script the host creates the pad
// (bw/platform/os/macos/pad.hpp) with the world and, before each tick,
// polls it once and hands its samples to key state; when it connects or
// disconnects one stdout line `pad connected=<0|1>` is printed, so a pad
// present at startup prints `pad connected=1` at the first tick. With
// --script no pad is created. Key state then begins its frame; the step is made
// (bw/runtime/step.hpp) from the fixed step and kLongestStep with the
// clock's two readings; the world ticks; a camera error the tick reports
// ends the run as a named failure; a prerequisite cycle the tick
// reports is one stdout line `prerequisite_cycle function=<name>
// prerequisite=<name>`; the frame is drawn from the publication, the view
// at the pose's location looking along its forward with +y up, its vertical
// field of view and its clip planes, the model drawn kBodyBelowPlace below
// the object's transform, so her feet are on the origin, the model scene's cut flag the
// publication's and its clock the world's; after the frame is drawn the marks of a script are
// printed and dumped and the cut flag is cleared. A frame that is not drawn (the size mismatch
// below) is drawn again from the same tick, which is not run again.
//
// Command line: bw/hosts/game_host_options.hpp states the rows and parses
// them. Without --shaders the directory is
// <executable directory>/shaders/metal, the executable's path read from the
// OS, not from argv[0], and resolved through symbolic links
// (bw/platform/process/executable_directory.hpp).
//
// Exit codes: 0 when the window asked to close, the seconds limit passed or
// a script's last tick was drawn,
// with one line on stdout, `frames=N elapsed=S waited=W`: the frames drawn,
// the elapsed seconds and how many end_frame calls blocked on the oldest
// frame in flight; 1 on a named failure, one line on stderr
// naming what failed and, for an error code, the enumerator's number and
// the detail; 2 on a usage error, with a line naming the error and the
// argument, then the usage line, on stderr. Named failures include an
// executable path the OS cannot give or resolve when --shaders is absent,
// the OS refusing the borderless fullscreen --fullscreen asked for, a frame
// graph that does not compile or whose execute fails, and for
// --asset: a post chain pass that recorded less than it declared (the
// chain's failure line, the frame neither executed nor presented), layers that do not open (with a
// second line naming the content_converter command for every failure of the converted layer), a
// model that does not load (the id concerned and the failing layer's own
// error), a resource the RHI refuses, a script that cannot be read or does
// not parse (its line, error number and word), the world's composition
// refused (the call and its error), a tick whose camera evaluation reports
// an error, a tick that publishes no valid camera, a scripted run that the
// window's close request or --seconds ends before the script's last tick,
// and a dump that cannot be read back, encoded or written. No failure falls
// back: every failure ends the run.
//
// Window events: Resized resizes the viewport to the event's pixel size;
// CloseRequested ends the run; ModeChanged, FocusChanged and, without
// --fullscreen, ModeRefused print one stdout line each, FocusChanged also
// saying whether the cursor may be captured; the input events (mouse,
// scroll, keys) are ignored, since the game reads the device samples. A frame whose back
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

#include "bw/camera/evaluator.hpp"
#include "bw/camera/pose.hpp"
#include "bw/capture/png.hpp"
#include "bw/characters/player_controller.hpp"
#include "bw/game/characters/alicia/view.hpp"
#include "bw/game/input/actions.hpp"
#include "bw/hosts/game_host_options.hpp"
#include "bw/hosts/option_table.hpp"
#include "bw/input/key_state.hpp"
#include "bw/input/modifiers.hpp"
#include "bw/input/player_input.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/input/device_input.hpp"
#include "bw/platform/input/pad.hpp"
#include "bw/platform/input/scripted_device.hpp"
#include "bw/platform/os/macos/pad.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/process/executable_directory.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/executor.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/runtime/step.hpp"
#include "bw/world/object.hpp"
#include "bw/world/tick_function.hpp"
#include "bw/world/world.hpp"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <numbers>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace graph = bw::graph;
namespace platform = bw::platform;
namespace rhi = bw::rhi;

using bw::hosts::fail;

// ---- The host ------------------------------------------------------------------------

const char* mode_name(platform::WindowMode mode) {
  return mode == platform::WindowMode::BorderlessFullscreen ? "borderless_fullscreen" : "windowed";
}

// The shortest decimal that reads back as the float, in fixed notation (up
// to nine significant digits for a float), a zero of either sign as `0`.
// The longest such text of a float is 48 characters (a sign and the
// smallest subnormal's 47), so the buffer always holds it.
std::string shortest_decimal(float value) {
  std::array<char, 64> buffer{};
  const float written = value == 0.0F ? 0.0F : value;
  const std::to_chars_result result = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                    written, std::chars_format::fixed);
  return std::string(buffer.data(), result.ptr);
}

// The drawn view of a published pose: from its location along its
// forward, +y up, its vertical field and clip planes.
bw::scene::PerspectiveView view_of(const bw::camera::Pose& pose) {
  bw::scene::PerspectiveView view{};
  view.eye = pose.location;
  view.target = pose.location + bw::camera::forward(pose.yaw_degrees, pose.pitch_degrees);
  view.up = bw::math::Vector3f{0.0F, 1.0F, 0.0F};
  view.vertical_fov = bw::camera::vertical_fov_degrees(pose) * std::numbers::pi_v<float> / 180.0F;
  view.near_plane = pose.near_plane;
  view.far_plane = pose.far_plane;
  return view;
}

// The enumerator's name of a camera error (bw/camera/evaluator.hpp).
std::string_view camera_error_name(bw::camera::CameraError error) {
  switch (error) {
  case bw::camera::CameraError::None:
    return "None";
  case bw::camera::CameraError::UnknownRig:
    return "UnknownRig";
  case bw::camera::CameraError::UnknownAction:
    return "UnknownAction";
  case bw::camera::CameraError::SecondBoom:
    return "SecondBoom";
  case bw::camera::CameraError::DuplicateRig:
    return "DuplicateRig";
  case bw::camera::CameraError::RigsFull:
    return "RigsFull";
  case bw::camera::CameraError::NodesFull:
    return "NodesFull";
  case bw::camera::CameraError::TransitionsFull:
    return "TransitionsFull";
  case bw::camera::CameraError::StackFull:
    return "StackFull";
  }
  return "an unnamed camera error";
}

// The events of an action state joined by '+', in the header's order.
std::string events_text(const bw::input::ActionState& state) {
  std::string text;
  const auto add = [&](bw::input::ActionEvent event, std::string_view name) {
    if (state.has(event)) {
      text += text.empty() ? "" : "+";
      text += name;
    }
  };
  add(bw::input::ActionEvent::Started, "Started");
  add(bw::input::ActionEvent::Fired, "Fired");
  add(bw::input::ActionEvent::Completed, "Completed");
  return text;
}

class Host {
public:
  explicit Host(bw::hosts::GameHostOptions options) : options_(std::move(options)) {}

  // Creates everything and runs the frame loop; the exit code.
  int run();

  // Waits for the GPU, then destroys in order: command list, the frame
  // graph's frame, resources, the dump's staging buffer and fence,
  // viewport, device, window.
  void shut_down();

private:
  // What one frame did. Resized: the back buffer's size did not match, the
  // frame was not drawn and the viewport now has the window's pixel size.
  // Failed: the failure line is printed and the run ends with exit 1.
  enum class FrameOutcome : std::uint8_t { Drawn, Resized, Failed };

  int run_frames();
  // One frame: the graph declared, compiled and executed into the list, the
  // list executed, end_frame.
  FrameOutcome draw_frame(double elapsed);
  // The frame graph of one frame into the list; false after a failure line.
  bool record_frame(rhi::Texture& back_buffer, rhi::Extent2 size, double elapsed);
  // The answer to BackBufferSizeMismatch (viewport.hpp, invariant 4).
  FrameOutcome resize_after_mismatch();

  // --script: the file read and parsed, `scripted_device script=` printed;
  // false after a failure line.
  bool load_script();
  // --asset: the world, the player's input, controller, camera and view;
  // false after a failure line.
  bool set_up_world();
  // Without --script: the window's samples to key state, and the cursor
  // captured while the window is key.
  void read_devices();
  // Without --script, once before each tick: the pad's samples to key
  // state, and its connection line when connected() changed.
  void read_pad();
  // One tick of the world and the scene's inputs from its publication;
  // false after a failure line.
  bool tick_world();
  // --action-log: one line per action with an event this tick.
  void log_actions();
  // After the tick's frame is drawn: its marks' lines and dumps, then the
  // cut flag cleared; false after a failure line.
  bool finish_tick();
  // The staging buffer for the back buffer's layout and the fence reset;
  // false after a failure line.
  bool prepare_dump(const rhi::TextureDescription& back_buffer);
  // The drawn frame's readback encoded as PNG into png_; false after a
  // failure line.
  bool read_dump();
  // png_ written to `path`; false after a failure line.
  bool write_dump_file(const std::filesystem::path& path);

  bw::hosts::GameHostOptions options_;
  bw::platform::macos::PlatformTime time_{};
  std::unique_ptr<platform::Window> window_{};
  std::unique_ptr<rhi::Device> device_{};
  rhi::Ref<rhi::Viewport> viewport_{};
  // The dump's fence (made at startup with --dump) and staging buffer (made
  // at the first dump), and the layout the staging buffer was made for.
  rhi::Ref<rhi::GpuFence> fence_{};
  rhi::Ref<rhi::StagingBuffer> staging_{};
  rhi::StagingLayout staging_layout_{};
  std::vector<std::byte> rgba_{};
  std::vector<std::byte> png_{};
  // The one scene: the asset's with --asset, else the cube's; the asset's
  // also as itself, for the inputs the world gives it each frame.
  std::unique_ptr<bw::hosts::HostScene> scene_{};
  bw::hosts::AssetScene* asset_scene_{};
  std::unique_ptr<rhi::CommandList> list_{};
  // The schedule policy of the device's backend (process lifetime), and the
  // graph declared again each frame with its compiled form; both keep their
  // storage across frames.
  const graph::SchedulePolicy* policy_{};
  graph::FrameGraph graph_{};
  graph::CompiledFrame compiled_{};
  std::uint64_t frames_{};
  std::uint64_t waited_frames_{};
  // The elapsed seconds of the last frame drawn; the next frame's time step
  // is measured from it.
  double drawn_elapsed_{};
  // A mismatch resized the viewport and no frame has been drawn since. Only
  // a drawn frame clears it: a resize from a Resized event in between is
  // another explicit resize, and a mismatch after it still means the
  // resize did not take.
  bool resized_after_mismatch_{};

  // The world's tick (--asset). The controller and the view observe the
  // world, the input, the key state and the evaluator, declared before
  // them so they are destroyed after.
  platform::InputScript script_{};
  std::unique_ptr<bw::world::World> world_{};
  bw::input::KeyState keys_{};
  std::unique_ptr<bw::input::PlayerInput> input_{};
  std::unique_ptr<bw::camera::Evaluator> evaluator_{};
  std::unique_ptr<bw::characters::PlayerController> controller_{};
  bw::game::alicia::PlayerView view_{};
  // The pad (without --script), and whether it was connected at the last
  // poll, for the connection line.
  std::unique_ptr<platform::Pad> pad_{};
  bool pad_connected_{};
  bw::world::ObjectId object_{};
  // The tick to run next, whether it is still to run (its frame not yet
  // drawn), and whether its frame is read back.
  std::uint32_t tick_{};
  bool tick_due_{true};
  bool dump_due_{};
  bool dump_in_frame_{};
  // The clock's reading at the last tick, the step is measured from it.
  double previous_seconds_{};
  // Whether the window is key, from FocusChanged; whether the last capture
  // asked for was refused; whether a script's last tick was drawn.
  bool focused_{};
  bool capture_refused_{};
  bool script_finished_{};
  // Each action's value as of the tick before, for the action log.
  std::vector<bw::input::ActionValue> logged_values_{};
};

int Host::run() {
  if (!options_.script.empty() && !load_script()) {
    return 1;
  }
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
  policy_ = graph::schedule_policy_for(device_->capabilities().backend);
  if (policy_ == nullptr) {
    std::cerr << "game_host: the render graph has no schedule policy for the device's backend "
              << static_cast<unsigned>(device_->capabilities().backend) << '\n';
    return 1;
  }

  const platform::PixelSize pixels = window_->pixel_size();
  rhi::ViewportDescription viewport_description{};
  viewport_description.window = window_->native_handle();
  viewport_description.pixel_size = rhi::Extent2{pixels.width, pixels.height};
  viewport_description.format =
      options_.asset.empty() ? rhi::Format::B8G8R8A8UnormSrgb : rhi::Format::B8G8R8A8Unorm;
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
    asset_scene_ = asset.get();
    scene_ = std::move(asset);
    if (!bw::hosts::build_asset_scene(*device_, options_, viewport_->format(), scene)) {
      return 1;
    }
    if (!set_up_world()) {
      return 1;
    }
    if (!options_.dump.empty()) {
      rhi::GpuFenceResult fence = device_->create_fence("game_host dump fence");
      if (!fence.ok()) {
        return fail("dump fence creation", fence.error(), fence.detail());
      }
      fence_ = std::move(fence.value());
    }
  }
  list_ = std::make_unique<rhi::CommandList>();
  return run_frames();
}

int Host::run_frames() {
  const double start = time_.seconds();
  previous_seconds_ = start;
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
        focused_ = event.focused;
        break;
      // The game reads the device samples, not these.
      case platform::WindowEventKind::MouseMoved:
      case platform::WindowEventKind::MouseButton:
      case platform::WindowEventKind::Scrolled:
      case platform::WindowEventKind::Key:
        break;
      }
    }
    if (close) {
      break;
    }
    if (world_ != nullptr) {
      if (options_.script.empty()) {
        read_devices();
      }
      if (tick_due_) {
        if (!tick_world()) {
          return 1;
        }
        tick_due_ = false;
      }
    }
    elapsed = time_.seconds() - start;
    const FrameOutcome frame = draw_frame(elapsed);
    if (frame == FrameOutcome::Failed) {
      return 1;
    }
    if (frame == FrameOutcome::Drawn && world_ != nullptr) {
      if (!finish_tick()) {
        return 1;
      }
      tick_due_ = true;
      // A scripted run ends after its last tick is drawn.
      if (!options_.script.empty() && tick_ == platform::last_tick(script_)) {
        script_finished_ = true;
        elapsed = time_.seconds() - start;
        break;
      }
      ++tick_;
    }
    elapsed = time_.seconds() - start;
    if (bw::hosts::seconds_limit_reached(options_.seconds, elapsed)) {
      break;
    }
  }
  if (!options_.script.empty() && !script_finished_) {
    std::cerr << "game_host: the scripted run ended at tick " << tick_
              << " before the script's last tick " << platform::last_tick(script_)
              << " (the window asked to close or --seconds passed)\n";
    return 1;
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
  if (!record_frame(back_buffer, size, elapsed)) {
    return FrameOutcome::Failed;
  }

  const rhi::Status<rhi::ExecuteError> executed = device_->execute(list);
  if (!executed.ok()) {
    if (executed.error() == rhi::ExecuteError::BackBufferSizeMismatch) {
      // The frame's graph did not run to its end; every other failure ends
      // the run. The replay stopped before the readback, so the dump stays
      // due for the frame drawn again.
      dump_in_frame_ = false;
      scene_->frame_dropped();
      return resize_after_mismatch();
    }
    fail("frame execute", executed.error(), executed.detail());
    return FrameOutcome::Failed;
  }
  const rhi::EndFrameResult ended = device_->end_frame();
  ++frames_;
  drawn_elapsed_ = elapsed;
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
  if (dump_in_frame_) {
    dump_in_frame_ = false;
    if (!read_dump()) {
      return FrameOutcome::Failed;
    }
  }
  return FrameOutcome::Drawn;
}

// The back buffer enters the frame in Present, the state the viewport hands
// it in and the state every frame leaves it in, and leaves in Present through
// the mark (graph/declaration.hpp, contract 7). The graph is reset first, so
// the previous frame's handles and imported pointers are gone.
bool Host::record_frame(rhi::Texture& back_buffer, rhi::Extent2 size, double elapsed) {
  graph_.reset();
  const graph::TextureHandle target = graph_.import_texture(
      graph::ImportedTexture{&back_buffer, rhi::Access::Present, rhi::Access::Present});
  graph_.mark_presented(target);
  if (dump_due_) {
    if (!prepare_dump(back_buffer.description())) {
      return false;
    }
    graph_.mark_readback(target, graph::ReadBack{staging_.get(), fence_.get(), 0});
    dump_in_frame_ = true;
  }
  bw::hosts::HostFrame frame{};
  frame.elapsed = elapsed;
  frame.first = frames_ == 0U;
  frame.delta_seconds = frame.first ? 0.0F : static_cast<float>(elapsed - drawn_elapsed_);
  // The chain reads the index modulo 8 (tone_mapping.hpp, contract 4), which
  // the narrowing keeps.
  frame.frame_index = static_cast<std::uint32_t>(frames_);
  frame.display = viewport_->display_report();
  if (!scene_->declare(*device_, graph_, target, size, frame)) {
    return false;
  }
  const graph::CompileResult compiled =
      graph_.compile(*policy_, device_->capabilities(), compiled_);
  if (!compiled.ok()) {
    std::cerr << "game_host: the frame graph did not compile: "
              << graph::graph_error_name(compiled.error) << " at pass '" << compiled.pass
              << "' resource '" << compiled.resource << "'\n";
    return false;
  }
  const graph::ExecuteFailure executed = graph::execute(compiled_, graph_, *device_, *list_);
  if (!executed.ok()) {
    std::cerr << "game_host: the frame graph's execute failed: error "
              << static_cast<unsigned>(executed.error) << " resource error "
              << static_cast<unsigned>(executed.resource_error) << ": " << executed.detail << '\n';
    return false;
  }
  // A pass that recorded less than it declared ends the run here: the list
  // is never executed, so the frame is neither presented, counted nor
  // dumped.
  if (!scene_->frame_recorded()) {
    dump_in_frame_ = false;
    return false;
  }
  return true;
}

// The path is opened and read to its end: a path that does not open, or a
// read that fails (a directory, an I/O error), is the named read failure
// with the path, never a parse of cut text.
bool Host::load_script() {
  const auto read_failure = [this](std::string_view why) {
    std::cerr << "game_host: reading the script " << options_.script.string() << " failed: " << why
              << '\n';
    return false;
  };
  // The file is closed on every path, a thrown one included.
  struct Close {
    void operator()(std::FILE* file) const noexcept { static_cast<void>(std::fclose(file)); }
  };
  errno = 0;
  const std::unique_ptr<std::FILE, Close> file(std::fopen(options_.script.c_str(), "rb"));
  if (file == nullptr) {
    return read_failure(errno != 0 ? std::strerror(errno) : "it does not open");
  }
  std::string text;
  std::array<char, 4096> chunk{};
  // A short read is the end of the file or an error, told apart below. A
  // file that does not fit in memory is the read failure, not an
  // exception out of the host.
  try {
    for (;;) {
      const std::size_t got = std::fread(chunk.data(), 1, chunk.size(), file.get());
      text.append(chunk.data(), got);
      if (got < chunk.size() || std::feof(file.get()) != 0 || std::ferror(file.get()) != 0) {
        break;
      }
    }
  } catch (const std::bad_alloc&) {
    return read_failure("the file does not fit in memory");
  } catch (const std::length_error&) {
    return read_failure("the file does not fit in memory");
  }
  if (std::ferror(file.get()) != 0) {
    const int read_error = errno;
    return read_failure(read_error != 0 ? std::strerror(read_error) : "the read failed");
  }
  // The script's words view `text`, which the parse's error line reads
  // before `text` goes; the commands own their names.
  platform::InputScript parsed = platform::parse_script(text);
  if (!parsed.ok()) {
    std::cerr << "game_host: script " << options_.script.string() << " line " << parsed.line
              << ": error " << static_cast<unsigned>(parsed.error) << " at '" << parsed.word
              << "'\n";
    return false;
  }
  parsed.word = {};
  script_ = std::move(parsed);
  std::cout << "scripted_device script=" << options_.script.string() << '\n';
  return true;
}

bool Host::set_up_world() {
  world_ = bw::world::World::create(bw::world::WorldDescription{});
  // Her object stands her place's height above the origin, so her feet
  // are on it (alicia/view.hpp, contract 1).
  bw::world::Transform place{};
  place.location = bw::math::Vector3f{0.0F, bw::game::alicia::kBodyBelowPlace, 0.0F};
  const bw::world::ObjectResult object = world_->create_object(place);
  if (!object.ok()) {
    return fail("the world's object creation", object.error, "");
  }
  object_ = object.id;
  input_ = bw::input::PlayerInput::create(bw::input::PlayerInputDescription{});
  const bw::game::GameInput loaded = bw::game::load_game_input(*input_);
  if (!loaded.ok()) {
    return fail("loading the game's input", loaded.failure.error, loaded.failure.name);
  }
  evaluator_ = bw::camera::Evaluator::create(bw::camera::EvaluatorDescription{});
  controller_ = std::make_unique<bw::characters::PlayerController>(*world_, *input_, keys_);
  const bw::world::TickFunctionResult registered = world_->register_tick_function(
      "player controller", bw::world::Stage::BeforeSimulation, *controller_);
  if (!registered.ok()) {
    return fail("registering the player's controller", registered.error, "");
  }
  controller_->possess(object_);
  const bw::game::alicia::ViewSetupFailure view = view_.setup(*input_, *controller_, *evaluator_);
  if (view.error == bw::game::alicia::ViewSetupError::RigsFailed) {
    return fail("loading Alicia's rigs", view.camera.error, view.camera.name);
  }
  if (view.error == bw::game::alicia::ViewSetupError::BindFailed) {
    return fail("binding Alicia's view action", view.input, "");
  }
  const bw::world::WorldError camera = world_->set_camera(*evaluator_, object_);
  if (camera != bw::world::WorldError::None) {
    return fail("setting the world's camera", camera, "");
  }
  logged_values_.assign(input_->action_count(), bw::input::ActionValue{});
  if (options_.script.empty()) {
    pad_ = bw::platform::macos::create_pad();
  }
  return true;
}

// Capture is asked for each loop while the window is key and the cursor is
// free: the window releases it itself when it stops being key, and a
// refusal (the window not key after all) is asked again on the next loop.
void Host::read_devices() {
  platform::InputSample sample{};
  while (window_->poll_input(sample)) {
    keys_.accumulate(sample);
  }
  if (focused_ && !window_->cursor_captured()) {
    const bool held = window_->set_cursor_captured(true);
    if (!held && !capture_refused_) {
      std::cout << "capture refused\n";
    } else if (held && capture_refused_) {
      std::cout << "capture held\n";
    }
    capture_refused_ = !held;
  }
}

// Polled here, once per tick, and not with the window's samples on every
// loop: key state sums an axis's samples, and a loop that draws a frame
// again without a tick would hand the stick's position twice.
void Host::read_pad() {
  std::array<platform::InputSample, platform::kPadElementCount> samples{};
  const std::size_t count = pad_->poll(samples);
  for (std::size_t index = 0; index < count; ++index) {
    keys_.accumulate(samples[index]);
  }
  const bool connected = pad_->connected();
  if (connected != pad_connected_) {
    pad_connected_ = connected;
    std::cout << "pad connected=" << (connected ? 1 : 0) << '\n';
  }
}

bool Host::tick_world() {
  if (!options_.script.empty()) {
    for (const platform::ScriptCommand& command : platform::commands_at(script_, tick_)) {
      if (command.kind == platform::ScriptCommandKind::Set) {
        keys_.accumulate(command.sample);
      }
    }
  } else {
    read_pad();
  }
  keys_.begin_frame();
  const double now = time_.seconds();
  const double step =
      bw::engine::make_step(bw::engine::StepSettings{options_.fixed_step, bw::hosts::kLongestStep},
                            previous_seconds_, now);
  previous_seconds_ = now;
  const bw::world::TickReport report = world_->tick(step);
  if (report.camera_error != bw::camera::CameraError::None) {
    std::cerr << "game_host: the camera's evaluation failed at tick " << tick_ << ": "
              << camera_error_name(report.camera_error) << '\n';
    return false;
  }
  for (const bw::world::PrerequisiteCycle& cycle : report.cycles) {
    std::cout << "prerequisite_cycle function=" << cycle.function
              << " prerequisite=" << cycle.prerequisite << '\n';
  }
  if (options_.action_log) {
    log_actions();
  }
  const bw::world::Publication published = world_->publication();
  if (!published.camera_valid) {
    std::cerr << "game_host: the world published no valid camera at tick " << tick_ << '\n';
    return false;
  }
  asset_scene_->view = view_of(published.camera);
  for (const bw::world::PublishedObject& object : published.objects) {
    if (object.id == object_) {
      // The mesh is drawn her place's height below her object.
      asset_scene_->object_to_world =
          bw::math::Matrix4f::translation(
              bw::math::Vector3f{0.0F, -bw::game::alicia::kBodyBelowPlace, 0.0F}) *
          bw::math::Matrix4f::rotation_translation(object.transform.rotation,
                                                   object.transform.location);
    }
  }
  asset_scene_->camera_cut = published.camera_cut;
  asset_scene_->world_seconds = world_->clocks().world;
  asset_scene_->step_seconds = static_cast<float>(step);
  dump_due_ = false;
  if (!options_.script.empty() && !options_.dump.empty()) {
    for (const platform::ScriptCommand& command : platform::commands_at(script_, tick_)) {
      dump_due_ = dump_due_ || command.kind == platform::ScriptCommandKind::Mark;
    }
  }
  return true;
}

void Host::log_actions() {
  for (std::size_t index = 0; index < input_->action_count(); ++index) {
    const bw::input::ActionState& state = input_->state(input_->action_at(index));
    const bw::input::ActionValue before = logged_values_[index];
    logged_values_[index] = state.value;
    if (state.events == 0U) {
      continue;
    }
    const bool fired_alone =
        state.events == static_cast<std::uint8_t>(bw::input::ActionEvent::Fired);
    if (fired_alone && state.value == before) {
      continue;
    }
    std::cout << "action tick=" << tick_ << " name=" << input_->action_name(state.action)
              << " events=" << events_text(state) << " value=" << shortest_decimal(state.value.x)
              << ',' << shortest_decimal(state.value.y) << ',' << shortest_decimal(state.value.z)
              << '\n';
  }
}

bool Host::finish_tick() {
  if (!options_.script.empty()) {
    const bw::world::Publication published = world_->publication();
    const bw::camera::Pose& pose = published.camera;
    for (const platform::ScriptCommand& command : platform::commands_at(script_, tick_)) {
      if (command.kind != platform::ScriptCommandKind::Mark) {
        continue;
      }
      std::filesystem::path dump;
      if (!options_.dump.empty()) {
        dump = options_.dump / (command.name + ".png");
        if (!write_dump_file(dump)) {
          return false;
        }
      }
      const std::array numbers{static_cast<float>(world_->clocks().world),
                               pose.location.x,
                               pose.location.y,
                               pose.location.z,
                               pose.yaw_degrees,
                               pose.pitch_degrees,
                               bw::camera::vertical_fov_degrees(pose)};
      std::array<std::string, numbers.size()> text{};
      for (std::size_t index = 0; index < numbers.size(); ++index) {
        text[index] = shortest_decimal(numbers[index]);
      }
      std::ostringstream line;
      line << "mark=" << command.name << " tick=" << tick_ << " time=" << text[0]
           << " pose=" << text[1] << ',' << text[2] << ',' << text[3] << " yaw=" << text[4]
           << " pitch=" << text[5] << " fov=" << text[6]
           << " cut=" << (published.camera_cut ? 1 : 0) << " rigs=";
      bool first = true;
      for (const bw::camera::StackEntry& entry : evaluator_->stack()) {
        line << (first ? "" : ",") << evaluator_->rig_name(entry.rig) << ':'
             << shortest_decimal(entry.weight);
        first = false;
      }
      line << " actions=";
      first = true;
      for (std::size_t index = 0; index < input_->action_count(); ++index) {
        const bw::input::ActionState& state = input_->state(input_->action_at(index));
        if (state.has(bw::input::ActionEvent::Fired)) {
          line << (first ? "" : ",") << input_->action_name(state.action);
          first = false;
        }
      }
      if (!dump.empty()) {
        line << " dump=" << dump.string();
      }
      // Flushed so the line survives a process ended by a signal.
      std::cout << line.str() << '\n' << std::flush;
    }
  }
  world_->clear_camera_cut();
  return true;
}

// The fence has no write pending here: it is new, or the previous dump's
// write was waited on by the lock, or the frame that recorded it was not
// drawn and its replay stopped before the write.
bool Host::prepare_dump(const rhi::TextureDescription& back_buffer) {
  const rhi::StagingLayout layout = rhi::staging_layout(back_buffer, 0);
  if (staging_.get() == nullptr || layout.width != staging_layout_.width ||
      layout.height != staging_layout_.height || layout.row_pitch != staging_layout_.row_pitch ||
      layout.total_bytes != staging_layout_.total_bytes) {
    staging_.reset();
    rhi::StagingBufferResult staging =
        device_->create_staging_buffer(layout.total_bytes, "game_host dump staging");
    if (!staging.ok()) {
      fail("dump staging buffer creation", staging.error(), staging.detail());
      return false;
    }
    staging_ = std::move(staging.value());
    staging_layout_ = layout;
  }
  fence_->reset();
  return true;
}

bool Host::read_dump() {
  const rhi::StagingLockResult locked =
      device_->lock_staging(*staging_, *fence_, rhi::WaitPolicy::Block);
  if (!locked.ok()) {
    fail("locking the dump's staging buffer", locked.error(), locked.detail());
    return false;
  }
  const bw::capture::PngError packed =
      bw::capture::pack_bgra8_rows(locked.value().bytes, staging_layout_.row_pitch,
                                   staging_layout_.width, staging_layout_.height, rgba_);
  const rhi::StagingError unlocked = device_->unlock_staging(*staging_);
  if (packed != bw::capture::PngError::None) {
    fail("packing the dump's rows", packed, "");
    return false;
  }
  if (unlocked != rhi::StagingError::None) {
    fail("unlocking the dump's staging buffer", unlocked, "");
    return false;
  }
  const bw::capture::PngError encoded =
      bw::capture::encode_png_rgba8(staging_layout_.width, staging_layout_.height, rgba_, png_);
  if (encoded != bw::capture::PngError::None) {
    fail("encoding the dump as PNG", encoded, "");
    return false;
  }
  return true;
}

bool Host::write_dump_file(const std::filesystem::path& path) {
  errno = 0;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (file) {
    file.write(reinterpret_cast<const char*>(png_.data()),
               static_cast<std::streamsize>(png_.size()));
    file.close();
  }
  if (!file) {
    const int error = errno;
    std::cerr << "game_host: writing the dump " << path.string()
              << " failed: " << (error != 0 ? std::strerror(error) : "the stream reported an error")
              << '\n';
    return false;
  }
  return true;
}

// The replay stopped at the pass that would have acquired the back buffer,
// and a mismatch holds nothing (viewport.hpp, after_acquire), so the resize
// is accepted in the frame being recorded (validate_resize). The frame is
// not drawn: it gets no end_frame and is not counted, and the next loop
// iteration records it again under the same frame number; what the replay
// ran before the failed pass (any transition of the replaced back buffer)
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
  graph_.reset();
  asset_scene_ = nullptr;
  scene_.reset();
  staging_.reset();
  fence_.reset();
  viewport_.reset();
  device_.reset();
  window_.reset();
}

} // namespace

// An exception (allocation, path handling) is a failure like any other: one
// line on stderr and exit 1. The host's members are declared in teardown
// order, so leaving by an exception destroys the frame graph, the command
// list, the resources, the viewport, the device (which waits for the GPU)
// and the window in that order.
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
