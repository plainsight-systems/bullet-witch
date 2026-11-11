// Integration test: the character_viewer executable's dump, script and
// usage contracts, probed by running it as a child process (the grammars are
// viewer_options.hpp's and script.hpp's unit tests, the PNG bytes png.hpp's).
// The dump and script runs open a window, draw the PW0 cache the CTest
// fixture pw0_cache converted and need a GUI session; the usage probe opens
// no window.
#include "bw/character_viewer/script.hpp"
#include "bw/rendering/scene/orbit.hpp"
#include "bw/testing/harness.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

extern char** environ;

namespace {

using bw::testing::require;

// cache is given to the dump and script runs alone, which need it
// non-empty; the usage probe takes three arguments.
struct Arguments {
  std::string viewer;
  std::string shaders;
  std::string output;
  std::string cache;
};

struct ChildResult {
  std::string out, err, transcript;
};

std::string read_file(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Runs `executable` with `options`, stdout and stderr in
// `<output>/<name>.stdout` and `.stderr`, and reads both; fails unless it
// exits normally with `expected`.
ChildResult run_child(const std::string& executable, const std::string& output,
                      std::vector<const char*> options, const std::string& name, int expected) {
  const std::string path = output + "/" + name;
  posix_spawn_file_actions_t actions{};
  require(posix_spawn_file_actions_init(&actions) == 0, "posix_spawn_file_actions_init failed");
  // A refused addopen is a failed requirement naming the call and the
  // stream; the actions are destroyed first.
  const auto add_open = [&actions](int descriptor, const std::string& file,
                                   std::string_view stream) {
    const int opened = posix_spawn_file_actions_addopen(&actions, descriptor, file.c_str(),
                                                        O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (opened != 0) {
      posix_spawn_file_actions_destroy(&actions);
    }
    require(opened == 0, "posix_spawn_file_actions_addopen for " + std::string(stream) +
                             " failed: errno " + std::to_string(opened));
  };
  add_open(STDOUT_FILENO, path + ".stdout", "stdout");
  add_open(STDERR_FILENO, path + ".stderr", "stderr");
  options.insert(options.begin(), executable.c_str());
  options.push_back(nullptr);
  pid_t child = 0;
  const int spawned = posix_spawn(&child, options[0], &actions, nullptr,
                                  const_cast<char* const*>(options.data()), environ);
  posix_spawn_file_actions_destroy(&actions);
  require(spawned == 0,
          "posix_spawn of " + executable + " failed: errno " + std::to_string(spawned));
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    require(errno == EINTR, "waitpid failed for " + executable);
  }
  require(WIFEXITED(status), executable + " did not exit normally");
  ChildResult result{read_file(path + ".stdout"), read_file(path + ".stderr"), {}};
  result.transcript = "exit " + std::to_string(WEXITSTATUS(status)) + "; stdout: " + result.out +
                      "; stderr: " + result.err;
  require(WEXITSTATUS(status) == expected,
          "expected exit " + std::to_string(expected) + ": " + result.transcript);
  return result;
}

std::vector<std::string_view> lines_starting_with(std::string_view text, std::string_view prefix) {
  std::vector<std::string_view> found;
  for (std::size_t at = 0, end = 0; at < text.size(); at = end + 1) {
    end = std::min(text.find('\n', at), text.size());
    if (text.substr(at, end - at).starts_with(prefix)) {
      found.push_back(text.substr(at, end - at));
    }
  }
  return found;
}

std::uint32_t u32_be(const std::string& bytes, std::size_t at) {
  const auto byte = [&bytes](std::size_t i) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes.at(i)));
  };
  return (byte(at) << 24U) | (byte(at + 1U) << 16U) | (byte(at + 2U) << 8U) | byte(at + 3U);
}

// One `dump=<file> size=<w>x<h> yaw=<y> pitch=<p> distance=<d>
// offset=<x>,<y>,<z>` line (script.hpp, contract 4), read whole; a
// requirement fails when the line is not one.
struct DumpLine {
  std::string file;
  std::uint32_t width{}, height{};
  double yaw{}, pitch{}, distance{};
  std::array<double, 3> offset{};
};

DumpLine read_dump_line(std::string_view line) {
  const std::size_t size_at = line.rfind(" size=");
  require(line.starts_with("dump=") && size_at != std::string_view::npos,
          "the dump line has a file and a size: " + std::string(line));
  DumpLine parsed{std::string(line.substr(5, size_at - 5))};
  const char* at = line.data() + size_at + 6;
  const char* const end = line.data() + line.size();
  const auto number = [&at, end](auto& value, std::string_view head) {
    if (!std::string_view(at, static_cast<std::size_t>(end - at)).starts_with(head)) {
      return false;
    }
    const auto read = std::from_chars(at + head.size(), end, value);
    at = read.ptr;
    return read.ec == std::errc{};
  };
  const bool ok = number(parsed.width, "") && number(parsed.height, "x") &&
                  number(parsed.yaw, " yaw=") && number(parsed.pitch, " pitch=") &&
                  number(parsed.distance, " distance=") && number(parsed.offset[0], " offset=") &&
                  number(parsed.offset[1], ",") && number(parsed.offset[2], ",") && at == end &&
                  parsed.width > 0U && parsed.height > 0U;
  require(ok, "the dump line has a size and a camera: " + std::string(line));
  return parsed;
}

bool near(double value, double expected, double tolerance) {
  return std::fabs(value - expected) <= tolerance;
}

// The offset a pan of (dx, dy) pixels at `line`'s camera and size moves
// `line`'s offset to (orbit.hpp, contracts 4 and 5): -dx pixel widths
// along the right vector and dy along the up vector, the pixel width at
// the target's depth for the dump's height.
std::array<double, 3> panned_offset(const DumpLine& line, double dx, double dy) {
  const double wpp = 2.0 * line.distance * std::tan(bw::scene::kFrameVerticalFov / 2.0) /
                     static_cast<double>(line.height);
  const std::array<double, 3> right{std::cos(line.yaw), 0.0, -std::sin(line.yaw)};
  const std::array<double, 3> forward{-std::cos(line.pitch) * std::sin(line.yaw),
                                      -std::sin(line.pitch),
                                      -std::cos(line.pitch) * std::cos(line.yaw)};
  const std::array<double, 3> up{right[1] * forward[2] - right[2] * forward[1],
                                 right[2] * forward[0] - right[0] * forward[2],
                                 right[0] * forward[1] - right[1] * forward[0]};
  std::array<double, 3> moved{};
  for (std::size_t i = 0; i < moved.size(); ++i) {
    moved[i] = line.offset[i] - dx * wpp * right[i] + dy * wpp * up[i];
  }
  return moved;
}

// Each component within 1e-5 of the expected offset's largest component.
bool near_offset(const std::array<double, 3>& value, const std::array<double, 3>& expected) {
  const double scale =
      std::max({std::fabs(expected[0]), std::fabs(expected[1]), std::fabs(expected[2]), 1e-6});
  return near(value[0], expected[0], 1e-5 * scale) && near(value[1], expected[1], 1e-5 * scale) &&
         near(value[2], expected[2], 1e-5 * scale);
}

// (viewer_options.hpp, --asset repeatable and --dump; the dump contract of
// the viewer; script.hpp, contract 4): two scenes of the PW0 mesh, --dump
// and --seconds 1 exit 0 with exactly one dump line, and the file
// begins with the PNG signature and an IHDR whose width and height are the
// printed size. Alone catches the dump path unwired: the readback mark, the
// staging lock, the row packing, the writer or the file.
void dumps_first_frame(const Arguments& args) {
  const std::string file = args.output + "/first_frame.png";
  std::error_code removed;
  std::filesystem::remove(file, removed);
  require(!std::filesystem::exists(file), "the previous dump is removed: " + file);
  const ChildResult result =
      run_child(args.viewer, args.output,
                {"--cache", args.cache.c_str(), "--asset", "model.pw0.mesh", "--asset",
                 "model.pw0.mesh", "--seconds", "1", "--shaders", args.shaders.c_str(), "--size",
                 "320x200", "--dump", file.c_str()},
                "dumps_first_frame", 0);
  const std::vector<std::string_view> dumps = lines_starting_with(result.out, "dump=");
  require(dumps.size() == 1U, "stdout has " + std::to_string(dumps.size()) +
                                  " dump lines, not one: " + result.transcript);
  const DumpLine line = read_dump_line(dumps.front());
  require(line.file == file, "the dump line names the file: " + std::string(dumps.front()));
  const std::uint32_t width = line.width;
  const std::uint32_t height = line.height;

  const std::string bytes = read_file(file);
  constexpr std::array<unsigned char, 8> kSignature{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  require(bytes.size() >= 24U, "the dump holds a signature and an IHDR header: " +
                                   std::to_string(bytes.size()) + " bytes");
  require(std::equal(kSignature.begin(), kSignature.end(), bytes.begin(),
                     [](unsigned char a, char b) { return a == static_cast<unsigned char>(b); }),
          "the dump starts with the PNG signature");
  require(bytes.substr(12, 4) == "IHDR", "the first chunk is IHDR");
  require(u32_be(bytes, 16) == width && u32_be(bytes, 20) == height,
          "the IHDR size " + std::to_string(u32_be(bytes, 16)) + "x" +
              std::to_string(u32_be(bytes, 20)) + " is the printed size");
}

// (script.hpp, contracts 3, 4 and 5; orbit.hpp, turn, dolly and pan
// through the viewer): a script that drags, dumps, switches scene with ],
// dumps, scrolls, dumps, holds Up across a dump, holds = across a dump,
// drags with the right button and dumps, then presses W, presses D,
// releases W, releases D and dumps, run with no --seconds and no Escape,
// exits 0 when its last command's frame is drawn, with the first frame's
// dump and the script's seven, in order and of one size. From the bytes:
// the drag changes the frame from the first; the switch resets the camera
// to the second scene's framing, the same model's, so its dump is the
// first frame's bytes; the scroll changes the frame again. From the dump
// lines' cameras: the 60 pixel drag right lowers yaw by 60 drag pixels'
// radians; the switch gives the first camera and a zero offset; the scroll
// of -3 lines scales distance by kOrbitDollyPerLine^-3; Up, held in two
// frames of kScriptFrameSeconds, lowers pitch by two frames of
// kOrbitKeyPixelsPerSecond (Up is a drag upward), and = held in two frames
// scales distance by kOrbitDollyPerLine to minus two frames of
// kOrbitKeyLinesPerSecond, each leaving the other two fields. The pans come
// last so the turn, the switch and the dollies are read with no offset;
// the camera then has the pitch Up left, so each expected offset is
// computed with orbit.hpp contract 4's right and up vectors from the
// printed yaw and pitch, and the pixel width from contract 5 with that
// dump's distance and height: the right drag of 30 pixels right and 20
// down moves the offset by -30 pixel widths along right and 20 along up
// and leaves yaw, pitch and distance; W and D, one command each per frame,
// are each held for two frames (W in the first two, D in the second and
// third), a drag of two frames of kOrbitKeyPixelsPerSecond up and as many
// right. An end of script that does not end the run is a CTest timeout.
// Alone catches: the apply path or the end of script unwired; a switch
// that does not reset the camera or its offset; a drag or dolly that does
// not change the frame; held turn and dolly keys that do not act each
// frame or act with the wrong sign, rate or frame time; a right drag or a
// held W or D that does not pan, or pans with the wrong sign, scale or
// axis; and a dump line that does not carry the drawn camera.
// The scene index itself is not observable: both scenes are the one mesh
// the fixture converts.
void scripted_input_dumps(const Arguments& args) {
  const std::array<std::string, 8> files{args.output + "/first.png", args.output + "/a.png",
                                         args.output + "/b.png",     args.output + "/c.png",
                                         args.output + "/d.png",     args.output + "/e.png",
                                         args.output + "/f.png",     args.output + "/g.png"};
  for (const std::string& file : files) {
    std::error_code removed;
    std::filesystem::remove(file, removed);
    require(!std::filesystem::exists(file), "the previous dump is removed: " + file);
  }
  const std::string script = args.output + "/scripted_input.txt";
  {
    std::ofstream stream(script, std::ios::binary | std::ios::trunc);
    stream << "button left down 200 100\n"
           << "move 260 100 left\n"
           << "button left up 260 100\n"
           << "dump " << files[1] << "\n"
           << "key RightBracket\n"
           << "dump " << files[2] << "\n"
           << "scroll -3\n"
           << "dump " << files[3] << "\n"
           << "key Up down\n"
           << "dump " << files[4] << "\n"
           << "key Up up\n"
           << "key Equal down\n"
           << "dump " << files[5] << "\n"
           << "key Equal up\n"
           << "button right down 300 200\n"
           << "move 330 220 right\n"
           << "button right up 330 220\n"
           << "dump " << files[6] << "\n"
           << "key W down\n"
           << "key D down\n"
           << "key W up\n"
           << "key D up\n"
           << "dump " << files[7] << "\n";
    stream.close();
    require(static_cast<bool>(stream), "the script is written: " + script);
  }
  const ChildResult result =
      run_child(args.viewer, args.output,
                {"--cache", args.cache.c_str(), "--asset", "model.pw0.mesh", "--asset",
                 "model.pw0.mesh", "--size", "160x90", "--shaders", args.shaders.c_str(), "--dump",
                 files[0].c_str(), "--script", script.c_str()},
                "scripted_input_dumps", 0);
  const std::vector<std::string_view> dumps = lines_starting_with(result.out, "dump=");
  require(dumps.size() == files.size(), "stdout has " + std::to_string(dumps.size()) +
                                            " dump lines, not eight: " + result.transcript);
  std::vector<DumpLine> camera;
  for (std::size_t i = 0; i < files.size(); ++i) {
    DumpLine line = read_dump_line(dumps[i]);
    require(line.file == files[i],
            "dump line " + std::to_string(i) + " names its file: " + std::string(dumps[i]));
    require(i == 0U || (line.width == camera[0].width && line.height == camera[0].height),
            "dump line " + std::to_string(i) + " is at the first's size: " + std::string(dumps[i]));
    camera.push_back(std::move(line));
  }
  const std::string first = read_file(files[0]);
  const std::string a = read_file(files[1]);
  const std::string b = read_file(files[2]);
  const std::string c = read_file(files[3]);
  require(!first.empty() && a != first, "the drag changed the frame");
  require(b == first, "the switch reset the camera: b.png is the first frame's bytes");
  require(c != b, "the scroll changed the frame");

  namespace scene = bw::scene;
  const DumpLine& first_camera = camera[0];
  const DumpLine& a_camera = camera[1];
  const DumpLine& b_camera = camera[2];
  const DumpLine& c_camera = camera[3];
  const DumpLine& d_camera = camera[4];
  const DumpLine& e_camera = camera[5];
  const DumpLine& f_camera = camera[6];
  const DumpLine& g_camera = camera[7];
  const double radians_per_pixel = scene::kOrbitRadiansPerPixel;
  const double frame = bw::character_viewer::kScriptFrameSeconds;
  require(near(a_camera.yaw - first_camera.yaw, -60.0 * radians_per_pixel, 1e-5),
          "the 60 pixel drag right lowered yaw: " + std::string(dumps[1]));
  require(b_camera.yaw == first_camera.yaw && b_camera.pitch == first_camera.pitch &&
              b_camera.distance == first_camera.distance,
          "the switch gave the first camera: " + std::string(dumps[2]));
  require(b_camera.offset == std::array<double, 3>{},
          "the switch zeroed the offset: " + std::string(dumps[2]));
  require(
      near(c_camera.distance / b_camera.distance, std::pow(scene::kOrbitDollyPerLine, -3.0), 1e-4),
      "the scroll of -3 lines scaled distance: " + std::string(dumps[3]));
  const double up_turn = 2.0 * scene::kOrbitKeyPixelsPerSecond * frame * radians_per_pixel;
  require(near(d_camera.pitch, c_camera.pitch - up_turn, 1e-5),
          "Up held for two frames lowered pitch: " + std::string(dumps[4]));
  require(near(d_camera.yaw, c_camera.yaw, 1e-6) &&
              near(d_camera.distance, c_camera.distance, 1e-6),
          "Up held left yaw and distance: " + std::string(dumps[4]));
  const double equal_scale =
      std::pow(scene::kOrbitDollyPerLine, -2.0 * scene::kOrbitKeyLinesPerSecond * frame);
  require(near(e_camera.distance / d_camera.distance, equal_scale, 1e-4),
          "= held for two frames moved in: " + std::string(dumps[5]));
  require(e_camera.yaw == d_camera.yaw && e_camera.pitch == d_camera.pitch,
          "= held left yaw and pitch: " + std::string(dumps[5]));
  require(near_offset(f_camera.offset, panned_offset(e_camera, 30.0, 20.0)),
          "the 30 by 20 pixel right drag panned by its pixel widths: " + std::string(dumps[6]));
  require(f_camera.yaw == e_camera.yaw && f_camera.pitch == e_camera.pitch &&
              f_camera.distance == e_camera.distance,
          "the right drag left yaw, pitch and distance: " + std::string(dumps[6]));
  const double key_pixels = 2.0 * scene::kOrbitKeyPixelsPerSecond * frame;
  require(near_offset(g_camera.offset, panned_offset(f_camera, key_pixels, -key_pixels)),
          "W and D, each held for two frames, panned up and right by their pixel widths: " +
              std::string(dumps[7]));
}

// The parse's wiring (viewer_options.hpp, --dump needs --asset): `--dump`
// alone exits 2 with the usage line on stderr.
void dump_without_asset_exits_2(const Arguments& args) {
  const ChildResult result =
      run_child(args.viewer, args.output, {"--dump", "x.png"}, "dump_without_asset", 2);
  require(!lines_starting_with(result.err, "Usage: character_viewer").empty(),
          "stderr has no usage line: " + result.transcript);
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"dumps_first_frame", &dumps_first_frame},
    Scenario{"dump_without_asset_exits_2", &dump_without_asset_exits_2},
    Scenario{"scripted_input_dumps", &scripted_input_dumps},
};

// The scenarios that take the PW0 cache.
constexpr std::array<std::string_view, 2> kCacheScenarios{"dumps_first_frame",
                                                          "scripted_input_dumps"};

} // namespace

// The harness has matched argv[1] to a scenario before it calls the parse,
// so the parse reads the scenario's name from it: the dump and script runs
// take exactly four arguments with a non-empty cache, the usage probe
// exactly three.
int main(int argc, char** argv) {
  const std::string_view scenario =
      argc >= 2 ? std::string_view(std::span(argv, static_cast<std::size_t>(argc))[1])
                : std::string_view{};
  return bw::testing::run_scenario(
      argc, argv, kScenarios,
      "<character_viewer> <metal artifact directory> <output directory>, then for "
      "dumps_first_frame and scripted_input_dumps only <PW0 cache>",
      [scenario](std::span<char* const> rest, Arguments& out) {
        if (std::find(kCacheScenarios.begin(), kCacheScenarios.end(), scenario) !=
            kCacheScenarios.end()) {
          if (rest.size() != 4U || std::string_view(rest[3]).empty()) {
            return false;
          }
          out = Arguments{rest[0], rest[1], rest[2], rest[3]};
          return true;
        }
        if (rest.size() != 3U) {
          return false;
        }
        out = Arguments{rest[0], rest[1], rest[2], {}};
        return true;
      });
}
