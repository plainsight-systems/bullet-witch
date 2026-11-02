#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/shader_compiler/enumeration.hpp"

#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

// Responsibility: the configure-time enumerator. cmake/ShaderArtifacts.cmake
// builds it at configure time, from the engine shader module and
// enumeration.cpp only, and runs it to learn the artifact set before the
// shader toolchain is built; it takes the enumerate grammar of
// compiler_options.hpp and prints exactly what `shader_compiler enumerate`
// prints for the same arguments.
//
// Contract: exit code 0 with the enumerate lines on standard output, or
// UsageError (compiler_options.hpp) with the reason on standard error for a
// command line that is not an enumerate invocation. Reads no environment.
//
// Retrieved rules: I.1 (targets from the command line), E.27 (the named exit
// codes).
namespace {
int run(int argc, char** argv) {
  std::vector<std::string_view> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  const bw::shaders::CommandLineResult parsed = bw::shaders::parse_command_line(args);
  if (!parsed.ok() || parsed.invocation.mode != bw::shaders::ToolMode::Enumerate) {
    std::cerr << "shader_enumerate: expected `enumerate --targets <t>[,<t>...] --output-root "
                 "<dir>`\n"
              << bw::shaders::usage_text();
    return static_cast<int>(bw::shaders::ExitCode::UsageError);
  }
  std::string lines;
  bw::shader_compiler::append_enumeration(bw::shaders::shader_registry(),
                                          parsed.invocation.enumerate.targets.view(), lines);
  std::cout << lines;
  std::cout.flush();
  return static_cast<int>(std::cout ? bw::shaders::ExitCode::Success
                                    : bw::shaders::ExitCode::OutputError);
}
} // namespace

// An exception reaching here is an allocation or library failure inside the
// tool, not a shader problem; it is reported and ends the run with
// OutputError, since no output was produced.
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "shader_enumerate: internal failure: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "shader_enumerate: internal failure\n";
  }
  return static_cast<int>(bw::shaders::ExitCode::OutputError);
}
