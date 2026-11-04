// Unit test: public contracts of bw/rendering/shaders/compiler_options.hpp,
// the shader_compiler option parser and argument set, one scenario per CTest
// entry. Each scenario names the header declaration it asserts.
#include "bw/rendering/shaders/compiler_options.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {
using bw::shaders::CommandLineError;
using bw::shaders::CommandLineResult;
using bw::shaders::Define;
using bw::shaders::PermutationId;
using bw::shaders::ShaderStage;
using bw::shaders::ShaderTarget;
using bw::shaders::ToolMode;

using bw::testing::require;

CommandLineResult parse(std::initializer_list<std::string_view> args) {
  return bw::shaders::parse_command_line(std::span{args.begin(), args.size()});
}

bool points_into(std::string_view view, std::string_view arg) {
  return view.data() == arg.data() && view.size() == arg.size();
}

// (compiler_options.hpp, parse_command_line), grammar as
// (compiler_options.hpp, usage_text) describes it: both modes parse, views
// point into the argument strings, optional options may be absent, and a
// --converter-library given for a non-Metal target is parsed and kept in the
// invocation ((compiler_options.hpp, CommandLineResult); the tool ignores it).
void parse_enumerate_and_compile() {
  const std::array<std::string_view, 5> enumerate{"enumerate", "--targets", "metal,d3d12",
                                                  "--output-root", "build/shaders"};
  const CommandLineResult listed = bw::shaders::parse_command_line(enumerate);
  require(listed.ok() && listed.invocation.mode == ToolMode::Enumerate, "enumerate parses");
  const auto targets = listed.invocation.enumerate.targets.view();
  require(targets.size() == 2U && targets[0] == ShaderTarget::Metal &&
              targets[1] == ShaderTarget::Direct3D12,
          "targets in the order given");
  require(points_into(listed.invocation.enumerate.output_root, enumerate[4]),
          "output root views the argument");

  const std::array<std::string_view, 19> compile{"compile",
                                                 "--target",
                                                 "metal",
                                                 "--shader",
                                                 "some_shader",
                                                 "--permutation",
                                                 "1",
                                                 "--shader-root",
                                                 "src/shaders",
                                                 "--output-root",
                                                 "out",
                                                 "--depfile",
                                                 "out/x.d",
                                                 "--dxc-library",
                                                 "lib/libdxcompiler",
                                                 "--converter-library",
                                                 "lib/libmetalirconverter",
                                                 "--symbols",
                                                 "out/symbols"};
  const CommandLineResult built = bw::shaders::parse_command_line(compile);
  require(built.ok() && built.invocation.mode == ToolMode::Compile, "compile parses");
  const bw::shaders::CompileOptions& options = built.invocation.compile;
  require(options.target == ShaderTarget::Metal, "target");
  require(options.permutation == PermutationId{1}, "permutation");
  require(points_into(options.shader, compile[4]) && points_into(options.shader_root, compile[8]) &&
              points_into(options.output_root, compile[10]) &&
              points_into(options.depfile, compile[12]) &&
              points_into(options.dxc_library, compile[14]) &&
              points_into(options.converter_library, compile[16]) &&
              points_into(options.symbols_dir, compile[18]),
          "every path views its argument");
  const CommandLineResult no_symbols =
      parse({"compile", "--shader", "s", "--permutation", "0", "--target", "vulkan",
             "--shader-root", "r", "--output-root", "o", "--depfile", "d", "--dxc-library", "x"});
  require(no_symbols.ok() && no_symbols.invocation.compile.symbols_dir.empty() &&
              no_symbols.invocation.compile.converter_library.empty() &&
              no_symbols.invocation.compile.target == ShaderTarget::Vulkan,
          "optional options may be absent for a non-Metal target");

  const std::array<std::string_view, 17> d3d12_with_converter{
      "compile", "--shader",      "s", "--permutation",       "0",       "--target",
      "d3d12",   "--shader-root", "r", "--output-root",       "o",       "--depfile",
      "d",       "--dxc-library", "x", "--converter-library", "lib/conv"};
  const CommandLineResult retained = bw::shaders::parse_command_line(d3d12_with_converter);
  require(retained.ok() && retained.invocation.compile.target == ShaderTarget::Direct3D12,
          "a non-Metal compile with --converter-library parses");
  require(points_into(retained.invocation.compile.converter_library, d3d12_with_converter[16]),
          "the converter library is retained");
  require(!bw::shaders::usage_text().empty(), "usage text exists");
}

// The error and the offending argument, by identity: offending is a view of
// args[index] itself.
template <std::size_t N>
void expect_view(const std::array<std::string_view, N>& args, CommandLineError error,
                 std::size_t index, std::string_view what) {
  const CommandLineResult result = bw::shaders::parse_command_line(args);
  require(!result.ok() && result.error == error && points_into(result.offending, args[index]),
          what);
}

// The error and an offending text that is not a view into the arguments.
template <std::size_t N>
void expect_static(const std::array<std::string_view, N>& args, CommandLineError error,
                   std::string_view text, std::string_view what) {
  const CommandLineResult result = bw::shaders::parse_command_line(args);
  require(!result.ok() && result.error == error && result.offending == text, what);
  for (const std::string_view arg : args) {
    require(result.offending.data() != arg.data(), "offending is static text, not an argument");
  }
}

// (compiler_options.hpp, parse_command_line) (errors in argument order,
// after-the-walk errors last) and (compiler_options.hpp, CommandLineResult)
// (offending per error: the argument itself for
// UnknownMode, UnknownOption, RepeatedOption, UnexpectedArgument; the option
// argument for MissingValue, InvalidTarget, InvalidPermutation; empty for
// MissingMode; the option's name as static text for MissingRequiredOption and
// ConverterRequiredForMetal). Every input carries one fault, except the last,
// which carries two and shows the stated precedence.
void parse_rejections() {
  {
    const CommandLineResult result = bw::shaders::parse_command_line({});
    require(!result.ok() && result.error == CommandLineError::MissingMode &&
                result.offending.empty(),
            "MissingMode, offending empty");
  }
  expect_view(std::array<std::string_view, 1>{"build"}, CommandLineError::UnknownMode, 0,
              "UnknownMode");
  expect_view(std::array<std::string_view, 7>{"enumerate", "--targets", "d3d12", "--output-root",
                                              "o", "--shader", "s"},
              CommandLineError::UnknownOption, 5, "UnknownOption");
  expect_view(std::array<std::string_view, 4>{"enumerate", "--output-root", "o", "--targets"},
              CommandLineError::MissingValue, 3, "MissingValue: absent");
  expect_view(std::array<std::string_view, 4>{"enumerate", "--targets", "--output-root", "o"},
              CommandLineError::MissingValue, 1, "MissingValue: a value starting with --");
  expect_view(std::array<std::string_view, 7>{"enumerate", "--targets", "d3d12", "--output-root",
                                              "o", "--targets", "metal"},
              CommandLineError::RepeatedOption, 5, "RepeatedOption");
  expect_static(std::array<std::string_view, 3>{"enumerate", "--targets", "d3d12"},
                CommandLineError::MissingRequiredOption, "--output-root", "MissingRequiredOption");
  expect_view(
      std::array<std::string_view, 5>{"enumerate", "--targets", "d3d12,gl", "--output-root", "o"},
      CommandLineError::InvalidTarget, 1, "InvalidTarget: unknown name");
  expect_view(std::array<std::string_view, 5>{"enumerate", "--targets", "d3d12,metal,d3d12",
                                              "--output-root", "o"},
              CommandLineError::InvalidTarget, 1, "InvalidTarget: repeated target");
  expect_view(std::array<std::string_view, 5>{"enumerate", "--targets", "d3d12,vulkan,metal,vulkan",
                                              "--output-root", "o"},
              CommandLineError::InvalidTarget, 1, "InvalidTarget: a fourth target");
  expect_view(std::array<std::string_view, 15>{"compile", "--shader", "s", "--permutation", "0",
                                               "--target", "gl", "--shader-root", "r",
                                               "--output-root", "o", "--depfile", "d",
                                               "--dxc-library", "x"},
              CommandLineError::InvalidTarget, 5, "InvalidTarget: compile target");
  expect_view(std::array<std::string_view, 15>{"compile", "--shader", "s", "--permutation", "-1",
                                               "--target", "d3d12", "--shader-root", "r",
                                               "--output-root", "o", "--depfile", "d",
                                               "--dxc-library", "x"},
              CommandLineError::InvalidPermutation, 3, "InvalidPermutation");
  expect_static(std::array<std::string_view, 15>{"compile", "--shader", "s", "--permutation", "0",
                                                 "--target", "metal", "--shader-root", "r",
                                                 "--output-root", "o", "--depfile", "d",
                                                 "--dxc-library", "x"},
                CommandLineError::ConverterRequiredForMetal, "--converter-library",
                "ConverterRequiredForMetal");
  expect_view(std::array<std::string_view, 6>{"enumerate", "stray", "--targets", "d3d12",
                                              "--output-root", "o"},
              CommandLineError::UnexpectedArgument, 1, "UnexpectedArgument");
  // Two faults: an unparsable permutation and no --dxc-library. The error
  // found in argument order is reported; the missing option comes last.
  expect_view(std::array<std::string_view, 13>{"compile", "--shader", "s", "--permutation", "x",
                                               "--target", "d3d12", "--shader-root", "r",
                                               "--output-root", "o", "--depfile", "d"},
              CommandLineError::InvalidPermutation, 3,
              "precedence: argument-order error before missing required option");
}

bool contains_path(const std::vector<std::string>& args) {
  for (const std::string& arg : args) {
    if (arg.find('/') != std::string::npos || arg.find('\\') != std::string::npos) {
      return true;
    }
  }
  return false;
}

std::vector<std::string> with(std::vector<std::string> head, std::vector<std::string> tail) {
  head.insert(head.end(), tail.begin(), tail.end());
  return head;
}

// (compiler_options.hpp, append_compiler_arguments): the fixed list in the fixed order:
// symbol flags after -enable-16bit-types and before -disable-lifetime-markers
// (-Zss -Zi -Qstrip_debug on DXIL targets, -Zi on Vulkan), defines in domain
// order, the root signature pair on DXIL targets only (ignored on Vulkan),
// Vulkan flags with -fspv-debug=line, then -E and -T last; appended; no host
// path.
void arguments_fixed_order_no_paths() {
  const std::array<Define, 2> defines{Define{"BW_FLIP_Y", 1}, Define{"TEST_TAPS", -1}};
  std::string root_signature;
  bw::shaders::append_root_signature_text(bw::shaders::engine_root_layout(), root_signature);
  const std::vector<std::string> fixed{"-HV", "2021", "-Zpr", "-O3", "-WX", "-enable-16bit-types"};
  const std::vector<std::string> root_pair{"-D", "BW_ROOT_SIGNATURE=" + root_signature,
                                           "-rootsig-define", "BW_ROOT_SIGNATURE"};

  std::vector<std::string> d3d12;
  bw::shaders::append_compiler_arguments(ShaderTarget::Direct3D12, ShaderStage::Vertex, "main",
                                         defines, root_signature, false, d3d12);
  std::vector<std::string> expected_d3d12 =
      with(fixed, {"-disable-lifetime-markers", "-D", "BW_TARGET_D3D12=1", "-D", "BW_FLIP_Y=1",
                   "-D", "TEST_TAPS=-1"});
  expected_d3d12 = with(with(expected_d3d12, root_pair), {"-E", "main", "-T", "vs_6_4"});
  require(d3d12 == expected_d3d12, "Direct3D 12 arguments, no symbols");

  std::vector<std::string> vulkan{"kept"};
  bw::shaders::append_compiler_arguments(ShaderTarget::Vulkan, ShaderStage::Pixel, "ps_main",
                                         defines, root_signature, true, vulkan);
  const std::vector<std::string> expected_vulkan =
      with(with({"kept"}, fixed),
           {"-Zi", "-disable-lifetime-markers", "-D", "BW_TARGET_VULKAN=1", "-D", "BW_FLIP_Y=1",
            "-D", "TEST_TAPS=-1", "-spirv", "-fspv-target-env=vulkan1.3", "-fvk-use-dx-layout",
            "-fspv-reflect", "-fspv-debug=line", "-E", "ps_main", "-T", "ps_6_4"});
  require(vulkan == expected_vulkan, "Vulkan arguments, appended, with symbols, no root signature");

  std::vector<std::string> metal;
  bw::shaders::append_compiler_arguments(ShaderTarget::Metal, ShaderStage::Compute, "cs_main", {},
                                         root_signature, true, metal);
  std::vector<std::string> expected_metal =
      with(fixed, {"-Zss", "-Zi", "-Qstrip_debug", "-disable-lifetime-markers", "-D",
                   "BW_TARGET_METAL=1"});
  expected_metal = with(with(expected_metal, root_pair), {"-E", "cs_main", "-T", "cs_6_4"});
  require(metal == expected_metal, "Metal arguments with symbols");
  require(!contains_path(d3d12) && !contains_path(vulkan) && !contains_path(metal),
          "no argument contains a path");
}

// (compiler_options.hpp, append_enumerate_line),
// (compiler_options.hpp, target_name), (compiler_options.hpp, parse_target) and
// (compiler_options.hpp, stage_profile): the enumerate line for a description
// built here, target names round-trip, and the 6.4 profiles.
void enumerate_line_format() {
  bw::shaders::ShaderDescription shader{};
  shader.name = "test_shader";
  shader.stage = ShaderStage::Pixel;
  shader.permutation_count = 8;
  std::string out = "first\n";
  bw::shaders::append_enumerate_line(shader, PermutationId{3}, ShaderTarget::Vulkan, out);
  require(out == "first\ntest_shader\t3\tvulkan\tvulkan/test_shader.3.ps.bwsh\n",
          "enumerate line is exact and appended");
  for (const ShaderTarget target :
       {ShaderTarget::Direct3D12, ShaderTarget::Vulkan, ShaderTarget::Metal}) {
    require(bw::shaders::parse_target(bw::shaders::target_name(target)) == target,
            "target names round-trip");
  }
  require(bw::shaders::target_name(ShaderTarget::Direct3D12) == "d3d12" &&
              bw::shaders::target_name(ShaderTarget::Vulkan) == "vulkan" &&
              bw::shaders::target_name(ShaderTarget::Metal) == "metal",
          "target names");
  require(!bw::shaders::parse_target("dx12").has_value(), "unknown target name");
  require(bw::shaders::stage_profile(ShaderStage::Vertex) == "vs_6_4" &&
              bw::shaders::stage_profile(ShaderStage::Pixel) == "ps_6_4" &&
              bw::shaders::stage_profile(ShaderStage::Compute) == "cs_6_4",
          "stage profiles");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"parse_enumerate_and_compile", parse_enumerate_and_compile},
    Scenario{"parse_rejections", parse_rejections},
    Scenario{"arguments_fixed_order_no_paths", arguments_fixed_order_no_paths},
    Scenario{"enumerate_line_format", enumerate_line_format},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
