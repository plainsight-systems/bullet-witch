#ifndef BW_TESTS_SHADER_TOOL_PROCESS_HPP
#define BW_TESTS_SHADER_TOOL_PROCESS_HPP
// Test helper shared by the shader toolchain integration scenarios in this
// directory: runs the built shader_compiler as a child process and reads the
// files it writes. Not a test double: it launches the real executable.
//
// The child gets an empty environment. compiler_options.hpp states that the
// tool reads nothing from the environment, so running it with none is both
// the honest launch and a probe of that statement.
//
// Core guidelines read: E.27 (the result carries the exit status or the
// launch failure by name), I.1 (every path is an argument). Performance
// guidelines read: none applies to a test launcher.
#include <spawn.h>
#include <sys/wait.h>

#include <cerrno>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace bw::tests {

// The child's exit code, or throws when it could not be started or did not
// exit normally.
inline int run_process(const std::vector<std::string>& argv) {
  std::vector<char*> arguments;
  arguments.reserve(argv.size() + 1);
  for (const std::string& argument : argv) {
    arguments.push_back(const_cast<char*>(argument.c_str()));
  }
  arguments.push_back(nullptr);
  char* environment[] = {nullptr};
  pid_t child = 0;
  const int spawned =
      posix_spawn(&child, arguments[0], nullptr, nullptr, arguments.data(), environment);
  if (spawned != 0) {
    throw std::runtime_error("posix_spawn of " + argv.front() + " failed: errno " +
                             std::to_string(spawned));
  }
  int status = 0;
  while (waitpid(child, &status, 0) < 0) {
    if (errno != EINTR) {
      throw std::runtime_error("waitpid failed for " + argv.front());
    }
  }
  if (!WIFEXITED(status)) {
    throw std::runtime_error(argv.front() + " did not exit normally");
  }
  return WEXITSTATUS(status);
}

inline std::vector<std::byte> read_bytes(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot read " + path.string());
  }
  const std::vector<char> chars{std::istreambuf_iterator<char>(stream),
                                std::istreambuf_iterator<char>()};
  std::vector<std::byte> bytes(chars.size());
  for (std::size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(chars[i]);
  }
  return bytes;
}

inline std::string read_text(const std::filesystem::path& path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    throw std::runtime_error("cannot read " + path.string());
  }
  return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// Paths every scenario receives from CMake, in this order after the
// scenario name.
struct ToolPaths {
  std::string shader_compiler{};
  std::string dxc_library{};
  std::string converter_library{};
  std::string shader_root{};
  std::filesystem::path scratch{};
};

// The compile command for one artifact, written under `output_root`, with
// its depfile beside it as "<target>.<permutation>.d".
inline std::vector<std::string> compile_command(const ToolPaths& paths, const std::string& shader,
                                                unsigned permutation, const std::string& target,
                                                const std::filesystem::path& output_root) {
  std::vector<std::string> command{
      paths.shader_compiler,
      "compile",
      "--shader",
      shader,
      "--permutation",
      std::to_string(permutation),
      "--target",
      target,
      "--shader-root",
      paths.shader_root,
      "--output-root",
      output_root.string(),
      "--depfile",
      (output_root / (target + "." + std::to_string(permutation) + ".d")).string(),
      "--dxc-library",
      paths.dxc_library};
  if (target == "metal") {
    command.emplace_back("--converter-library");
    command.push_back(paths.converter_library);
  }
  return command;
}

} // namespace bw::tests
#endif
