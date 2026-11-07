// Integration test: the shader toolchain end to end on this host. The built
// shader_compiler runs as a child process against the fixture shader
// (src/engine/rendering/shaders/hlsl/fullscreen_triangle.hlsl) with the real
// DXC, SPIRV-Tools, SPIRV-Reflect and Metal Shader Converter; the tool
// library's compile_shader runs in process where the test needs the backend
// reflection the artifact does not keep. One scenario per CTest entry; each
// names the statements it asserts.
//
// Arguments after the scenario name: shader_compiler, the DXC library, the
// converter library, the shader root, a scratch directory under the build
// tree, and the comma-separated targets this host builds.
#include "bw/rendering/shaders/artifact.hpp"
#include "bw/rendering/shaders/compiler_options.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"
#include "bw/rendering/shaders/permutation.hpp"
#include "bw/rendering/shaders/root_layout.hpp"
#include "bw/shader_compiler/logic.hpp"
#include "bw/shader_compiler/toolchain.hpp"
#include "shader_tool_process.hpp"

#include "bw/testing/harness.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using bw::shaders::ExitCode;
using bw::shaders::PermutationId;
using bw::shaders::ShaderTarget;
using bw::tests::ToolPaths;

using bw::testing::require;

// The executable's arguments after the scenario name: the tool paths, then
// the comma-separated targets.
struct Arguments {
  ToolPaths paths{};
  std::vector<std::string> targets{};
};

const bw::shaders::ShaderDescription& fixture() {
  const bw::shaders::ShaderDescription* shader =
      bw::shaders::find_shader(bw::shaders::shader_registry(), "fullscreen_triangle");
  require(shader != nullptr, "the registry holds the fixture shader");
  return *shader;
}

std::vector<PermutationId> build_set(const bw::shaders::ShaderDescription& shader) {
  std::vector<PermutationId> ids(shader.permutation_count);
  ids.resize(bw::shaders::enumerate_permutations(shader, ids));
  return ids;
}

std::vector<const bw::shaders::BlockLayout*>
running_blocks(const bw::shaders::ShaderDescription& shader) {
  std::vector<const bw::shaders::BlockLayout*> blocks(shader.static_blocks.begin(),
                                                      shader.static_blocks.end());
  if (shader.draw_block != nullptr) {
    blocks.push_back(shader.draw_block);
  }
  return blocks;
}

std::filesystem::path fresh_directory(const std::filesystem::path& path) {
  std::filesystem::remove_all(path);
  std::filesystem::create_directories(path);
  return path;
}

ShaderTarget target_of(const std::string& name) {
  const std::optional<ShaderTarget> target = bw::shaders::parse_target(name);
  if (!target.has_value()) {
    throw std::runtime_error("target name does not parse: " + name);
  }
  return *target;
}

std::string artifact_path(const std::filesystem::path& root, const std::string& target,
                          const bw::shaders::ShaderDescription& shader, PermutationId permutation) {
  return (root / bw::shader_compiler::artifact_relative_path(target_of(target), shader.name,
                                                             permutation, shader.stage))
      .string();
}

void require_same_reflection(const bw::shaders::Reflection& a, const bw::shaders::Reflection& b,
                             const std::string& what) {
  require(a.block_count == b.block_count, what + ": block count");
  for (std::uint32_t i = 0; i < a.block_count; ++i) {
    require(a.blocks[i].name == b.blocks[i].name &&
                a.blocks[i].layout_hash == b.blocks[i].layout_hash &&
                a.blocks[i].slot == b.blocks[i].slot && a.blocks[i].kind == b.blocks[i].kind,
            what + ": block " + std::to_string(i));
  }
  require(a.vertex_input_count == b.vertex_input_count, what + ": vertex input count");
  for (std::uint32_t i = 0; i < a.vertex_input_count; ++i) {
    require(a.vertex_inputs[i].semantic_name == b.vertex_inputs[i].semantic_name &&
                a.vertex_inputs[i].semantic_index == b.vertex_inputs[i].semantic_index &&
                a.vertex_inputs[i].location == b.vertex_inputs[i].location,
            what + ": vertex input " + std::to_string(i));
  }
  require(a.thread_group_size == b.thread_group_size, what + ": thread group size");
  require(a.root_constant_words == b.root_constant_words, what + ": root constant words");
  require(a.descriptor_arrays == b.descriptor_arrays, what + ": descriptor arrays");
}

// The fixture's reflection on every target: the draw block (registry name
// and hash) as the only block, the Sampler and Texture2D arrays and nothing
// else, no vertex input (SV_VertexID is a system value), no root constants,
// no thread group (a vertex shader).
void require_fixture_reflection(const bw::shaders::Reflection& reflection,
                                const bw::shaders::ShaderDescription& shader,
                                const std::string& what) {
  require(reflection.block_count == 1 &&
              reflection.blocks[0].kind == bw::shaders::BlockSlotKind::Draw &&
              reflection.blocks[0].slot == 0 &&
              reflection.blocks[0].name == shader.draw_block->name &&
              reflection.blocks[0].layout_hash == shader.draw_block->hash,
          what + ": one Draw block, the registry's");
  const std::uint32_t expected_arrays =
      (1U << static_cast<std::uint32_t>(bw::shaders::DescriptorArray::Sampler)) |
      (1U << static_cast<std::uint32_t>(bw::shaders::DescriptorArray::Texture2D));
  require(reflection.descriptor_arrays == expected_arrays,
          what + ": exactly the Sampler and Texture2D arrays");
  require(reflection.vertex_input_count == 0, what + ": no vertex input");
  require(reflection.root_constant_words == 0, what + ": no root constants");
  require(reflection.thread_group_size == std::array<std::uint32_t, 3>{0, 0, 0},
          what + ": no thread group");
}

// toolchain.hpp, compiler identity paragraph: every artifact's
// CompilerIdentity trailer entry starts with DXC's identity; a Metal
// artifact's then names the converter by "uuid:" and 32 lower-case hex
// digits, and nothing else follows.
void require_compiler_identity(const std::string& target, const bw::shaders::Artifact& artifact,
                               const std::string& what) {
  const auto entry =
      bw::shaders::find_trailer_entry(artifact.trailer, bw::shaders::TrailerKey::CompilerIdentity);
  if (!entry.has_value()) {
    throw std::runtime_error(what + ": the artifact has a CompilerIdentity trailer entry");
  }
  std::string identity;
  for (const std::byte b : *entry) {
    identity += static_cast<char>(b);
  }
  require(identity.rfind("dxcompiler ", 0) == 0,
          what + ": identity starts with DXC's: " + identity);
  constexpr std::string_view converter = "; metal-shaderconverter uuid:";
  const std::size_t at = identity.find(converter);
  if (target != "metal") {
    require(at == std::string::npos, what + ": no converter in a " + target + " identity");
    return;
  }
  require(at != std::string::npos, what + ": the Metal identity names the converter by UUID");
  const std::string uuid = identity.substr(at + converter.size());
  require(uuid.size() == 32 && uuid.find_first_not_of("0123456789abcdef") == std::string::npos,
          what + ": 32 lower-case hex digits follow uuid:, got '" + uuid + "'");
}

std::uint32_t read_u32(std::span<const std::byte> bytes, std::size_t at) {
  require(at + 4 <= bytes.size(), "read within the code block");
  std::uint32_t value = 0;
  for (std::size_t i = 0; i < 4; ++i) {
    value |= static_cast<std::uint32_t>(bytes[at + i]) << (8U * i);
  }
  return value;
}

std::string four_cc(std::uint32_t value) {
  std::string text(4, ' ');
  for (std::size_t i = 0; i < 4; ++i) {
    text[i] = static_cast<char>((value >> (8U * i)) & 0xFFU);
  }
  return text;
}

// The code block of each target (compiler_options.hpp, build-time algorithm
// step 4): a DXIL container with its reflection part removed and the root
// signature kept, still carrying a digest; SPIR-V words; a Metal library.
// DXIL container layout: "DXBC", 16-byte digest, u16 major, u16 minor, u32
// size, u32 part count, u32 part offsets; each part is a four-character code
// and a u32 size (DirectX-Headers and DXC's DxilContainer.h).
void require_code_block(const std::string& target, std::span<const std::byte> code,
                        const std::string& what) {
  if (target == "vulkan") {
    require(read_u32(code, 0) == 0x07230203U, what + ": code starts with the SPIR-V magic");
    return;
  }
  if (target == "metal") {
    require(four_cc(read_u32(code, 0)) == "MTLB", what + ": code is a Metal library");
    return;
  }
  require(four_cc(read_u32(code, 0)) == "DXBC", what + ": code is a DXIL container");
  bool digest = false;
  for (std::size_t i = 4; i < 20; ++i) {
    digest = digest || code[i] != std::byte{0};
  }
  require(digest, what + ": the container digest is not zero");
  const std::uint32_t parts = read_u32(code, 28);
  std::set<std::string> names;
  for (std::uint32_t i = 0; i < parts; ++i) {
    names.insert(four_cc(read_u32(code, read_u32(code, 32 + 4 * static_cast<std::size_t>(i)))));
  }
  require(names.count("RTS0") == 1, what + ": the root signature part is kept");
  require(names.count("STAT") == 0, what + ": the reflection part is removed");
  require(names.count("ILDB") == 0, what + ": no debug part without symbols");
}

// The reflected members of `block`, generated padding left out, against the
// C++ layout: same names, same offsets, same order.
void require_offsets_equal_layout(const bw::shader_compiler::BoundBlock& block,
                                  const bw::shaders::BlockLayout& layout, const std::string& what) {
  std::vector<bw::shader_compiler::ReflectedMember> named;
  for (const auto& member : block.members) {
    if (member.name.rfind("bw_pad_", 0) != 0) {
      named.push_back(member);
    }
  }
  require(named.size() == layout.members.size(), what + ": member count");
  for (std::size_t i = 0; i < named.size(); ++i) {
    require(named[i].name == layout.members[i].name, what + ": member name " + named[i].name);
    require(named[i].offset == layout.members[i].offset,
            what + ": offset of " + named[i].name + " is " + std::to_string(named[i].offset) +
                ", C++ offset " + std::to_string(layout.members[i].offset));
  }
}

// docs/architecture/shaders.md invariant 3 and artifact.hpp InputHash (two
// runs give identical bytes); artifact.hpp load_artifact contract (the
// artifacts load against the running declarations); artifact.hpp
// "Reflection ... is identical across the three targets"; parameter_block.hpp
// place_in_hlsl and invariant 4 (the compiled shader's reflected offsets equal
// the C++ offsets, from DXC's reflection of the DXIL and SPIRV-Reflect's of
// the SPIR-V); the fixture's reflection content follows from its registry
// entry (permutation.cpp) and the placement table (root_layout.hpp,
// placement()); the code blocks per compiler_options.hpp build-time
// algorithm step 4; compiler_options.hpp ExitCode for Success and the named
// failures; --symbols per the grammar and toolchain.hpp.
void fixture_on_three_targets(const Arguments& arguments) {
  const ToolPaths& paths = arguments.paths;
  const std::vector<std::string>& targets = arguments.targets;
  const bw::shaders::ShaderDescription& shader = fixture();
  const std::vector<PermutationId> permutations = build_set(shader);
  require(!permutations.empty(), "the fixture has permutations to build");
  const std::filesystem::path root = fresh_directory(paths.scratch / "fixture_on_three_targets");
  const std::array<std::filesystem::path, 2> runs{root / "run1", root / "run2"};

  for (const std::filesystem::path& run : runs) {
    for (const PermutationId permutation : permutations) {
      for (const std::string& target : targets) {
        const int code = bw::tests::run_process(bw::tests::compile_command(
            paths, std::string{shader.name}, permutation.value, target, run));
        require(code == static_cast<int>(ExitCode::Success),
                "shader_compiler exits Success for " + target + " permutation " +
                    std::to_string(permutation.value) + " (exit " + std::to_string(code) + ")");
      }
    }
  }

  const std::vector<const bw::shaders::BlockLayout*> blocks = running_blocks(shader);
  const bw::shaders::LoadExpectations expectations{&bw::shaders::engine_root_layout(), blocks,
                                                   shader.permutation_count};
  std::map<std::string, std::vector<std::byte>> images;
  for (const PermutationId permutation : permutations) {
    std::optional<bw::shaders::Reflection> first;
    for (const std::string& target : targets) {
      const std::string what = target + " permutation " + std::to_string(permutation.value);
      std::vector<std::byte> one =
          bw::tests::read_bytes(artifact_path(runs[0], target, shader, permutation));
      const std::vector<std::byte> two =
          bw::tests::read_bytes(artifact_path(runs[1], target, shader, permutation));
      require(one == two, what + ": two runs give byte-identical artifacts");

      const auto& image = images.emplace(what, std::move(one)).first->second;
      const bw::shaders::ArtifactLoadResult loaded =
          bw::shaders::load_artifact(image, expectations);
      require(loaded.ok(), what + ": loads, error " +
                               std::to_string(static_cast<unsigned>(loaded.error().code)));
      const bw::shaders::Artifact& artifact = loaded.artifact();
      require(artifact.header.shader_name == shader.name &&
                  artifact.header.permutation == permutation &&
                  artifact.header.target == target_of(target) &&
                  artifact.header.stage == shader.stage,
              what + ": header identifies the artifact");
      require_fixture_reflection(artifact.reflection, shader, what);
      require_code_block(target, artifact.code, what);
      require_compiler_identity(target, artifact, what);
      if (!first.has_value()) {
        first = artifact.reflection;
      } else {
        require_same_reflection(*first, artifact.reflection, what + " against " + targets.front());
      }
    }
  }

  require(shader.draw_block != nullptr, "the fixture declares a draw block");
  for (const ShaderTarget target : {ShaderTarget::Direct3D12, ShaderTarget::Vulkan}) {
    for (const PermutationId permutation : permutations) {
      const std::string what = std::string{bw::shaders::target_name(target)} + " permutation " +
                               std::to_string(permutation.value);
      const bw::shader_compiler::CompileProduct product = bw::shader_compiler::compile_shader(
          bw::shader_compiler::CompileRequest{.shader = &shader,
                                              .shader_root = paths.shader_root,
                                              .dxc_library = paths.dxc_library,
                                              .converter_library = {},
                                              .permutation = permutation,
                                              .target = target,
                                              .emit_symbols = false});
      require(product.status == ExitCode::Success,
              what + ": compiles in process: " + product.diagnostics);
      const auto draw =
          std::find_if(product.reflection.blocks.begin(), product.reflection.blocks.end(),
                       [](const bw::shader_compiler::BoundBlock& block) {
                         return block.binding.kind == bw::shader_compiler::BlockBindingKind::Draw;
                       });
      require(draw != product.reflection.blocks.end(), what + ": the draw block is reflected");
      require_offsets_equal_layout(*draw, *shader.draw_block, what);
    }
  }

  // Named failures (compiler_options.hpp ExitCode; toolchain.hpp status
  // table): a DXC library that does not load, a shader the registry does not
  // hold, a permutation outside the build set.
  const std::filesystem::path failures = root / "failures";
  std::vector<std::string> bad_library =
      bw::tests::compile_command(paths, std::string{shader.name}, 0, "d3d12", failures);
  bad_library.back() = (failures / "no_such_libdxcompiler").string();
  require(bw::tests::run_process(bad_library) == static_cast<int>(ExitCode::ToolchainUnavailable),
          "a DXC library that does not load exits ToolchainUnavailable");
  require(bw::tests::run_process(
              bw::tests::compile_command(paths, "no_such_shader", 0, "d3d12", failures)) ==
              static_cast<int>(ExitCode::ContractViolation),
          "an unknown shader exits ContractViolation");
  require(bw::tests::run_process(bw::tests::compile_command(
              paths, std::string{shader.name}, shader.permutation_count, "d3d12", failures)) ==
              static_cast<int>(ExitCode::ContractViolation),
          "a permutation at the count exits ContractViolation");
  // (toolchain.hpp, UsageError): an empty entry in the root list.
  const std::string empty_entry = paths.shader_root + ",," + paths.shader_root;
  const bw::shader_compiler::CompileProduct refused =
      bw::shader_compiler::compile_shader(bw::shader_compiler::CompileRequest{
          .shader = &shader, .shader_root = empty_entry, .dxc_library = paths.dxc_library});
  require(refused.status == ExitCode::UsageError &&
              refused.diagnostics.find("--shader-root") != std::string::npos,
          "an empty source root is UsageError naming --shader-root: " + refused.diagnostics);

  // Symbols (compiler_options.hpp grammar, --symbols; toolchain.hpp
  // CompileProduct::symbols): one non-empty file per artifact, named by the
  // artifact's own input hash. The arguments differ with symbols, so the
  // artifact differs from the run without them; only the name and presence
  // of the symbol file are asserted.
  const std::filesystem::path symbol_run = root / "symbols";
  const std::filesystem::path symbol_dir = symbol_run / "files";
  for (const std::string& target : targets) {
    std::vector<std::string> command =
        bw::tests::compile_command(paths, std::string{shader.name}, 0, target, symbol_run);
    command.emplace_back("--symbols");
    command.push_back(symbol_dir.string());
    require(bw::tests::run_process(command) == static_cast<int>(ExitCode::Success),
            target + ": a --symbols run exits Success");
    const std::vector<std::byte> image =
        bw::tests::read_bytes(artifact_path(symbol_run, target, shader, PermutationId{0}));
    const bw::shaders::ArtifactLoadResult loaded = bw::shaders::load_artifact(image, expectations);
    require(loaded.ok(), target + ": the --symbols artifact loads");
    const std::filesystem::path symbols =
        symbol_dir / bw::shader_compiler::symbol_file_name(loaded.artifact().header.input_hash,
                                                           target_of(target));
    require(std::filesystem::is_regular_file(symbols) && std::filesystem::file_size(symbols) > 0,
            target + ": the symbol file is named by the artifact's input hash and is not empty");
  }
}

// The Makefile text as a target and its dependencies; "\\\n" continues a
// line. Paths under the build tree contain no spaces, so whitespace splits.
std::pair<std::string, std::vector<std::string>> parse_depfile(std::string text) {
  for (std::size_t at = text.find("\\\n"); at != std::string::npos; at = text.find("\\\n")) {
    text.replace(at, 2, " ");
  }
  std::istringstream stream(text);
  std::string rule_target;
  stream >> rule_target;
  require(!rule_target.empty() && rule_target.back() == ':', "depfile starts with '<target>:'");
  rule_target.pop_back();
  std::vector<std::string> dependencies;
  for (std::string token; stream >> token;) {
    dependencies.push_back(token);
  }
  return {rule_target, dependencies};
}

// compiler_options.hpp build-time algorithm step 5 (the dependency file
// lists the main source and every include opened) and logic.hpp
// append_depfile (main source first, includes in the order given, which
// toolchain.hpp CompileProduct::includes states is first-inclusion order;
// the generated include is never listed; the rule target is the artifact path
// as given).
void depfile_names_every_include(const Arguments& arguments) {
  const ToolPaths& paths = arguments.paths;
  const std::vector<std::string>& targets = arguments.targets;
  const bw::shaders::ShaderDescription& shader = fixture();
  const std::filesystem::path root = fresh_directory(paths.scratch / "depfile_names_every_include");
  // Main source first, then includes in first-inclusion order.
  const std::vector<std::string> expected{
      bw::shader_compiler::join_path(paths.shader_root, shader.source_path),
      bw::shader_compiler::join_path(paths.shader_root, "bw/bindless.hlsli")};
  for (const std::string& target : targets) {
    const int code = bw::tests::run_process(
        bw::tests::compile_command(paths, std::string{shader.name}, 0, target, root));
    require(code == static_cast<int>(ExitCode::Success),
            target + ": shader_compiler exits Success");
    const auto [rule_target, dependencies] =
        parse_depfile(bw::tests::read_text(root / (target + ".0.d")));
    require(rule_target == bw::shader_compiler::join_path(
                               root.string(),
                               bw::shader_compiler::artifact_relative_path(
                                   target_of(target), shader.name, PermutationId{0}, shader.stage)),
            target + ": the rule target is the artifact path");
    require(dependencies == expected,
            target + ": dependencies are the main source, then bw/bindless.hlsli");
    for (const std::string& dependency : dependencies) {
      require(dependency.find("generated") == std::string::npos,
              target + ": no generated include is listed");
    }
  }

  // (toolchain.hpp, CompileRequest::shader_root and
  // CompileProduct::include_paths): with two roots, an include both hold is
  // read from the first and one only the second holds from the second, each
  // listed joined to the root that held it. The second root's copy of the
  // shared include does not compile.
  const std::filesystem::path first = fresh_directory(root / "first" / "bw").parent_path();
  const std::filesystem::path second = fresh_directory(root / "second" / "bw").parent_path();
  std::ofstream(first / shader.source_path)
      << "#include \"bw/probe.hlsli\"\n"
      << bw::tests::read_text(std::filesystem::path(paths.shader_root) / shader.source_path);
  std::ofstream(first / "bw/probe.hlsli") << "// the first root's copy\n";
  std::ofstream(second / "bw/probe.hlsli") << "#error \"the second root's copy was read\"\n";
  std::filesystem::copy_file(std::filesystem::path(paths.shader_root) / "bw/bindless.hlsli",
                             second / "bw/bindless.hlsli");
  const std::string roots = first.string() + "," + second.string();
  const bw::shader_compiler::CompileProduct product =
      bw::shader_compiler::compile_shader(bw::shader_compiler::CompileRequest{
          .shader = &shader, .shader_root = roots, .dxc_library = paths.dxc_library});
  require(product.status == ExitCode::Success,
          "the shared include is the first root's: " + product.diagnostics);
  require(product.include_paths ==
              std::vector<std::string>{
                  bw::shader_compiler::join_path(first.string(), "bw/probe.hlsli"),
                  bw::shader_compiler::join_path(second.string(), "bw/bindless.hlsli")},
          "each include is joined to the root that held it");
}

std::vector<std::string> split_targets(std::string_view csv) {
  std::vector<std::string> targets;
  std::size_t begin = 0;
  while (begin <= csv.size()) {
    const std::size_t end = std::min(csv.find(',', begin), csv.size());
    if (end > begin) {
      targets.emplace_back(csv.substr(begin, end - begin));
    }
    begin = end + 1;
  }
  return targets;
}

using Scenario = bw::testing::Scenario<Arguments>;

constexpr std::array scenarios{
    Scenario{"fixture_on_three_targets", fixture_on_three_targets},
    Scenario{"depfile_names_every_include", depfile_names_every_include},
};

} // namespace

int main(int argc, char** argv) {
  return bw::testing::run_scenario(
      argc, argv, scenarios,
      "<shader_compiler> <dxc library> <converter library> <shader root> <scratch dir> <targets>",
      [](std::span<char* const> rest, Arguments& arguments) {
        if (rest.size() != 6) {
          return false;
        }
        arguments.paths = ToolPaths{rest[0], rest[1], rest[2], rest[3], rest[4]};
        arguments.targets = split_targets(rest[5]);
        return true;
      });
}
