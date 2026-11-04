// game_host: the first stage of the game executable. It opens the window
// (bw/platform/os/macos/window.hpp), creates the Metal device and the
// viewport on the window's surface (bw/rendering/rhi/device.hpp,
// viewport.hpp), and draws the fixture cube with pulled vertices
// (bw/rendering/geometry/pulled_mesh_fixture.hpp) every frame on the main
// thread until the window is asked to close or the seconds limit passes.
//
// Command line: bw/hosts/game_host_options.hpp states the grammar and
// parses it. Without --shaders the directory is
// <executable directory>/shaders/metal, the executable's path read from the
// OS, not from argv[0], and resolved through symbolic links; that
// resolution is the OS-bound part and stays here.
//
// Exit codes: 0 when the window asked to close or the seconds limit passed,
// with one line on stdout, `frames=N elapsed=S waited=W`: the frames drawn,
// the elapsed seconds and how many end_frame calls blocked on the oldest
// frame in flight; 1 on a named failure, one line on stderr naming what
// failed and, for an error code, the enumerator's number and the detail;
// 2 on a usage error, with a line naming the error and the argument, then
// the usage line, on stderr. Named failures include
// an executable path the OS cannot give or resolve when --shaders is
// absent, and the OS refusing the borderless fullscreen --fullscreen asked
// for. Nothing falls back: every failure ends the run.
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
#include "bw/hosts/game_host_options.hpp"
#include "bw/platform/os/macos/platform_time.hpp"
#include "bw/platform/os/macos/window.hpp"
#include "bw/platform/window/window.hpp"
#include "bw/rendering/geometry/pulled_mesh_fixture.hpp"
#include "bw/rendering/geometry/vertex_streams.hpp"
#include "bw/rendering/rhi/access.hpp"
#include "bw/rendering/rhi/command_list.hpp"
#include "bw/rendering/rhi/device.hpp"
#include "bw/rendering/rhi/format.hpp"
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/pipeline_states.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/transitions.hpp"
#include "bw/rendering/rhi/viewport.hpp"
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"

#include <mach-o/dyld.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

// The artifact views `bytes`; the owner keeps both together and does not
// copy them.
struct LoadedShader {
  std::vector<std::byte> bytes{};
  shaders::Artifact artifact{};
};

// Permutation 0 of a registry shader from `directory`, checked against the
// registry's expectations (artifact.hpp, load_artifact). Prints the failure
// line and returns false on any failure.
bool load_shader(const std::filesystem::path& directory, std::string_view name,
                 LoadedShader& loaded) {
  const shaders::ShaderDescription* shader = shaders::find_shader(shaders::shader_registry(), name);
  if (shader == nullptr) {
    std::cerr << "game_host: the shader registry has no shader named " << name << '\n';
    return false;
  }
  const shaders::PermutationId permutation{};
  const std::filesystem::path path =
      directory / shaders::artifact_file_name(shader->name, permutation, shader->stage);
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    std::cerr << "game_host: the shader artifact " << path.string() << " cannot be read\n";
    return false;
  }
  const std::vector<char> chars{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
  if (stream.bad()) {
    std::cerr << "game_host: reading the shader artifact " << path.string() << " failed\n";
    return false;
  }
  loaded.bytes.resize(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    loaded.bytes[i] = static_cast<std::byte>(chars[i]);
  }
  std::vector<const shaders::BlockLayout*> blocks;
  if (shader->draw_block != nullptr) {
    blocks.push_back(shader->draw_block);
  }
  const shaders::LoadExpectations expectations{&shaders::engine_root_layout(), blocks,
                                               shader->permutation_count};
  const shaders::ArtifactLoadResult result = shaders::load_artifact(loaded.bytes, expectations);
  if (!result.ok()) {
    const shaders::LoadError& error = result.error();
    std::cerr << "game_host: loading the shader artifact " << path.string() << " failed: error "
              << static_cast<unsigned>(error.code) << ": block '" << error.block_name
              << "' expected " << error.expected << " actual " << error.actual << " at byte "
              << error.byte_offset << '\n';
    return false;
  }
  loaded.artifact = result.artifact();
  return true;
}

// ---- The cube's transform ------------------------------------------------------------

// Row vectors, as the shader computes mul(float4(p, 1), world_to_clip).
using Mat4 = std::array<std::array<float, 4>, 4>;
using Vec3 = std::array<float, 3>;

Mat4 multiply(const Mat4& a, const Mat4& b) {
  Mat4 out{};
  for (std::size_t r = 0; r < 4; ++r) {
    for (std::size_t j = 0; j < 4; ++j) {
      out[r][j] = a[r][0] * b[0][j] + a[r][1] * b[1][j] + a[r][2] * b[2][j] + a[r][3] * b[3][j];
    }
  }
  return out;
}
Vec3 normalized(const Vec3& v) {
  const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  return {v[0] / length, v[1] / length, v[2] / length};
}
Vec3 cross(const Vec3& a, const Vec3& b) {
  return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
}
float dot3(const Vec3& a, const Vec3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

// A rotation of `angle` radians about the unit `axis`, right-handed, as a
// row-vector matrix: the transpose of the column-vector rotation
// cos(a) I + sin(a) [axis]x + (1 - cos(a)) axis axis^T.
Mat4 rotation(const Vec3& axis, float angle) {
  const float c = std::cos(angle);
  const float s = std::sin(angle);
  const float t = 1.0F - c;
  const float x = axis[0];
  const float y = axis[1];
  const float z = axis[2];
  return Mat4{{{t * x * x + c, t * x * y + s * z, t * x * z - s * y, 0.0F},
               {t * x * y - s * z, t * y * y + c, t * y * z + s * x, 0.0F},
               {t * x * z + s * y, t * y * z - s * x, t * z * z + c, 0.0F},
               {0.0F, 0.0F, 0.0F, 1.0F}}};
}

// The cube turning about a tilted axis at a fixed rate, seen by a camera at
// (3.0, 2.4, 4.2) looking at the origin with +y up (right-handed view), a
// perspective with a 40 degree vertical field of view at the aspect of
// `size` (width over height), near 0.1, far 100, depth mapped to [0, 1]. The cube's
// bounding sphere stays inside the view at every angle, so nothing clips.
// Faces toward the camera are counter-clockwise on screen, which is what
// the pipeline's cull of clockwise faces keeps.
Mat4 cube_world_to_clip(double elapsed_seconds, rhi::Extent2 size) {
  const float aspect = static_cast<float>(size.width) / static_cast<float>(size.height);
  constexpr double kRadiansPerSecond = 0.9;
  const Vec3 axis = normalized({0.3F, 1.0F, 0.2F});
  const auto angle =
      static_cast<float>(std::fmod(elapsed_seconds * kRadiansPerSecond, 6.283185307179586));
  const Mat4 model = rotation(axis, angle);
  const Vec3 eye{3.0F, 2.4F, 4.2F};
  const Vec3 z = normalized(eye);
  const Vec3 x = normalized(cross({0.0F, 1.0F, 0.0F}, z));
  const Vec3 y = cross(z, x);
  const Mat4 view{{{x[0], y[0], z[0], 0.0F},
                   {x[1], y[1], z[1], 0.0F},
                   {x[2], y[2], z[2], 0.0F},
                   {-dot3(x, eye), -dot3(y, eye), -dot3(z, eye), 1.0F}}};
  const float f = 1.0F / std::tan(40.0F * 3.14159265358979F / 360.0F);
  const float near = 0.1F;
  const float far = 100.0F;
  const Mat4 projection{{{f / aspect, 0.0F, 0.0F, 0.0F},
                         {0.0F, f, 0.0F, 0.0F},
                         {0.0F, 0.0F, far / (near - far), -1.0F},
                         {0.0F, 0.0F, near * far / (near - far), 0.0F}}};
  return multiply(multiply(model, view), projection);
}

// ---- The scene -----------------------------------------------------------------------

// Everything the cube draw holds: the two artifacts, the shared state
// objects, the pipeline, the two stream buffers with their views, the index
// buffer, and the cube's layout and counts.
struct Scene {
  LoadedShader vertex{};
  LoadedShader pixel{};
  geometry::FixtureCube cube{};
  rhi::Ref<rhi::RasterizerState> rasterizer{};
  rhi::Ref<rhi::DepthStencilState> depth_stencil{};
  rhi::Ref<rhi::BlendState> blend{};
  rhi::Ref<rhi::GraphicsPipeline> pipeline{};
  rhi::Ref<rhi::Buffer> stream0{};
  rhi::Ref<rhi::Buffer> stream1{};
  rhi::Ref<rhi::ShaderResourceView> view0{};
  rhi::Ref<rhi::ShaderResourceView> view1{};
  rhi::Ref<rhi::Buffer> indices{};
  geometry::VertexStreams streams{};
};

// A stream buffer with the cube's bytes as initial data, read by the vertex
// shader through a raw view in the buffer descriptor array.
bool make_stream(rhi::Device& device, std::span<const std::byte> bytes, std::string_view name,
                 rhi::Ref<rhi::Buffer>& buffer, rhi::Ref<rhi::ShaderResourceView>& view) {
  rhi::BufferDescription description{};
  description.size = bytes.size();
  description.usage = rhi::BufferUsage::ShaderResource;
  description.initial_access = rhi::Access::ShaderResourceNonPixel;
  description.debug_name = name;
  rhi::BufferResult created = device.create_buffer(description, bytes);
  if (!created.ok()) {
    fail(name, created.error(), created.detail());
    return false;
  }
  buffer = std::move(created.value());
  rhi::ShaderResourceViewResult viewed =
      device.create_shader_resource_view(buffer, rhi::BufferViewDescription{});
  if (!viewed.ok()) {
    fail(std::string(name) + " view", viewed.error(), viewed.detail());
    return false;
  }
  view = std::move(viewed.value());
  return true;
}

// Loads the artifacts and creates the pipeline and the buffers. The pipeline
// targets the viewport's format with no depth target, culls clockwise
// (back) faces, and is made through precompile so the same call sequence
// holds under both miss policies (device.hpp, Pipelines). Prints the failure
// line and returns false on any failure.
bool build_scene(rhi::Device& device, const std::filesystem::path& shader_directory,
                 rhi::Format target_format, Scene& scene) {
  if (!load_shader(shader_directory, geometry::kPulledMeshShader, scene.vertex) ||
      !load_shader(shader_directory, geometry::kVertexColorShader, scene.pixel)) {
    return false;
  }

  rhi::RasterizerDescription cull_back{};
  cull_back.cull = rhi::CullMode::Clockwise;
  rhi::RasterizerStateResult rasterizer = device.rasterizer_state(cull_back);
  if (!rasterizer.ok()) {
    fail("rasterizer state", rasterizer.error(), rasterizer.detail());
    return false;
  }
  scene.rasterizer = std::move(rasterizer.value());
  rhi::DepthStencilDescription depth_off{};
  depth_off.depth_write = false;
  depth_off.depth_test = rhi::CompareFunction::Always;
  rhi::DepthStencilStateResult depth_stencil = device.depth_stencil_state(depth_off);
  if (!depth_stencil.ok()) {
    fail("depth stencil state", depth_stencil.error(), depth_stencil.detail());
    return false;
  }
  scene.depth_stencil = std::move(depth_stencil.value());
  rhi::BlendStateResult blend = device.blend_state(rhi::BlendDescription{});
  if (!blend.ok()) {
    fail("blend state", blend.error(), blend.detail());
    return false;
  }
  scene.blend = std::move(blend.value());

  rhi::GraphicsPipelineDescription description{};
  description.vertex_shader = &scene.vertex.artifact;
  description.pixel_shader = &scene.pixel.artifact;
  description.rasterizer = &*scene.rasterizer;
  description.depth_stencil = &*scene.depth_stencil;
  description.blend = &*scene.blend;
  description.targets.color_formats[0] = target_format;
  description.targets.color_count = 1;
  description.targets.sample_count = 1;
  description.primitive = rhi::PrimitiveType::TriangleList;
  description.debug_name = "game_host cube";
  const rhi::Status<rhi::PipelineError> precompiled =
      device.precompile(std::span<const rhi::GraphicsPipelineDescription>{&description, 1}, {});
  if (!precompiled.ok()) {
    fail("pipeline precompile", precompiled.error(), precompiled.detail());
    return false;
  }
  rhi::GraphicsPipelineResult pipeline = device.graphics_pipeline(description);
  if (!pipeline.ok()) {
    fail("graphics pipeline", pipeline.error(), pipeline.detail());
    return false;
  }
  scene.pipeline = std::move(pipeline.value());

  scene.cube = geometry::make_fixture_cube();
  const geometry::VertexStreamError layout =
      geometry::validate_vertex_stream_layout(scene.cube.layout);
  if (layout != geometry::VertexStreamError::None) {
    fail("cube layout validation", layout, "");
    return false;
  }
  if (!make_stream(device, scene.cube.stream0, "cube stream 0", scene.stream0, scene.view0) ||
      !make_stream(device, scene.cube.stream1, "cube stream 1", scene.stream1, scene.view1)) {
    return false;
  }
  rhi::BufferDescription index_description{};
  index_description.size = scene.cube.indices.size() * sizeof(std::uint16_t);
  index_description.stride = sizeof(std::uint16_t);
  index_description.usage = rhi::BufferUsage::Index;
  index_description.initial_access = rhi::Access::VertexOrIndexBuffer;
  index_description.debug_name = "cube indices";
  rhi::BufferResult indices =
      device.create_buffer(index_description, std::as_bytes(std::span(scene.cube.indices)));
  if (!indices.ok()) {
    fail("cube indices", indices.error(), indices.detail());
    return false;
  }
  scene.indices = std::move(indices.value());

  const std::array<geometry::VertexStreamBinding, 2> bindings{
      geometry::VertexStreamBinding{scene.view0->buffer_index(), scene.cube.vertex_count},
      geometry::VertexStreamBinding{scene.view1->buffer_index(), scene.cube.vertex_count}};
  scene.streams = geometry::make_vertex_streams(scene.cube.layout, bindings, 0);
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
  // One frame: the draw block, the recorded list, execute, end_frame.
  FrameOutcome draw_frame(double elapsed);
  // The answer to BackBufferSizeMismatch (viewport.hpp, invariant 4).
  FrameOutcome resize_after_mismatch();

  bw::hosts::GameHostOptions options_;
  bw::platform::macos::PlatformTime time_{};
  std::unique_ptr<platform::Window> window_{};
  std::unique_ptr<rhi::Device> device_{};
  rhi::Ref<rhi::Viewport> viewport_{};
  std::unique_ptr<Scene> scene_{};
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

  scene_ = std::make_unique<Scene>();
  if (!build_scene(*device_, options_.shaders, viewport_->format(), *scene_)) {
    return 1;
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
  Scene& scene = *scene_;
  rhi::Texture& back_buffer = viewport_->back_buffer();
  const rhi::Extent2 size = viewport_->size();

  const std::array<std::uint64_t, 2> stream_bytes{scene.stream0->description().size,
                                                  scene.stream1->description().size};
  const geometry::VertexStreamError range = geometry::validate_vertex_range(
      scene.cube.layout, stream_bytes, scene.streams.base_vertex, scene.cube.vertex_count);
  if (range != geometry::VertexStreamError::None) {
    fail("cube vertex range validation", range, "");
    return FrameOutcome::Failed;
  }
  geometry::PulledMeshDraw block{};
  const Mat4 world_to_clip = cube_world_to_clip(elapsed, size);
  for (std::size_t r = 0; r < 4; ++r) {
    block.world_to_clip.rows[r] = shaders::Float4{world_to_clip[r][0], world_to_clip[r][1],
                                                  world_to_clip[r][2], world_to_clip[r][3]};
  }
  block.streams = scene.streams;

  rhi::CommandList& list = *list_;
  list.reset();
  const rhi::Transition to_target{&back_buffer, rhi::Access::Present, rhi::Access::RenderTarget};
  list.transition(rhi::TransitionBatch{std::span<const rhi::Transition>{&to_target, 1}});
  rhi::RenderPassDescription pass{};
  pass.colors[0] =
      rhi::ColorTarget{&back_buffer, 0, 0, rhi::LoadAction::Clear, rhi::StoreAction::Store};
  pass.color_count = 1;
  pass.name = "game_host frame";
  list.begin_render_pass(pass);
  list.set_graphics_pipeline(*scene.pipeline);
  list.set_viewport(rhi::ViewportRect{0.0F, 0.0F, static_cast<float>(size.width),
                                      static_cast<float>(size.height), 0.0F, 1.0F});
  list.set_scissor(rhi::ScissorRect{0, 0, size.width, size.height});
  list.set_draw_block(block);
  list.set_index_buffer(*scene.indices, rhi::IndexFormat::Uint16, 0);
  list.draw_indexed(rhi::DrawIndexedArguments{scene.cube.index_count, 1, 0, 0, 0});
  list.end_render_pass();
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
    if (parse.error != bw::hosts::UsageError::None) {
      std::cerr << "game_host: usage error " << static_cast<unsigned>(parse.error) << ": "
                << parse.argument << '\n'
                << bw::hosts::kGameHostUsage << '\n';
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
