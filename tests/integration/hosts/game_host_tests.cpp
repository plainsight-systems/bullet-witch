// Integration test: the game_host executable's exit contract, probed by
// running it as a child process (the grammar is game_host_options.hpp's unit
// test). The artifact directory is the build's for the run, a missing one for
// the missing-artifact probe; both open a window and need a GUI session. The
// asset run reads the PW0 cache the CTest fixture pw0_cache converted.
#include "bw/testing/harness.hpp"

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdint>
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

// cache is given to the asset run alone, which needs it non-empty; the
// other scenarios take three arguments.
struct Arguments {
  std::string game_host;
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

// run_child for game_host.
ChildResult run_expecting(const Arguments& args, std::vector<const char*> options,
                          const std::string& name, int expected) {
  return run_child(args.game_host, args.output, std::move(options), name, expected);
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

// The exit line of a `--seconds 1` run: exactly one stdout line `frames=N
// elapsed=S waited=W`, W the end_frame calls that blocked: at least one
// frame at any display rate, at least the second given, the blocked calls
// among the frames.
void require_one_second_exit_line(const ChildResult& result) {
  const std::vector<std::string_view> lines = lines_starting_with(result.out, "frames=");
  require(lines.size() == 1U, "stdout has " + std::to_string(lines.size()) +
                                  " frames lines, not one: " + result.transcript);
  std::string_view rest = lines.front();
  const auto take = [&rest](std::string_view key, auto& value) {
    const char* first = rest.data() + std::min(key.size(), rest.size());
    const auto parsed = std::from_chars(first, rest.data() + rest.size(), value);
    const bool ok = rest.starts_with(key) && parsed.ec == std::errc{};
    rest.remove_prefix(static_cast<std::size_t>(parsed.ptr - rest.data()));
    return ok;
  };
  std::uint64_t frames = 0, waited = 0;
  double elapsed = 0.0;
  require(take("frames=", frames) && take(" elapsed=", elapsed) && take(" waited=", waited) &&
              rest.empty(),
          "the exit line is not `frames=N elapsed=S waited=W`: " + std::string(lines.front()));
  require(frames >= 1U && elapsed >= 1.0 && waited <= frames,
          "want frames >= 1, elapsed >= 1 and waited <= frames: " + result.transcript);
}

// `--seconds 1` exits 0 with the exit line above.
void game_host_runs_one_second(const Arguments& args) {
  require_one_second_exit_line(
      run_expecting(args, {"--seconds", "1", "--shaders", args.shaders.c_str()}, "runs", 0));
}

// (game_host_options.hpp, --cache and --asset; content.md, invariant 6):
// `--cache <the PW0 cache> --asset model.pw0.mesh --seconds 1` loads and
// draws the converted mesh and exits 0 with the exit line above. Alone
// catches the host no longer reaching the loader, the decoders or the
// textured pair from its options.
void draws_asset_one_second(const Arguments& args) {
  const ChildResult result =
      run_expecting(args,
                    {"--cache", args.cache.c_str(), "--asset", "model.pw0.mesh", "--seconds", "1",
                     "--shaders", args.shaders.c_str()},
                    "draws_asset", 0);
  require_one_second_exit_line(result);
}

// The parse's wiring (game_host_options.hpp, game_host_usage): an unknown
// option exits 2 with the usage line on stderr.
void game_host_unknown_option_exits_2(const Arguments& args) {
  const ChildResult result = run_expecting(args, {"--no-such-option"}, "usage", 2);
  require(!lines_starting_with(result.err, "Usage: game_host").empty(),
          "stderr has no usage line: " + result.transcript);
}

// A shader directory without the artifacts exits 1 with a stderr line naming
// the artifact it could not read, in the directory given.
void game_host_missing_shaders_exits_1(const Arguments& args) {
  const ChildResult result =
      run_expecting(args, {"--seconds", "1", "--shaders", args.shaders.c_str()}, "missing", 1);
  require(std::ranges::any_of(lines_starting_with(result.err, "game_host: "),
                              [&](std::string_view l) { return l.find(args.shaders) != l.npos; }),
          "no stderr line names an artifact in " + args.shaders + ": " + result.transcript);
}

using Scenario = bw::testing::Scenario<Arguments>;
constexpr std::array kScenarios{
    Scenario{"game_host_runs_one_second", &game_host_runs_one_second},
    Scenario{"game_host_unknown_option_exits_2", &game_host_unknown_option_exits_2},
    Scenario{"game_host_missing_shaders_exits_1", &game_host_missing_shaders_exits_1},
    Scenario{"draws_asset_one_second", &draws_asset_one_second},
};

// The one scenario that takes the PW0 cache.
constexpr std::string_view kAssetScenario = "draws_asset_one_second";

} // namespace

// The harness has matched argv[1] to a scenario before it calls the parse,
// so the parse reads the scenario's name from it: the asset run takes
// exactly four arguments with a non-empty cache, every
// other scenario exactly three. Anything else is a usage error (exit 2 and
// the usage line, harness.hpp).
int main(int argc, char** argv) {
  const std::string_view scenario =
      argc >= 2 ? std::string_view(std::span(argv, static_cast<std::size_t>(argc))[1])
                : std::string_view{};
  return bw::testing::run_scenario(
      argc, argv, kScenarios,
      "<game_host> <metal artifact directory> <output directory>, then for "
      "draws_asset_one_second only <PW0 cache>",
      [scenario](std::span<char* const> rest, Arguments& out) {
        if (scenario == kAssetScenario) {
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
