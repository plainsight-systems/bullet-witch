#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/shader_compiler/enumeration.hpp"
#include "bw/shader_compiler/logic.hpp"
#include "bw/shader_compiler/toolchain.hpp"

#include <cstddef>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Responsibility: the shader_compiler executable. Parses the command line
// (compiler_options.hpp), prints the enumeration or runs compile_shader
// (toolchain.hpp), and writes the three outputs of a compile: the artifact,
// the dependency file and, with --symbols, the symbol file.
//
// Contract: the process exit code is compiler_options.hpp's ExitCode: a
// command line error is UsageError with the usage text on standard error; a
// compile failure is the status compile_shader returned, with its
// diagnostics on standard error; a file that cannot be written is
// OutputError. Standard output carries the enumeration and nothing else.
// Nothing is read from the environment; every path is an option. Each file
// is written to a temporary name beside it and renamed into place, so a
// failed or interrupted run never leaves a file that looks complete.
//
// Retrieved rules: E.27 (exit codes are the named enum), I.1 (every input an
// option), SL.io.2 (a stream failure is checked before the output is
// declared written). Performance: none applies to a one-shot tool beyond
// COPY.1.
namespace {

using bw::shaders::ExitCode;

int exit_with(ExitCode code) { return static_cast<int>(code); }

std::string_view command_line_error_name(bw::shaders::CommandLineError error) {
  using bw::shaders::CommandLineError;
  switch (error) {
  case CommandLineError::None:
    return "None";
  case CommandLineError::MissingMode:
    return "MissingMode";
  case CommandLineError::UnknownMode:
    return "UnknownMode";
  case CommandLineError::UnknownOption:
    return "UnknownOption";
  case CommandLineError::MissingValue:
    return "MissingValue";
  case CommandLineError::RepeatedOption:
    return "RepeatedOption";
  case CommandLineError::MissingRequiredOption:
    return "MissingRequiredOption";
  case CommandLineError::InvalidTarget:
    return "InvalidTarget";
  case CommandLineError::InvalidPermutation:
    return "InvalidPermutation";
  case CommandLineError::ConverterRequiredForMetal:
    return "ConverterRequiredForMetal";
  case CommandLineError::UnexpectedArgument:
    return "UnexpectedArgument";
  }
  return "unknown";
}

std::span<const char> as_chars(const std::vector<std::byte>& bytes) {
  return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}

// Writes `bytes` to `path` through a temporary file renamed into place;
// creates the parent directory. Returns false with `error` set on failure.
bool write_file(const std::filesystem::path& path, std::span<const char> bytes,
                std::string& error) {
  std::error_code code;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), code);
    if (code) {
      error = "cannot create directory '" + path.parent_path().string() + "': " + code.message();
      return false;
    }
  }
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
      error = "cannot open '" + temporary.string() + "' for writing";
      return false;
    }
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) {
      error = "cannot write '" + temporary.string() + "'";
      return false;
    }
  }
  std::filesystem::rename(temporary, path, code);
  if (code) {
    error =
        "cannot rename '" + temporary.string() + "' to '" + path.string() + "': " + code.message();
    return false;
  }
  return true;
}

int run_compile(const bw::shaders::CompileOptions& options) {
  const bw::shaders::ShaderDescription* shader =
      bw::shaders::find_shader(bw::shaders::shader_registry(), options.shader);
  if (shader == nullptr) {
    std::cerr << "shader_compiler: no shader named '" << options.shader << "' in the registry\n";
    return exit_with(ExitCode::ContractViolation);
  }
  const bw::shader_compiler::CompileProduct product = bw::shader_compiler::compile_shader(
      bw::shader_compiler::CompileRequest{.shader = shader,
                                          .shader_root = options.shader_root,
                                          .dxc_library = options.dxc_library,
                                          .converter_library = options.converter_library,
                                          .permutation = options.permutation,
                                          .target = options.target,
                                          .emit_symbols = !options.symbols_dir.empty()});
  if (product.status != ExitCode::Success) {
    std::cerr << "shader_compiler: " << shader->name << " permutation " << options.permutation.value
              << " " << bw::shaders::target_name(options.target) << ":\n"
              << product.diagnostics << '\n';
    return exit_with(product.status);
  }

  const std::string artifact_path = bw::shader_compiler::join_path(
      options.output_root, bw::shader_compiler::artifact_relative_path(
                               options.target, shader->name, options.permutation, shader->stage));
  std::string error;
  if (!product.symbols.empty()) {
    const std::string symbol_path = bw::shader_compiler::join_path(
        options.symbols_dir,
        bw::shader_compiler::symbol_file_name(product.input_hash, options.target));
    if (!write_file(symbol_path, as_chars(product.symbols), error)) {
      std::cerr << "shader_compiler: " << error << '\n';
      return exit_with(ExitCode::OutputError);
    }
  }
  if (!write_file(artifact_path, as_chars(product.artifact), error)) {
    std::cerr << "shader_compiler: " << error << '\n';
    return exit_with(ExitCode::OutputError);
  }
  // The depfile last: it is what tells the build the artifact is current.
  std::string depfile;
  // Each dependency is written as its own root joined to its relative path:
  // the main source under the first root, each include under the root that
  // held it (toolchain.hpp, include_paths). An empty root makes
  // append_depfile write the paths as given.
  const std::string_view first_root = options.shader_root.substr(0, options.shader_root.find(','));
  bw::shader_compiler::append_depfile(
      artifact_path, {}, bw::shader_compiler::join_path(first_root, shader->source_path),
      product.include_paths, depfile);
  if (!write_file(std::string{options.depfile}, depfile, error)) {
    std::cerr << "shader_compiler: " << error << '\n';
    return exit_with(ExitCode::OutputError);
  }
  return exit_with(ExitCode::Success);
}

} // namespace

namespace {
int run(int argc, char** argv) {
  std::vector<std::string_view> args;
  for (int i = 1; i < argc; ++i) {
    args.emplace_back(argv[i]);
  }
  const bw::shaders::CommandLineResult parsed = bw::shaders::parse_command_line(args);
  if (!parsed.ok()) {
    std::cerr << "shader_compiler: " << command_line_error_name(parsed.error) << ": '"
              << parsed.offending << "'\n"
              << bw::shaders::usage_text();
    return exit_with(ExitCode::UsageError);
  }
  if (parsed.invocation.mode == bw::shaders::ToolMode::Enumerate) {
    std::string lines;
    bw::shader_compiler::append_enumeration(bw::shaders::shader_registry(),
                                            parsed.invocation.enumerate.targets.view(), lines);
    std::cout << lines;
    std::cout.flush();
    return exit_with(std::cout ? ExitCode::Success : ExitCode::OutputError);
  }
  return run_compile(parsed.invocation.compile);
}
} // namespace

// An exception reaching here is an allocation or library failure inside the
// tool, not a shader problem; it is reported and ends the run with
// OutputError, since no output was produced.
int main(int argc, char** argv) {
  try {
    return run(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "shader_compiler: internal failure: " << error.what() << '\n';
  } catch (...) {
    std::cerr << "shader_compiler: internal failure\n";
  }
  return static_cast<int>(bw::shaders::ExitCode::OutputError);
}
