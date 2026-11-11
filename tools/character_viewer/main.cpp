// character_viewer: a windowed composition of the runtime that draws a
// converted model through the engine's model scene
// (bw/rendering/model_scene.hpp), the draw game_host makes with --asset,
// under an orbit camera the user moves (bw/rendering/scene/orbit.hpp), and
// writes the frame to a PNG file on request (bw/capture/png.hpp). It
// composes the window (bw/platform/os/macos/window.hpp), the Metal device
// and the viewport on the window's surface (bw/rendering/rhi/device.hpp,
// viewport.hpp), one frame graph (bw/rendering/graph/builder.hpp) with the
// schedule policy of the device's backend, and a frame loop on the main
// thread, as game_host does.
//
// Command line: bw/character_viewer/viewer_options.hpp states the rows and
// parses them; at least one --asset is required. Without --shaders the
// directory is <executable directory>/shaders/metal, the executable's path
// read from the OS (bw/platform/process/executable_directory.hpp).
//
// Startup: the window at --size points, titled "Bullet Witch character
// viewer"; the device, the schedule policy and the viewport; the layers
// over --cache and --overrides opened once with one loader; then one model
// scene per --asset, in argument order, each behind a unique_ptr because
// its record function points at it (model_scene.hpp, contract 3), all
// built before the first frame. The camera is the first scene's sphere
// framed at the viewport's aspect (orbit_from_sphere).
//
// Input (bw/platform/window/window.hpp, Input): the mapping of events and
// held keys to the camera is script.hpp's contract 5. Each mouse button
// keeps its own drag origin, set at its press and moved to the pointer at
// each move while it is held; a move with both held turns only. A right
// drag's world_per_pixel keeps the point under the pointer at the target's
// depth under it. A reset (R, [ or ]) zeroes the pan's offset and ends both
// drags until their button is pressed again. A key is held from its
// pressed event to its released event (a release with no press changes
// nothing), and the window losing focus releases them all, since a key let
// go while the window is not key never reports its release. Each frame,
// after its window and script events are handled and before the scene is
// declared, the held keys act for dt, the seconds since the previous
// frame's same point (0 on the first frame; kScriptFrameSeconds every frame
// with --script, so a scripted run is a pure function of its script): a
// key acts as a drag or scroll in its direction, so D slides the model
// right on screen, S down, = moves in and - moves out. Each frame declares
// the current scene under view_of(camera) with the identity model
// transform.
//
// Dump: with --dump FILE, the first drawn frame and the frame after each P
// are written to FILE; without it, P prints one stdout line saying no file
// was given. The dump frame marks the imported back buffer for readback
// into a staging buffer with a fence, beside the presented mark, so the
// graph records the copy and the fence write after the pass and the exit
// transition to Present after the copy (graph/declaration.hpp, contract 7;
// executor.hpp, contract 1); frames not dumped carry no mark. After that
// frame's end_frame the staging buffer is locked on the fence, its rows (at
// the pitch staging_layout gives, B, G, R, A as the swapchain holds them,
// sRGB) are packed to R, G, B, 255 and encoded as PNG, and the file is
// written; one stdout line
// `dump=<file> size=<w>x<h> yaw=<y> pitch=<p> distance=<d> offset=<x>,<y>,<z>`
// gives the pixel size and the camera the frame was drawn from (after its
// commands and held keys): its angles, its distance and the pan's offset
// from the sphere's centre, each number the shortest decimal of up to nine
// significant digits that rounds to the float (a zero is `0`), the offset's
// three components joined by commas. One line is printed per
// file when --dump and a script's dump fall on the same frame. A frame that
// is not drawn (the size mismatch path below) keeps the request for the next
// frame drawn. The staging buffer is made at the first dump and made again
// when the back buffer's staging layout changes; the fence is made at
// startup and reset before each dump frame.
//
// Script (bw/character_viewer/script.hpp): with --script FILE the file is
// read and parsed before the window is made; a file that cannot be opened
// or read, or a directory, is a named failure, and a parse error is one stderr line
// "character_viewer: script FILE line N: <error name> at '<word>'" and exit
// 2, with no usage line. Then each frame, after the window's events are
// handled, the next command is applied once: an Event command goes through
// the same handler as the window's events (an Escape ends the run as a
// window Escape does); a Dump command asks for that frame to be dumped to
// its file, beside and independent of --dump's request. A command is
// consumed when its frame is drawn; a frame that is not drawn keeps it,
// already applied, for the next frame drawn. After the last command's frame
// is drawn (and its dump written) the run ends as a close does. A script
// with no commands ends the run after the first frame drawn.
//
// Exit codes, failure lines and window events as game_host's: 0 when the
// window asked to close, Escape was pressed, the script ended or the
// seconds limit passed,
// with one line on stdout, `frames=N elapsed=S waited=W`; 1 on a named
// failure, one line on stderr; 2 on a usage error, with a line naming the
// error and the argument, then the usage line, on stderr. Every failure line
// starts "character_viewer: ". Named failures include, beyond game_host's,
// a dump whose staging lock fails, whose rows or PNG cannot be made, or
// whose file cannot be opened or written (with the OS's error text). No
// failure falls back: every failure ends the run.
//
// One thread, the process main thread, owns the window, the viewport and
// the submission (window.hpp, Threads; device.hpp, Viewports).
#include "bw/capture/png.hpp"
#include "bw/character_viewer/script.hpp"
#include "bw/character_viewer/viewer_options.hpp"
#include "bw/content/asset_id.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/hosts/game_host_options.hpp"
#include "bw/hosts/option_table.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/process/executable_directory.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/graph/builder.hpp"
#include "bw/rendering/graph/compiled_frame.hpp"
#include "bw/rendering/graph/declaration.hpp"
#include "bw/rendering/graph/executor.hpp"
#include "bw/rendering/graph/schedule_policy.hpp"
#include "bw/rendering/model_scene.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/synchronization.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/scene/orbit.hpp"
#include "bw/rendering/scene/view.hpp"

#include <array>
#include <bitset>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

namespace graph = bw::graph;
namespace platform = bw::platform;
namespace rhi = bw::rhi;
namespace rendering = bw::rendering;
namespace scene = bw::scene;

using bw::character_viewer::ScriptCommand;
using bw::character_viewer::ScriptCommandKind;
using bw::character_viewer::ViewerOptions;

// One line on stderr: what failed, the enumerator's number, the detail.
template <class Code> int fail(std::string_view what, Code code, std::string_view detail) {
  std::cerr << "character_viewer: " << what << " failed: error " << static_cast<unsigned>(code);
  if (!detail.empty()) {
    std::cerr << ": " << detail;
  }
  std::cerr << '\n';
  return 1;
}

// The shortest fixed-notation decimal that reads back as `value` (at most
// nine significant digits for a float), with a negative zero written `0`;
// to_chars is locale-independent. Any float fits the buffer in fixed form
// (39 integer digits at the largest, 45 decimal places at the smallest), so
// the empty result is a failure the caller names, not a path the run takes.
std::optional<std::string> shortest_decimal(float value) {
  std::array<char, 64> buffer{};
  const float written = value == 0.0F ? 0.0F : value;
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), written,
                                          std::chars_format::fixed);
  if (error != std::errc{}) {
    return std::nullopt;
  }
  return std::string(buffer.data(), end);
}

const char* mode_name(platform::WindowMode mode) {
  return mode == platform::WindowMode::BorderlessFullscreen ? "borderless_fullscreen" : "windowed";
}

float aspect_of(rhi::Extent2 size) {
  return static_cast<float>(size.width) / static_cast<float>(size.height);
}

// The layers over --cache and --overrides, with no published cache; prints
// the failure and, for every failure of the converted layer, the converter
// command that makes one, as game_host does.
std::optional<bw::content::LayerDirectories> open_layers(const ViewerOptions& options) {
  std::optional<std::filesystem::path> overrides;
  if (!options.overrides.empty()) {
    overrides = options.overrides;
  }
  const bw::content::OpenLayersResult opened =
      bw::content::open_layers(overrides, std::nullopt, options.cache);
  if (opened.ok()) {
    return opened.layers;
  }
  std::cerr << "character_viewer: opening the content layers failed: error "
            << static_cast<unsigned>(opened.error) << " "
            << bw::content::layer_error_name(opened.error) << ": " << opened.detail << '\n';
  if (opened.error == bw::content::LayerError::ConvertedNotADirectory ||
      opened.error == bw::content::LayerError::ConvertedRecordMissing ||
      opened.error == bw::content::LayerError::ConvertedRecordUnreadable ||
      opened.error == bw::content::LayerError::ConvertedRecordInvalid ||
      opened.error == bw::content::LayerError::ConvertedRecordVersionMismatch) {
    std::cerr << "character_viewer: " << options.cache.string()
              << " is not a converted-originals cache this character_viewer can read; make it "
                 "with: content_converter convert --install <install directory> --out "
              << options.cache.string() << '\n';
  }
  return std::nullopt;
}

class Host {
public:
  // `script` is --script's commands in file order; empty without it.
  Host(ViewerOptions options, std::vector<ScriptCommand> script)
      : options_(std::move(options)), script_(std::move(script)) {}

  // Creates everything and runs the frame loop; the exit code.
  int run();

  // Waits for the GPU, then destroys in order: command list, the frame
  // graph's frame, the scenes, the staging buffer and fence, viewport,
  // device, window.
  void shut_down();

private:
  // What one frame did; as game_host's.
  enum class FrameOutcome : std::uint8_t { Drawn, Resized, Failed };
  // What one event asks of the loop.
  enum class EventOutcome : std::uint8_t { Continue, Close, Failed };

  // The layers, the loader and one scene per --asset; false after a
  // failure line.
  bool build_scenes();
  // The camera at rest on the current scene's sphere at the viewport's
  // aspect; false after a failure line.
  bool reset_camera();
  EventOutcome handle(const platform::WindowEvent& event);
  EventOutcome handle_key(platform::Key key);
  // The held keys' turn, pan and dolly for the time since the previous call.
  void apply_held_keys();
  [[nodiscard]] bool held(platform::Key key) const;
  int run_frames();
  // The next script command, applied once (script.hpp, contract 3).
  EventOutcome apply_script_command();
  FrameOutcome draw_frame();
  bool record_frame(rhi::Texture& back_buffer, rhi::Extent2 size);
  // The staging buffer for the back buffer's layout, made when absent or of
  // another layout, and the fence reset; false after a failure line.
  bool prepare_dump(const rhi::TextureDescription& back_buffer);
  // The dump frame's bytes to each requested file; false after a failure
  // line.
  bool write_dump();
  // The PNG to `file` and its stdout line; false after a failure line.
  bool write_dump_file(const std::filesystem::path& file);
  FrameOutcome resize_after_mismatch();

  ViewerOptions options_;
  // --script's commands, the next one's index, and whether it has been
  // applied to a frame not yet drawn.
  std::vector<ScriptCommand> script_{};
  std::size_t next_command_{};
  bool command_applied_{};
  bw::platform::macos::PlatformTime time_{};
  std::unique_ptr<platform::Window> window_{};
  std::unique_ptr<rhi::Device> device_{};
  rhi::Ref<rhi::Viewport> viewport_{};
  // The dump's fence (made at startup) and staging buffer (made at the
  // first dump), and the layout the staging buffer was made for.
  rhi::Ref<rhi::GpuFence> fence_{};
  rhi::Ref<rhi::StagingBuffer> staging_{};
  rhi::StagingLayout staging_layout_{};
  // One scene per --asset, in argument order.
  std::vector<std::unique_ptr<rendering::ModelScene>> scenes_{};
  std::unique_ptr<rhi::CommandList> list_{};
  const graph::SchedulePolicy* policy_{};
  graph::FrameGraph graph_{};
  graph::CompiledFrame compiled_{};
  // The scene declared each frame and the camera it is seen through.
  std::size_t current_{};
  scene::OrbitCamera camera_{};
  // The left drag's and the right drag's origins: the pointer's last
  // position in each.
  platform::PixelPoint last_{};
  platform::PixelPoint pan_last_{};
  // The keys pressed and not yet released, by Key's value, and the clock at
  // the previous frame's held-key step (none before the first; not read
  // with --script).
  std::bitset<16> held_{};
  std::optional<double> held_time_{};
  // A dump to --dump's file, and one to a script's file (empty when none),
  // asked for and not yet written; the frame being drawn carries the
  // readback mark.
  std::filesystem::path script_dump_{};
  bool dump_requested_{};
  bool dump_in_frame_{};
  // A left drag (turn) and a right drag (pan) in progress.
  bool dragging_{};
  bool panning_{};
  // The packed rows and the file, kept across dumps.
  std::vector<std::byte> rgba_{};
  std::vector<std::byte> png_{};
  std::uint64_t frames_{};
  std::uint64_t waited_frames_{};
  bool resized_after_mismatch_{};
};

int Host::run() {
  platform::WindowDescription window_description{};
  window_description.title = "Bullet Witch character viewer";
  window_description.width = options_.width;
  window_description.height = options_.height;
  window_description.mode = options_.fullscreen ? platform::WindowMode::BorderlessFullscreen
                                                : platform::WindowMode::Windowed;
  platform::WindowError window_error = platform::WindowError::None;
  window_ = bw::platform::macos::create_window(window_description, window_error);
  if (window_ == nullptr) {
    return fail("window creation", window_error, "");
  }

  rhi::DeviceResult device = rhi::Device::create(rhi::DeviceDescription{});
  if (!device.ok()) {
    return fail("device creation", device.error(), device.detail());
  }
  device_ = std::move(device.value());
  policy_ = graph::schedule_policy_for(device_->capabilities().backend);
  if (policy_ == nullptr) {
    std::cerr << "character_viewer: the render graph has no schedule policy for the device's "
                 "backend "
              << static_cast<unsigned>(device_->capabilities().backend) << '\n';
    return 1;
  }

  const platform::PixelSize pixels = window_->pixel_size();
  rhi::ViewportDescription viewport_description{};
  viewport_description.window = window_->native_handle();
  viewport_description.pixel_size = rhi::Extent2{pixels.width, pixels.height};
  viewport_description.format = rhi::Format::B8G8R8A8UnormSrgb;
  viewport_description.vsync = true;
  viewport_description.debug_name = "character_viewer viewport";
  rhi::ViewportResult viewport = device_->create_viewport(viewport_description);
  if (!viewport.ok()) {
    return fail("viewport creation", viewport.error(), viewport.detail());
  }
  viewport_ = std::move(viewport.value());

  rhi::GpuFenceResult fence = device_->create_fence("character_viewer dump fence");
  if (!fence.ok()) {
    return fail("dump fence creation", fence.error(), fence.detail());
  }
  fence_ = std::move(fence.value());

  if (!build_scenes() || !reset_camera()) {
    return 1;
  }
  dump_requested_ = !options_.dump.empty();
  list_ = std::make_unique<rhi::CommandList>();
  return run_frames();
}

// Each scene is held before it is built, so a scene whose build failed is
// destroyed by shut_down in the same place as a built one.
bool Host::build_scenes() {
  const std::optional<bw::content::LayerDirectories> layers = open_layers(options_);
  if (!layers.has_value()) {
    return false;
  }
  const bw::content::Loader loader(*layers);
  scenes_.reserve(options_.assets.size());
  for (const std::string& asset : options_.assets) {
    const bw::content::AssetIdResult id = bw::content::make_asset_id(asset);
    if (!id.ok()) {
      std::cerr << "character_viewer: the asset id " << asset << " does not validate: error "
                << static_cast<unsigned>(id.error) << '\n';
      return false;
    }
    rendering::ModelScene& built = *scenes_.emplace_back(std::make_unique<rendering::ModelScene>());
    const rendering::ModelSceneFailure failure = rendering::build_model_scene(
        *device_, loader, id.id, viewport_->format(), options_.shaders, built);
    if (!failure.ok()) {
      std::cerr << "character_viewer: " << rendering::model_scene_failure_text(failure, asset)
                << '\n';
      return false;
    }
  }
  return true;
}

// The build framed every sphere at the aspect that gives the nearest eye,
// so the empty camera is not reached; it is still a named failure.
bool Host::reset_camera() {
  const bw::geometry::MeshResources& mesh = scenes_[current_]->model.mesh;
  const bw::math::Vector3f centre{mesh.center[0], mesh.center[1], mesh.center[2]};
  const std::optional<scene::OrbitCamera> camera =
      scene::orbit_from_sphere(centre, mesh.radius, aspect_of(viewport_->size()));
  if (!camera.has_value()) {
    std::cerr << "character_viewer: the mesh " << options_.assets[current_]
              << " has the bounding sphere centre (" << mesh.center[0] << ", " << mesh.center[1]
              << ", " << mesh.center[2] << ") radius " << mesh.radius << " and cannot be framed\n";
    return false;
  }
  camera_ = *camera;
  dragging_ = false;
  panning_ = false;
  return true;
}

Host::EventOutcome Host::handle(const platform::WindowEvent& event) {
  const std::uint8_t left = platform::mouse_button_bit(platform::MouseButton::Left);
  const std::uint8_t right = platform::mouse_button_bit(platform::MouseButton::Right);
  switch (event.kind) {
  // A resize keeps the distance the user chose; R refits it to the new
  // aspect.
  case platform::WindowEventKind::Resized: {
    const rhi::Status<rhi::ViewportError> resized =
        device_->resize_viewport(*viewport_, rhi::Extent2{event.size.width, event.size.height});
    if (!resized.ok()) {
      fail("viewport resize", resized.error(), resized.detail());
      return EventOutcome::Failed;
    }
    return EventOutcome::Continue;
  }
  case platform::WindowEventKind::CloseRequested:
    return EventOutcome::Close;
  case platform::WindowEventKind::ModeChanged:
    std::cout << "mode_changed mode=" << mode_name(event.mode) << '\n';
    return EventOutcome::Continue;
  case platform::WindowEventKind::ModeRefused:
    // The one mode request this viewer makes is --fullscreen's, at window
    // creation; its refusal ends the run rather than running windowed.
    if (options_.fullscreen) {
      std::cerr << "character_viewer: borderless fullscreen failed: the OS refused the "
                   "transition and the window stays "
                << mode_name(event.mode) << '\n';
      return EventOutcome::Failed;
    }
    std::cout << "mode_refused mode=" << mode_name(event.mode) << '\n';
    return EventOutcome::Continue;
  case platform::WindowEventKind::FocusChanged:
    std::cout << "focus_changed focused=" << (event.focused ? 1 : 0) << '\n';
    if (!event.focused) {
      held_.reset();
    }
    return EventOutcome::Continue;
  case platform::WindowEventKind::MouseButton:
    if (event.button == platform::MouseButton::Left) {
      dragging_ = event.pressed;
      last_ = event.position;
    } else if (event.button == platform::MouseButton::Right) {
      panning_ = event.pressed;
      pan_last_ = event.position;
    }
    return EventOutcome::Continue;
  case platform::WindowEventKind::MouseMoved: {
    const bool turning = dragging_ && (event.buttons & left) != 0U;
    if (turning) {
      camera_ =
          scene::turn(camera_, static_cast<float>(event.position.x - last_.x),
                      static_cast<float>(event.position.y - last_.y), scene::kOrbitRadiansPerPixel);
      last_ = event.position;
    }
    if (panning_ && (event.buttons & right) != 0U) {
      const std::uint32_t height = viewport_->size().height;
      if (!turning && height != 0U) {
        camera_ = scene::pan(camera_, static_cast<float>(event.position.x - pan_last_.x),
                             static_cast<float>(event.position.y - pan_last_.y),
                             scene::world_per_pixel(camera_, static_cast<float>(height)));
      }
      pan_last_ = event.position;
    }
    return EventOutcome::Continue;
  }
  case platform::WindowEventKind::Scrolled:
    camera_ = scene::dolly(camera_, event.lines);
    return EventOutcome::Continue;
  case platform::WindowEventKind::Key:
    held_.set(static_cast<std::size_t>(event.key), event.pressed);
    return event.pressed ? handle_key(event.key) : EventOutcome::Continue;
  }
  return EventOutcome::Continue;
}

Host::EventOutcome Host::handle_key(platform::Key key) {
  switch (key) {
  case platform::Key::Escape:
    return EventOutcome::Close;
  case platform::Key::R:
    return reset_camera() ? EventOutcome::Continue : EventOutcome::Failed;
  case platform::Key::LeftBracket:
    current_ = current_ == 0U ? scenes_.size() - 1U : current_ - 1U;
    return reset_camera() ? EventOutcome::Continue : EventOutcome::Failed;
  case platform::Key::RightBracket:
    current_ = current_ + 1U == scenes_.size() ? 0U : current_ + 1U;
    return reset_camera() ? EventOutcome::Continue : EventOutcome::Failed;
  case platform::Key::P:
    if (options_.dump.empty()) {
      // Flushed so the line survives a process ended by a signal.
      std::cout << "dump skipped: no --dump file given\n" << std::flush;
    } else {
      dump_requested_ = true;
    }
    return EventOutcome::Continue;
  // The held keys act each frame (apply_held_keys), not on the press.
  case platform::Key::Left:
  case platform::Key::Right:
  case platform::Key::Up:
  case platform::Key::Down:
  case platform::Key::W:
  case platform::Key::A:
  case platform::Key::S:
  case platform::Key::D:
  case platform::Key::Equal:
  case platform::Key::Minus:
  case platform::Key::Other:
    return EventOutcome::Continue;
  }
  return EventOutcome::Continue;
}

bool Host::held(platform::Key key) const { return held_.test(static_cast<std::size_t>(key)); }

// A held key's rate times the frame's seconds: the arrows turn, W, A, S and
// D pan, = and - dolly. The rates, the key layout and the signs are
// explained beside kOrbitKeyPixelsPerSecond in orbit.hpp.
void Host::apply_held_keys() {
  double dt = bw::character_viewer::kScriptFrameSeconds;
  if (options_.script.empty()) {
    const double now = time_.seconds();
    dt = held_time_.has_value() ? now - *held_time_ : 0.0;
    held_time_ = now;
  }
  const auto axis = [this](platform::Key positive, platform::Key negative) {
    return static_cast<double>(static_cast<int>(held(positive)) - static_cast<int>(held(negative)));
  };
  const auto dx = static_cast<float>(axis(platform::Key::Right, platform::Key::Left) *
                                     scene::kOrbitKeyPixelsPerSecond * dt);
  const auto dy = static_cast<float>(axis(platform::Key::Down, platform::Key::Up) *
                                     scene::kOrbitKeyPixelsPerSecond * dt);
  if (dx != 0.0F || dy != 0.0F) {
    camera_ = scene::turn(camera_, dx, dy, scene::kOrbitRadiansPerPixel);
  }
  const auto pan_dx = static_cast<float>(axis(platform::Key::D, platform::Key::A) *
                                         scene::kOrbitKeyPixelsPerSecond * dt);
  const auto pan_dy = static_cast<float>(axis(platform::Key::S, platform::Key::W) *
                                         scene::kOrbitKeyPixelsPerSecond * dt);
  const std::uint32_t height = viewport_->size().height;
  if ((pan_dx != 0.0F || pan_dy != 0.0F) && height != 0U) {
    camera_ = scene::pan(camera_, pan_dx, pan_dy,
                         scene::world_per_pixel(camera_, static_cast<float>(height)));
  }
  const auto lines = static_cast<float>(axis(platform::Key::Minus, platform::Key::Equal) *
                                        scene::kOrbitKeyLinesPerSecond * dt);
  if (lines != 0.0F) {
    camera_ = scene::dolly(camera_, lines);
  }
}

int Host::run_frames() {
  const double start = time_.seconds();
  double elapsed = 0.0;
  for (;;) {
    window_->pump();
    bool close = false;
    platform::WindowEvent event{};
    while (window_->poll_event(event)) {
      const EventOutcome outcome = handle(event);
      if (outcome == EventOutcome::Failed) {
        return 1;
      }
      close = close || outcome == EventOutcome::Close;
    }
    if (close) {
      break;
    }
    const EventOutcome command = apply_script_command();
    if (command == EventOutcome::Failed) {
      return 1;
    }
    if (command == EventOutcome::Close) {
      break;
    }
    apply_held_keys();
    const FrameOutcome frame = draw_frame();
    if (frame == FrameOutcome::Failed) {
      return 1;
    }
    if (frame == FrameOutcome::Drawn && !options_.script.empty()) {
      if (command_applied_) {
        command_applied_ = false;
        ++next_command_;
      }
      if (next_command_ == script_.size()) {
        break;
      }
    }
    elapsed = time_.seconds() - start;
    if (bw::hosts::seconds_limit_reached(options_.seconds, elapsed)) {
      break;
    }
  }
  elapsed = time_.seconds() - start;
  std::cout << "frames=" << frames_ << " elapsed=" << elapsed << " waited=" << waited_frames_
            << '\n';
  return 0;
}

Host::EventOutcome Host::apply_script_command() {
  if (command_applied_ || next_command_ == script_.size()) {
    return EventOutcome::Continue;
  }
  command_applied_ = true;
  const ScriptCommand& command = script_[next_command_];
  if (command.kind == ScriptCommandKind::Dump) {
    script_dump_ = command.file;
    return EventOutcome::Continue;
  }
  return handle(command.event);
}

Host::FrameOutcome Host::draw_frame() {
  rhi::Texture& back_buffer = viewport_->back_buffer();
  const rhi::Extent2 size = viewport_->size();

  rhi::CommandList& list = *list_;
  list.reset();
  if (!record_frame(back_buffer, size)) {
    return FrameOutcome::Failed;
  }

  const rhi::Status<rhi::ExecuteError> executed = device_->execute(list);
  if (!executed.ok()) {
    if (executed.error() == rhi::ExecuteError::BackBufferSizeMismatch) {
      // The replay stopped before the pass, so the readback was not
      // recorded; the request stays for the next frame drawn.
      dump_in_frame_ = false;
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
  if (ended.gpu_fault) {
    std::cerr << "character_viewer: the GPU reported a fault in work completed by frame "
              << ended.completed << '\n';
    return FrameOutcome::Failed;
  }
  if (dump_in_frame_) {
    dump_in_frame_ = false;
    if (!write_dump()) {
      return FrameOutcome::Failed;
    }
  }
  return FrameOutcome::Drawn;
}

// game_host's frame with the orbit camera's view and, on a dump frame, the
// readback mark on the back buffer beside the presented mark.
bool Host::record_frame(rhi::Texture& back_buffer, rhi::Extent2 size) {
  graph_.reset();
  const graph::TextureHandle target = graph_.import_texture(
      graph::ImportedTexture{&back_buffer, rhi::Access::Present, rhi::Access::Present});
  graph_.mark_presented(target);
  if (dump_requested_ || !script_dump_.empty()) {
    if (!prepare_dump(back_buffer.description())) {
      return false;
    }
    graph_.mark_readback(target, graph::ReadBack{staging_.get(), fence_.get(), 0});
    dump_in_frame_ = true;
  }
  const bw::math::Matrix4f identity =
      bw::math::Matrix4f::translation(bw::math::Vector3f{0.0F, 0.0F, 0.0F});
  const rendering::ModelSceneFailure declared = rendering::declare_model_scene(
      *scenes_[current_], *device_, graph_, target, size, scene::view_of(camera_), identity);
  if (!declared.ok()) {
    std::cerr << "character_viewer: "
              << rendering::model_scene_failure_text(declared, options_.assets[current_]) << '\n';
    return false;
  }
  const graph::CompileResult compiled =
      graph_.compile(*policy_, device_->capabilities(), compiled_);
  if (!compiled.ok()) {
    std::cerr << "character_viewer: the frame graph did not compile: "
              << graph::graph_error_name(compiled.error) << " at pass '" << compiled.pass
              << "' resource '" << compiled.resource << "'\n";
    return false;
  }
  const graph::ExecuteFailure executed = graph::execute(compiled_, graph_, *device_, *list_);
  if (!executed.ok()) {
    std::cerr << "character_viewer: the frame graph's execute failed: error "
              << static_cast<unsigned>(executed.error) << " resource error "
              << static_cast<unsigned>(executed.resource_error) << ": " << executed.detail << '\n';
    return false;
  }
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
        device_->create_staging_buffer(layout.total_bytes, "character_viewer dump staging");
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

bool Host::write_dump() {
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
  if (dump_requested_) {
    if (!write_dump_file(options_.dump)) {
      return false;
    }
    dump_requested_ = false;
  }
  if (!script_dump_.empty()) {
    if (!write_dump_file(script_dump_)) {
      return false;
    }
    script_dump_.clear();
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
    std::cerr << "character_viewer: writing the dump " << path.string()
              << " failed: " << (error != 0 ? std::strerror(error) : "the stream reported an error")
              << '\n';
    return false;
  }
  const std::array<float, 6> numbers{camera_.yaw,      camera_.pitch,    camera_.distance,
                                     camera_.offset.x, camera_.offset.y, camera_.offset.z};
  std::array<std::string, 6> text{};
  for (std::size_t i = 0; i < numbers.size(); ++i) {
    const std::optional<std::string> written = shortest_decimal(numbers[i]);
    if (!written.has_value()) {
      std::cerr << "character_viewer: writing the dump line for " << path.string()
                << " failed: a camera float did not fit its text buffer\n";
      return false;
    }
    text[i] = *written;
  }
  // Flushed so the line survives a process ended by a signal.
  std::cout << "dump=" << path.string() << " size=" << staging_layout_.width << "x"
            << staging_layout_.height << " yaw=" << text[0] << " pitch=" << text[1]
            << " distance=" << text[2] << " offset=" << text[3] << ',' << text[4] << ',' << text[5]
            << '\n'
            << std::flush;
  return true;
}

// As game_host's: the frame is not drawn, the viewport takes the window's
// current pixel size, and a second mismatch before a drawn frame is a
// named failure.
Host::FrameOutcome Host::resize_after_mismatch() {
  if (resized_after_mismatch_) {
    std::cerr << "character_viewer: frame execute failed: the back buffer's size still differs "
                 "from the viewport's after the viewport was resized to the window's pixel "
                 "size\n";
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
  scenes_.clear();
  staging_.reset();
  fence_.reset();
  viewport_.reset();
  device_.reset();
  window_.reset();
}

// The usage error line, then the usage line; exit 2. The argument and the
// reason in the order the line prints them.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
int usage_error(bw::hosts::OptionError error, std::string_view argument, std::string_view reason) {
  std::cerr << "character_viewer: usage error " << static_cast<unsigned>(error) << ": " << argument;
  if (!reason.empty()) {
    std::cerr << " " << reason;
  }
  std::cerr << '\n' << bw::character_viewer::viewer_usage() << '\n';
  return 2;
}

const char* script_error_name(bw::character_viewer::ScriptError error) {
  using bw::character_viewer::ScriptError;
  switch (error) {
  case ScriptError::None:
    return "None";
  case ScriptError::UnknownCommand:
    return "UnknownCommand";
  case ScriptError::ArgumentCount:
    return "ArgumentCount";
  case ScriptError::InvalidNumber:
    return "InvalidNumber";
  case ScriptError::UnknownKey:
    return "UnknownKey";
  case ScriptError::UnknownButton:
    return "UnknownButton";
  case ScriptError::EmptyDumpFile:
    return "EmptyDumpFile";
  }
  return "unknown";
}

// --script's commands into `commands`; 0, or the exit code after the
// failure line: 1 when the file cannot be opened or read, 2 when it does
// not parse.
int read_script(const std::filesystem::path& path, std::vector<ScriptCommand>& commands) {
  const auto read_failed = [&path](std::string_view reason) {
    std::cerr << "character_viewer: reading the script " << path.string() << " failed: " << reason
              << '\n';
    return 1;
  };
  const auto os_text = [](int error) {
    return error != 0 ? std::string(std::strerror(error))
                      : std::string("the stream reported an error");
  };
  // A directory opens as a stream that reads as empty, so it is refused
  // here with the OS's text for the condition.
  std::error_code kind;
  if (std::filesystem::is_directory(path, kind)) {
    return read_failed(std::strerror(EISDIR));
  }
  // The whole file in one read of its size, so a short or failed read is a
  // failure rather than a truncated script.
  std::error_code sized;
  const std::uintmax_t size = std::filesystem::file_size(path, sized);
  if (sized) {
    return read_failed(sized.message());
  }
  errno = 0;
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return read_failed(os_text(errno));
  }
  std::string text(static_cast<std::size_t>(size), '\0');
  errno = 0;
  file.read(text.data(), static_cast<std::streamsize>(size));
  if (std::cmp_not_equal(file.gcount(), size)) {
    const int error = errno;
    return read_failed("read " + std::to_string(file.gcount()) + " of " + std::to_string(size) +
                       " bytes" + (error != 0 ? ": " + os_text(error) : std::string()));
  }
  bw::character_viewer::ScriptParse parse = bw::character_viewer::parse_script(text);
  if (!parse.ok()) {
    std::cerr << "character_viewer: script " << path.string() << " line " << parse.line << ": "
              << script_error_name(parse.error) << " at '" << parse.word << "'\n";
    return 2;
  }
  commands = std::move(parse.commands);
  return 0;
}

} // namespace

// An exception (allocation, path handling) is a failure like any other: one
// line on stderr and exit 1. The host's members are declared in teardown
// order, so leaving by an exception destroys them in shut_down's order.
int main(int argc, char** argv) {
  try {
    const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
    std::vector<std::string_view> arguments;
    arguments.reserve(raw.size());
    for (std::size_t i = 1; i < raw.size(); ++i) {
      arguments.emplace_back(raw[i]);
    }
    bw::character_viewer::ViewerParse parse = bw::character_viewer::parse_viewer_options(arguments);
    if (parse.error != bw::hosts::OptionError::None) {
      return usage_error(parse.error, parse.argument, parse.reason);
    }
    // viewer_options.hpp: at least one --asset, checked after the parse.
    if (parse.options.assets.empty()) {
      return usage_error(bw::hosts::OptionError::RequiredOptionMissing, "--asset",
                         "is required at least once");
    }
    ViewerOptions options = std::move(parse.options);
    if (options.shaders.empty()) {
      std::filesystem::path directory;
      std::string detail;
      if (!bw::platform::executable_directory(directory, detail)) {
        std::cerr << "character_viewer: resolving the executable's directory for the default "
                     "--shaders failed: "
                  << detail << '\n';
        return 1;
      }
      options.shaders = directory / "shaders" / "metal";
    }
    std::vector<ScriptCommand> script;
    if (!options.script.empty()) {
      const int code = read_script(options.script, script);
      if (code != 0) {
        return code;
      }
    }
    Host host(std::move(options), std::move(script));
    const int code = host.run();
    host.shut_down();
    return code;
  } catch (const std::exception& error) {
    std::cerr << "character_viewer: unexpected exception: " << error.what() << '\n';
    return 1;
  } catch (...) {
    std::cerr << "character_viewer: unexpected exception of unknown type\n";
    return 1;
  }
}
