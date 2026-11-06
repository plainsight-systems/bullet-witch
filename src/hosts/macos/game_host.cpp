// game_host: the first stage of the game executable. It composes: the
// window (bw/platform/os/macos/window.hpp), the Metal device and the
// viewport on the window's surface (bw/rendering/rhi/device.hpp,
// viewport.hpp), and a frame loop on the main thread that runs until the
// window is asked to close or the seconds limit passes. What it draws is
// chosen by its options: without --asset the fixture cube
// (bw/rendering/pulled_cube_fixture.hpp); with --cache and --asset the
// model of that id, loaded by bw/rendering/model_resources.hpp and drawn by
// bw/rendering/textured_model_fixture.hpp. Each frame's pass is begun by
// bw/rendering/target_pass.hpp. The matrices are made by
// bw/math/matrix/matrix4.hpp and bw/rendering/scene/view.hpp; the host
// states the cube's camera, composes each object's turn with its view, and
// writes no matrix element, resource description or pass description.
//
// The model draw: the layers are opened over --cache as the converted
// originals and --overrides when given, with no published cache
// (bw/content/layers.hpp, open_layers). Once the model is built, one stdout
// line `base_color_fallback draws=K of=T first=ID` names the K of the T
// draws that sample the white fallback and the material id of the first of
// them (kNoMaterial for a draw without a material, none when K is 0), so a
// run whose draws all took the fallback reads differently from a textured
// one. The camera frames the mesh's bounding sphere (view.hpp,
// frame_sphere) and the mesh turns about the vertical axis through the
// sphere's center, so the whole model stays in view at every angle; a mesh
// whose bounding sphere cannot be framed is a named failure. The pass has
// a depth target sized to the viewport, recreated when the viewport's size
// changes.
//
// Command line: bw/hosts/game_host_options.hpp states the rows and parses
// them. Without --shaders the directory is
// <executable directory>/shaders/metal, the executable's path read from the
// OS, not from argv[0], and resolved through symbolic links; that
// resolution is the OS-bound part and stays here.
//
// Exit codes: 0 when the window asked to close or the seconds limit passed,
// with one line on stdout, `frames=N elapsed=S waited=W`: the frames drawn,
// the elapsed seconds and how many end_frame calls blocked on the oldest
// frame in flight (with --asset, the base_color_fallback line above
// precedes it); 1 on a named failure, one line on stderr naming what
// failed and, for an error code, the enumerator's number and the detail;
// 2 on a usage error, with a line naming the error and the argument, then
// the usage line, on stderr. Named failures include an executable path the
// OS cannot give or resolve when --shaders is absent, the OS refusing the
// borderless fullscreen --fullscreen asked for, and for --asset: layers
// that do not open (with a second line naming the content_converter
// command for every failure of the converted layer), a model that does not
// load (the id concerned and the failing layer's own error) and a resource
// the RHI refuses. No failure falls back: every failure ends the run.
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
#include "bw/content/asset_id.hpp"
#include "bw/content/layers.hpp"
#include "bw/content/loader.hpp"
#include "bw/hosts/game_host_options.hpp"
#include "bw/hosts/option_table.hpp"
#include "bw/math/matrix/matrix4.hpp"
#include "bw/math/rotation/quaternion.hpp"
#include "bw/math/vector/vector3.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/geometry/mesh_asset.hpp"
#include "bw/rendering/geometry/mesh_resources.hpp"
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/geometry/textured_mesh_fixture.hpp"
#include "bw/rendering/model_resources.hpp"
#include "bw/rendering/pulled_cube_fixture.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/artifact_file.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/target_pass.hpp"
#include "bw/rendering/textured_model_fixture.hpp"

#include <mach-o/dyld.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
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

namespace platform = bw::platform;
namespace rhi = bw::rhi;
namespace geometry = bw::geometry;
namespace shaders = bw::shaders;
namespace content = bw::content;
namespace rendering = bw::rendering;
namespace math = bw::math;

// ---- Executable directory -----------------------------------------------------------

// The directory of this executable, from the path the OS loaded it from,
// resolved through symbolic links; false with `detail` set when the OS
// gives no path or the path does not resolve.
bool executable_directory(std::filesystem::path& directory, std::string& detail) {
  std::uint32_t size = 1024;
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    // The buffer was too small; `size` is now the size the path needs.
    buffer.assign(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
      detail = "the OS gave no executable path";
      return false;
    }
  }
  buffer.resize(buffer.find('\0'));
  std::error_code error;
  const std::filesystem::path real =
      std::filesystem::canonical(std::filesystem::path(buffer), error);
  if (error) {
    detail = buffer + ": " + error.message();
    return false;
  }
  directory = real.parent_path();
  return true;
}

// ---- Failure lines -------------------------------------------------------------------

// One line on stderr: what failed, the enumerator's number, the detail.
template <class Code> int fail(std::string_view what, Code code, std::string_view detail) {
  std::cerr << "game_host: " << what << " failed: error " << static_cast<unsigned>(code);
  if (!detail.empty()) {
    std::cerr << ": " << detail;
  }
  std::cerr << '\n';
  return 1;
}

// ---- Shader artifacts ----------------------------------------------------------------

// Permutation 0 of a registry shader from `directory`
// (bw/rendering/shaders/artifact_file.hpp). Prints the failure line and
// returns false on any failure.
bool load_shader(const std::filesystem::path& directory, std::string_view name,
                 shaders::ArtifactFile& file) {
  const shaders::ArtifactFileFailure failure =
      shaders::read_artifact_file(directory, name, shaders::PermutationId{}, file);
  switch (failure.error) {
  case shaders::ArtifactFileError::None:
    return true;
  case shaders::ArtifactFileError::ShaderUnknown:
    std::cerr << "game_host: the shader registry has no shader named " << name << '\n';
    break;
  case shaders::ArtifactFileError::FileUnreadable:
    std::cerr << "game_host: the shader artifact " << failure.path.string() << " cannot be read\n";
    break;
  case shaders::ArtifactFileError::ArtifactRejected:
    std::cerr << "game_host: loading the shader artifact " << failure.path.string()
              << " failed: error " << static_cast<unsigned>(failure.load.code) << ": block '"
              << failure.block_name << "' expected " << failure.load.expected << " actual "
              << failure.load.actual << " at byte " << failure.load.byte_offset << '\n';
    break;
  }
  return false;
}

// ---- The cameras ---------------------------------------------------------------------

// The cube's camera: 40 degrees top to bottom, +y up.
constexpr float kVerticalFov = 40.0F * 3.14159265358979F / 180.0F;

float aspect_of(rhi::Extent2 size) {
  return static_cast<float>(size.width) / static_cast<float>(size.height);
}

// The angle of a turn at a fixed rate, within one revolution.
float turn_angle(double elapsed_seconds, double radians_per_second) {
  return static_cast<float>(std::fmod(elapsed_seconds * radians_per_second, 6.283185307179586));
}

// The cube turning about a tilted axis, seen by a camera at (3.0, 2.4, 4.2)
// looking at the origin, near 0.1, far 100. The cube's bounding sphere
// stays inside the view at every angle, so nothing clips. Faces toward the
// camera are counter-clockwise on screen, which is what the pipeline's cull
// of clockwise faces keeps.
math::Matrix4f cube_world_to_clip(double elapsed_seconds, rhi::Extent2 size) {
  const math::Quaternion4f turn = math::Quaternion4f::from_axis_angle(
      math::Vector3f{0.3F, 1.0F, 0.2F}.safe_normal(), turn_angle(elapsed_seconds, 0.9));
  bw::scene::PerspectiveView view{};
  view.eye = math::Vector3f{3.0F, 2.4F, 4.2F};
  view.vertical_fov = kVerticalFov;
  view.near_plane = 0.1F;
  view.far_plane = 100.0F;
  return math::Matrix4f::rotation_translation(turn, math::Vector3f::zero) *
         bw::scene::world_to_clip(view, aspect_of(size));
}

// The model turning about the vertical axis through its bounding sphere's
// center, seen through the view that frames that sphere
// (bw/rendering/scene/view.hpp, frame_sphere); empty when the sphere cannot
// be framed.
std::optional<math::Matrix4f> asset_world_to_clip(double elapsed_seconds, rhi::Extent2 size,
                                                  const geometry::MeshResources& mesh) {
  const float aspect = aspect_of(size);
  const math::Vector3f center{mesh.center[0], mesh.center[1], mesh.center[2]};
  const std::optional<bw::scene::PerspectiveView> view =
      bw::scene::frame_sphere(center, mesh.radius, aspect);
  if (!view.has_value()) {
    return std::nullopt;
  }
  const math::Quaternion4f turn = math::Quaternion4f::from_axis_angle(
      math::Vector3f{0.0F, 1.0F, 0.0F}, turn_angle(elapsed_seconds, 0.5));
  return math::Matrix4f::translation(-center) * math::Matrix4f::rotation_translation(turn, center) *
         bw::scene::world_to_clip(*view, aspect);
}

// The failure line of a mesh whose bounding sphere cannot be framed; false.
bool fail_unframeable(std::string_view asset, const geometry::MeshResources& mesh) {
  std::cerr << "game_host: the mesh " << asset << " has the bounding sphere center ("
            << mesh.center[0] << ", " << mesh.center[1] << ", " << mesh.center[2] << ") radius "
            << mesh.radius << " and cannot be framed\n";
  return false;
}

// ---- The scene -----------------------------------------------------------------------

// Everything the cube draw holds: the two artifacts and the cube fixture
// (bw/rendering/pulled_cube_fixture.hpp).
struct Scene {
  shaders::ArtifactFile vertex{};
  shaders::ArtifactFile pixel{};
  rendering::PulledCube cube{};
};

// Loads the artifacts and makes the cube fixture for the viewport's format.
// Prints the failure line and returns false on any failure.
bool build_scene(rhi::Device& device, const std::filesystem::path& shader_directory,
                 rhi::Format target_format, Scene& scene) {
  if (!load_shader(shader_directory, geometry::kPulledMeshShader, scene.vertex) ||
      !load_shader(shader_directory, geometry::kVertexColorShader, scene.pixel)) {
    return false;
  }
  const rendering::PulledCubeFailure cube = rendering::create_pulled_cube(
      device, scene.vertex.artifact, scene.pixel.artifact, target_format, scene.cube);
  if (!cube.ok()) {
    std::cerr << "game_host: the cube draw failed: error " << static_cast<unsigned>(cube.error)
              << ": " << cube.detail << '\n';
    return false;
  }
  return true;
}

// ---- The converted asset ---------------------------------------------------------------

// The layers over --cache and --overrides, with no published cache; prints
// the failure and, for every failure of the converted layer (the directory
// absent, its record missing, unreadable, invalid or of another format
// version), the converter command that makes one. LayerDirectoryUnreadable
// gets no pointer: it may name the overrides directory, which the converter
// does not make; its detail names the path.
std::optional<content::LayerDirectories>
open_asset_layers(const bw::hosts::GameHostOptions& options) {
  std::optional<std::filesystem::path> overrides;
  if (!options.overrides.empty()) {
    overrides = options.overrides;
  }
  const content::OpenLayersResult opened =
      content::open_layers(overrides, std::nullopt, options.cache);
  if (opened.ok()) {
    return opened.layers;
  }
  std::cerr << "game_host: opening the content layers failed: error "
            << static_cast<unsigned>(opened.error) << " " << content::layer_error_name(opened.error)
            << ": " << opened.detail << '\n';
  if (opened.error == content::LayerError::ConvertedNotADirectory ||
      opened.error == content::LayerError::ConvertedRecordMissing ||
      opened.error == content::LayerError::ConvertedRecordUnreadable ||
      opened.error == content::LayerError::ConvertedRecordInvalid ||
      opened.error == content::LayerError::ConvertedRecordVersionMismatch) {
    std::cerr << "game_host: " << options.cache.string()
              << " is not a converted-originals cache this game_host can read; make it with: "
                 "content_converter convert --install <install directory> --out "
              << options.cache.string() << '\n';
  }
  return std::nullopt;
}

// Everything the model draw holds: the two artifacts, the model, its
// fixture pipeline and the depth target.
struct AssetScene {
  shaders::ArtifactFile vertex{};
  shaders::ArtifactFile pixel{};
  rendering::ModelResources model{};
  rendering::TexturedModel textured{};
  rhi::Ref<rhi::Texture> depth{};
};

// The material id of the first draw that samples the white fallback:
// "none" when no draw does, "kNoMaterial" for a draw without a material.
std::string first_fallback_name(const AssetScene& scene) {
  if (scene.textured.fallback_draws == 0U) {
    return "none";
  }
  const std::uint32_t material =
      scene.model.mesh.draws[scene.textured.first_fallback_draw].material;
  if (material == geometry::kNoMaterial) {
    return "kNoMaterial";
  }
  return std::string(scene.model.materials[material].asset.id().view());
}

// Loads the model --asset names from the layers over --cache and
// --overrides and builds its fixture pipeline. Prints the failure line and
// returns false on any failure.
bool build_asset_scene(rhi::Device& device, const bw::hosts::GameHostOptions& options,
                       rhi::Format target_format, AssetScene& scene) {
  const std::optional<content::LayerDirectories> layers = open_asset_layers(options);
  if (!layers.has_value()) {
    return false;
  }
  const content::Loader loader(*layers);
  const content::AssetIdResult id = content::make_asset_id(options.asset);
  if (!id.ok()) {
    std::cerr << "game_host: the asset id " << options.asset << " does not validate: error "
              << static_cast<unsigned>(id.error) << '\n';
    return false;
  }
  const rendering::ModelLoadFailure loaded = rendering::load_model(
      device, loader, id.id, rendering::kTexturedModelTextureParameters, scene.model);
  if (!loaded.ok()) {
    std::cerr << "game_host: loading the model " << options.asset << " failed: error "
              << static_cast<unsigned>(loaded.error) << " for " << loaded.id << ": "
              << loaded.detail << '\n';
    return false;
  }
  const geometry::MeshResources& mesh = scene.model.mesh;
  // A square target puts the eye nearest the center, so a sphere framed
  // here is framed at every aspect.
  if (!asset_world_to_clip(0.0, rhi::Extent2{1, 1}, mesh).has_value()) {
    return fail_unframeable(options.asset, mesh);
  }
  if (!load_shader(options.shaders, geometry::kTexturedMeshShader, scene.vertex) ||
      !load_shader(options.shaders, geometry::kTexturedColorShader, scene.pixel)) {
    return false;
  }
  const rendering::TexturedModelFailure textured =
      rendering::create_textured_model(device, scene.model, scene.vertex.artifact,
                                       scene.pixel.artifact, target_format, scene.textured);
  if (!textured.ok()) {
    std::cerr << "game_host: the textured draw of " << options.asset << " failed: error "
              << static_cast<unsigned>(textured.error) << " at index " << textured.index << ": "
              << textured.detail << '\n';
    return false;
  }
  // Always printed, a count of 0 included, so a run whose draws all sample
  // the fallback reads differently from a textured one.
  std::cout << "base_color_fallback draws=" << scene.textured.fallback_draws
            << " of=" << mesh.draws.size() << " first=" << first_fallback_name(scene) << '\n';
  return true;
}

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
  // The cube's commands, or the converted asset's, into the list between
  // the back buffer's two transitions. record_asset: false after a failure
  // line.
  void record_cube(rhi::CommandList& list, rhi::Texture& back_buffer, rhi::Extent2 size,
                   double elapsed);
  bool record_asset(rhi::CommandList& list, rhi::Texture& back_buffer, rhi::Extent2 size,
                    double elapsed);
  // The answer to BackBufferSizeMismatch (viewport.hpp, invariant 4).
  FrameOutcome resize_after_mismatch();

  bw::hosts::GameHostOptions options_;
  bw::platform::macos::PlatformTime time_{};
  std::unique_ptr<platform::Window> window_{};
  std::unique_ptr<rhi::Device> device_{};
  rhi::Ref<rhi::Viewport> viewport_{};
  // Exactly one of the two scenes exists: the asset's with --asset, else
  // the cube's.
  std::unique_ptr<Scene> scene_{};
  std::unique_ptr<AssetScene> asset_{};
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

  if (options_.asset.empty()) {
    scene_ = std::make_unique<Scene>();
    if (!build_scene(*device_, options_.shaders, viewport_->format(), *scene_)) {
      return 1;
    }
  } else {
    asset_ = std::make_unique<AssetScene>();
    if (!build_asset_scene(*device_, options_, viewport_->format(), *asset_)) {
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
  if (asset_ == nullptr) {
    record_cube(list, back_buffer, size, elapsed);
  } else if (!record_asset(list, back_buffer, size, elapsed)) {
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

void Host::record_cube(rhi::CommandList& list, rhi::Texture& back_buffer, rhi::Extent2 size,
                       double elapsed) {
  rendering::begin_cleared_pass(list, back_buffer, nullptr, size, "game_host frame");
  rendering::record_pulled_cube(list, scene_->cube, cube_world_to_clip(elapsed, size));
  list.end_render_pass();
}

// The depth target follows the viewport's size (target_pass.hpp,
// resize_depth_target): a frame whose viewport no longer matches it (after
// a Resized event or a mismatch resize) gets a new one. False after a
// failure line.
bool Host::record_asset(rhi::CommandList& list, rhi::Texture& back_buffer, rhi::Extent2 size,
                        double elapsed) {
  AssetScene& scene = *asset_;
  const rhi::Status<rhi::ResourceError> resized = rendering::resize_depth_target(
      *device_, size, rendering::kTexturedModelDepthFormat, "game_host depth", scene.depth);
  if (!resized.ok()) {
    fail("depth target", resized.error(), resized.detail());
    return false;
  }
  const std::optional<math::Matrix4f> world_to_clip =
      asset_world_to_clip(elapsed, size, scene.model.mesh);
  // The startup check framed this sphere at the aspect that gives the
  // nearest eye, so this is not reached; the empty result is still handled.
  if (!world_to_clip.has_value()) {
    return fail_unframeable(options_.asset, scene.model.mesh);
  }
  rendering::begin_cleared_pass(list, back_buffer, scene.depth.get(), size, "game_host frame");
  rendering::record_textured_model(list, scene.textured, scene.model, *world_to_clip);
  list.end_render_pass();
  return true;
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
  asset_.reset();
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
      if (!executable_directory(directory, detail)) {
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
