// Integration test: the Metal artifact's code block is a Metal library this
// machine's default device loads, and the shader's entry point is in it
// (docs/architecture/shaders.md, "Targets", Metal row: the same DXIL through
// the Metal Shader Converter, in process; macOS 15, Apple silicon, Metal 3).
// The built shader_compiler runs as a child process; Metal is called from
// Objective-C++ because the converter's runtime and the Metal API are
// Objective-C.
//
// Arguments after the scenario name: as shader_toolchain_tests.cpp.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "bw/shader_compiler/logic.hpp"
#include "shader_tool_process.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using bw::shaders::PermutationId;
using bw::tests::ToolPaths;

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

// UTF-8 text of an NSString, empty for nil.
std::string text_of(NSString* text) {
  const char* utf8 = text != nil ? text.UTF8String : nullptr;
  return utf8 != nullptr ? std::string{utf8} : std::string{};
}

std::string function_names(id<MTLLibrary> library) {
  std::string names;
  for (NSString* name in library.functionNames) {
    names += ' ';
    names += text_of(name);
  }
  return names;
}

// (toolchain.hpp, compile_shader): "The Metal library's function is named after
// the shader's entry point", so the function is
// `main`, a vertex function, in the library of every permutation the build
// set holds (enumerate_permutations).
void metal_library_loads(const ToolPaths& paths) {
  const bw::shaders::ShaderDescription* shader =
      bw::shaders::find_shader(bw::shaders::shader_registry(), "fullscreen_triangle");
  require(shader != nullptr, "the registry holds the fixture shader");
  const std::filesystem::path root = paths.scratch / "metal_library_loads";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  require(device != nil, "this machine has a default Metal device");

  std::vector<const bw::shaders::BlockLayout*> blocks;
  if (shader->draw_block != nullptr) {
    blocks.push_back(shader->draw_block);
  }
  const bw::shaders::LoadExpectations expectations{&bw::shaders::engine_root_layout(), blocks,
                                                   shader->permutation_count};
  std::vector<PermutationId> build_set(shader->permutation_count);
  build_set.resize(bw::shaders::enumerate_permutations(*shader, build_set));
  require(!build_set.empty(), "the fixture has permutations to build");
  for (const PermutationId member : build_set) {
    const std::uint32_t permutation = member.value;
    const std::string what = "permutation " + std::to_string(permutation);
    const int code = bw::tests::run_process(
        bw::tests::compile_command(paths, std::string{shader->name}, permutation, "metal", root));
    require(code == static_cast<int>(bw::shaders::ExitCode::Success),
            what + ": shader_compiler exits Success");
    const std::vector<std::byte> image =
        bw::tests::read_bytes(root / bw::shader_compiler::artifact_relative_path(
                                         bw::shaders::ShaderTarget::Metal, shader->name,
                                         PermutationId{permutation}, shader->stage));
    const bw::shaders::ArtifactLoadResult loaded = bw::shaders::load_artifact(image, expectations);
    require(loaded.ok(), what + ": the artifact loads");
    const std::span<const std::byte> library_bytes = loaded.artifact().code;

    dispatch_data_t data = dispatch_data_create(library_bytes.data(), library_bytes.size(), nullptr,
                                                DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    NSError* error = nil;
    id<MTLLibrary> library = [device newLibraryWithData:data error:&error];
    if (library == nil) {
      std::string message = what;
      message += ": the device does not load the Metal library: ";
      message += error != nil ? text_of(error.localizedDescription) : "no error text";
      throw std::runtime_error(message);
    }
    const std::string entry{shader->entry_point};
    NSString* entry_name = [NSString stringWithUTF8String:entry.c_str()];
    if (entry_name == nil) {
      throw std::runtime_error("the entry point name is not UTF-8");
    }
    id<MTLFunction> function = [library newFunctionWithName:entry_name];
    if (function == nil) {
      std::string message = what;
      message += ": the library has no function named '";
      message += entry;
      message += "'; it has:";
      message += function_names(library);
      throw std::runtime_error(message);
    }
    require(function.functionType == MTLFunctionTypeVertex, what + ": it is a vertex function");
  }
}

} // namespace

int main(int argc, char** argv) {
  if (argc != 8) {
    std::cerr << "Usage: metal_library_tests <scenario> <shader_compiler> <dxc library> "
                 "<converter library> <shader root> <scratch dir> <targets>\n";
    return 2;
  }
  if (std::string_view{argv[1]} != "metal_library_loads") {
    std::cerr << "Unknown scenario\n";
    return 2;
  }
  @autoreleasepool {
    try {
      metal_library_loads(ToolPaths{argv[2], argv[3], argv[4], argv[5], argv[6]});
      return 0;
    } catch (const std::exception& error) {
      std::cerr << "metal_library_loads: " << error.what() << '\n';
      return 1;
    }
  }
}
