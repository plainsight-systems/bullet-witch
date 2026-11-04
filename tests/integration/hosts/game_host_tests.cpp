// Integration test: the game_host executable's exit contract, probed by
// running it as a child process (the grammar is game_host_options.hpp's unit
// test). The artifact directory is the build's for the run, a missing one for
// the missing-artifact probe; both open a window and need a GUI session.
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
#include <vector>

extern char** environ;

namespace {

using bw::testing::require;

struct Arguments {
  std::string game_host;
  std::string shaders;
  std::string output;
};

struct ChildResult {
  std::string out, err, transcript;
};

std::string read_file(const std::string& path) {
  std::ifstream stream(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Runs game_host with `options`, stdout and stderr in `<output>/<name>.stdout`
// and `.stderr`, and reads both; fails unless it exits normally with `expected`.
ChildResult run_expecting(const Arguments& args, std::vector<const char*> options,
                          const std::string& name, int expected) {
  const std::string path = args.output + "/" + name;
  posix_spawn_file_actions_t actions{};
  require(posix_spawn_file_actions_init(&actions) == 0, "posix_spawn_file_actions_init failed");
  posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, (path + ".stdout").c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644);
  posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, (path + ".stderr").c_str(),
                                   O_WRONLY | O_CREAT | O_TRUNC, 0644);
  options.insert(options.begin(), args.game_host.c_str());
  options.push_back(nullptr);
  pid_t child = 0;
  const int spawned = posix_spawn(&child, options[0], &actions, nullptr,
                                  const_cast<char* const*>(options.data()), environ);
  posix_spawn_file_actions_destroy(&actions);
  require(spawned == 0,
          "posix_spawn of " + args.game_host + " failed: errno " + std::to_string(spawned));
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    require(errno == EINTR, "waitpid failed for " + args.game_host);
  }
  require(WIFEXITED(status), args.game_host + " did not exit normally");
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

// `--seconds 1` exits 0 with exactly one stdout line `frames=N elapsed=S
// waited=W`, W the end_frame calls that blocked: at least one frame at any
// display rate, at least the second given, the blocked calls among the frames.
void game_host_runs_one_second(const Arguments& args) {
  const ChildResult result =
      run_expecting(args, {"--seconds", "1", "--shaders", args.shaders.c_str()}, "runs", 0);
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

// The parse's wiring (game_host_options.hpp, kGameHostUsage): an unknown
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
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(
      argc, argv, kScenarios, "<game_host> <metal artifact directory> <output directory>",
      [](std::span<char* const> rest, Arguments& out) {
        out = rest.size() == 3U ? Arguments{rest[0], rest[1], rest[2]} : Arguments{};
        return rest.size() == 3U;
      });
}
