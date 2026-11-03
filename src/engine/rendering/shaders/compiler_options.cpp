#include "bw/rendering/shaders/compiler_options.hpp"

#include "bw/rendering/shaders/artifact.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

// Responsibility: the shader_compiler command line, target and profile names,
// the enumerate output line and the fixed DXC argument list of
// bw/rendering/shaders/compiler_options.hpp.
//
// Contract, parse_command_line: args[0] is the mode; the rest are
// "--option value" pairs from the header's grammar. Errors are found in
// argument order and the first is reported, each with the offending token:
//   MissingMode            no arguments (offending empty).
//   UnknownMode            args[0] is not enumerate or compile.
//   During the walk, per option token, in this order:
//   UnexpectedArgument     a token in option position that does not start
//                          with "--".
//   UnknownOption          a "--" token not in the mode's grammar.
//   MissingValue           an option is last, or its value starts with "--"
//                          (offending is the option).
//   RepeatedOption         an option given twice (offending is the option).
//   InvalidTarget          checked when the value is read: a target name not
//                          d3d12, vulkan or metal; in --targets also an empty
//                          element, a repeated target or a fourth one
//                          (offending is the option argument, --targets or
//                          --target).
//   InvalidPermutation     checked when the value is read: --permutation not
//                          a decimal u32 (offending is the option argument).
//   After the walk, offending is the option's name as static text, the two
//   errors the header's CommandLineResult comment names as not views into the
//   arguments, because the token is absent from them:
//   MissingRequiredOption  in grammar order.
//   ConverterRequiredForMetal  compile --target metal without
//                          --converter-library (offending "--converter-library").
// A --converter-library for another target is accepted and retained; the tool
// ignores it, as the grammar says. Views in the invocation point into args.
//
// Contract, append_compiler_arguments: appends one string per DXC argv
// element; an option and its value are separate elements ("-T", "vs_6_4"),
// in exactly the header's order: -HV 2021 -Zpr -O3 -WX -enable-16bit-types,
// the symbol flags (-Zss -Zi -Qstrip_debug on DXIL targets, -Zi on Vulkan),
// -disable-lifetime-markers, target define, domain defines, the root
// signature pair on DXIL targets (Direct3D 12 and Metal), Vulkan flags and
// -fspv-debug=line with symbols, then -E and -T. No element
// contains a path; the root signature text is passed through unchanged.
//
// Algorithm, parse: 1. mode; 2. option/value pairs in order, each recorded
// once and its value validated when read; 3. required options per mode;
// 4. the Metal converter rule.
//
// Design and cost: parsing is one pass, noexcept and allocation-free
// (from_chars for the permutation, string_view comparisons); the argument and
// enumerate builders allocate strings at build time.
//
// Retrieved rules: SL.str.2 (the invocation's views point into the caller's
// argument strings), E.27 (CommandLineError with the offending token; no
// exceptions), SL.con.3 (indices bounded by the argument span). Performance:
// MEM.9 (parsing allocates nothing), EMB.2 (TargetSet is fixed capacity;
// overflow is InvalidTarget, not growth).
namespace bw::shaders {
namespace {

constexpr std::string_view kTargets = "--targets";
constexpr std::string_view kOutputRoot = "--output-root";
constexpr std::string_view kShader = "--shader";
constexpr std::string_view kPermutation = "--permutation";
constexpr std::string_view kTarget = "--target";
constexpr std::string_view kShaderRoot = "--shader-root";
constexpr std::string_view kDepfile = "--depfile";
constexpr std::string_view kDxcLibrary = "--dxc-library";
constexpr std::string_view kConverterLibrary = "--converter-library";
constexpr std::string_view kSymbols = "--symbols";

[[nodiscard]] CommandLineResult failure(CommandLineError error, std::string_view offending) {
  CommandLineResult result{};
  result.error = error;
  result.offending = offending;
  return result;
}

// How an option's value is checked when it is read (step 2).
enum class ValueKind : std::uint8_t {
  Text,
  TargetList,
  Target,
  Permutation,
};

// One option of a mode: its name, whether required, and where its value goes.
// Exactly one destination is set, matching kind.
struct OptionSlot {
  std::string_view name{};
  std::string_view* text{};
  TargetSet* targets{};
  ShaderTarget* target{};
  PermutationId* permutation{};
  ValueKind kind{ValueKind::Text};
  bool required{};
  bool seen{};
};

[[nodiscard]] OptionSlot* find_slot(std::span<OptionSlot> slots, std::string_view name) noexcept {
  for (OptionSlot& slot : slots) {
    if (slot.name == name) {
      return &slot;
    }
  }
  return nullptr;
}

[[nodiscard]] bool starts_with_dashes(std::string_view token) noexcept {
  return token.size() >= 2U && token[0] == '-' && token[1] == '-';
}

[[nodiscard]] std::optional<std::uint32_t> parse_decimal_u32(std::string_view text) noexcept {
  if (text.empty()) {
    return std::nullopt;
  }
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return std::nullopt;
    }
  }
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

// --targets: comma-separated names, each known, none repeated, at most three.
// False on the first element that breaks a rule.
[[nodiscard]] bool parse_target_list(std::string_view list, TargetSet& set) noexcept {
  std::string_view rest = list;
  while (true) {
    const std::size_t comma = rest.find(',');
    const bool last = comma == std::string_view::npos;
    const std::string_view element{rest.data(), last ? rest.size() : comma};
    const std::optional<ShaderTarget> target = parse_target(element);
    if (!target.has_value() || set.count >= set.targets.size()) {
      return false;
    }
    for (const ShaderTarget existing : set.view()) {
      if (existing == *target) {
        return false;
      }
    }
    set.targets[set.count] = *target;
    ++set.count;
    if (last) {
      return true;
    }
    rest.remove_prefix(comma + 1U);
  }
}

// One "--option value" pair from the arguments.
struct OptionPair {
  std::string_view option{};
  std::string_view value{};
};

// Step 2, value check: records and validates one option's value. The option
// argument is the offending view for a value that does not parse.
[[nodiscard]] CommandLineResult read_value(OptionSlot& slot, const OptionPair& pair) noexcept {
  switch (slot.kind) {
  case ValueKind::Text:
    *slot.text = pair.value;
    return CommandLineResult{};
  case ValueKind::TargetList:
    if (!parse_target_list(pair.value, *slot.targets)) {
      return failure(CommandLineError::InvalidTarget, pair.option);
    }
    return CommandLineResult{};
  case ValueKind::Target: {
    const std::optional<ShaderTarget> target = parse_target(pair.value);
    if (!target.has_value()) {
      return failure(CommandLineError::InvalidTarget, pair.option);
    }
    *slot.target = *target;
    return CommandLineResult{};
  }
  case ValueKind::Permutation: {
    const std::optional<std::uint32_t> id = parse_decimal_u32(pair.value);
    if (!id.has_value()) {
      return failure(CommandLineError::InvalidPermutation, pair.option);
    }
    *slot.permutation = PermutationId{*id};
    return CommandLineResult{};
  }
  }
  // Unreachable: every ValueKind is handled above; the return satisfies the
  // compiler for an out-of-range enum value.
  return failure(CommandLineError::UnknownOption, pair.option);
}

// Steps 2-3 for a mode's slot table: errors in argument order, then the first
// missing required option in grammar order.
[[nodiscard]] CommandLineResult parse_options(std::span<const std::string_view> args,
                                              std::span<OptionSlot> slots) noexcept {
  for (std::size_t i = 1; i < args.size(); i += 2U) {
    const std::string_view option = args[i];
    if (!starts_with_dashes(option)) {
      return failure(CommandLineError::UnexpectedArgument, option);
    }
    OptionSlot* slot = find_slot(slots, option);
    if (slot == nullptr) {
      return failure(CommandLineError::UnknownOption, option);
    }
    if (i + 1U >= args.size() || starts_with_dashes(args[i + 1U])) {
      return failure(CommandLineError::MissingValue, option);
    }
    if (slot->seen) {
      return failure(CommandLineError::RepeatedOption, option);
    }
    slot->seen = true;
    if (CommandLineResult read =
            read_value(*slot, OptionPair{.option = option, .value = args[i + 1U]});
        !read.ok()) {
      return read;
    }
  }
  for (const OptionSlot& slot : slots) {
    if (slot.required && !slot.seen) {
      // The header's stated exception: the option's own name, not a view
      // into the arguments.
      return failure(CommandLineError::MissingRequiredOption, slot.name);
    }
  }
  return CommandLineResult{};
}

void append_decimal(std::int64_t value, std::string& out) {
  std::array<char, 24> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  // An int64 always fits; to_chars is locale-independent.
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

[[nodiscard]] std::string_view target_define(ShaderTarget target) noexcept {
  switch (target) {
  case ShaderTarget::Direct3D12:
    return "BW_TARGET_D3D12=1";
  case ShaderTarget::Vulkan:
    return "BW_TARGET_VULKAN=1";
  case ShaderTarget::Metal:
    return "BW_TARGET_METAL=1";
  }
  return {};
}

} // namespace

CommandLineResult parse_command_line(std::span<const std::string_view> args) noexcept {
  // Step 1: mode.
  if (args.empty()) {
    return failure(CommandLineError::MissingMode, {});
  }
  const std::string_view mode = args[0];
  CommandLineResult result{};
  if (mode == "enumerate") {
    result.invocation.mode = ToolMode::Enumerate;
    EnumerateOptions& enumerate = result.invocation.enumerate;
    std::array slots{
        OptionSlot{.name = kTargets,
                   .targets = &enumerate.targets,
                   .kind = ValueKind::TargetList,
                   .required = true},
        OptionSlot{.name = kOutputRoot, .text = &enumerate.output_root, .required = true},
    };
    // Steps 2-3.
    if (CommandLineResult parsed = parse_options(args, slots); !parsed.ok()) {
      return parsed;
    }
    return result;
  }
  if (mode == "compile") {
    result.invocation.mode = ToolMode::Compile;
    CompileOptions& compile = result.invocation.compile;
    std::array slots{
        OptionSlot{.name = kShader, .text = &compile.shader, .required = true},
        OptionSlot{.name = kPermutation,
                   .permutation = &compile.permutation,
                   .kind = ValueKind::Permutation,
                   .required = true},
        OptionSlot{.name = kTarget,
                   .target = &compile.target,
                   .kind = ValueKind::Target,
                   .required = true},
        OptionSlot{.name = kShaderRoot, .text = &compile.shader_root, .required = true},
        OptionSlot{.name = kOutputRoot, .text = &compile.output_root, .required = true},
        OptionSlot{.name = kDepfile, .text = &compile.depfile, .required = true},
        OptionSlot{.name = kDxcLibrary, .text = &compile.dxc_library, .required = true},
        OptionSlot{.name = kConverterLibrary, .text = &compile.converter_library},
        OptionSlot{.name = kSymbols, .text = &compile.symbols_dir},
    };
    // Steps 2-3.
    if (CommandLineResult parsed = parse_options(args, slots); !parsed.ok()) {
      return parsed;
    }
    // Step 4: the converter is required for Metal.
    if (compile.target == ShaderTarget::Metal && compile.converter_library.empty()) {
      return failure(CommandLineError::ConverterRequiredForMetal, kConverterLibrary);
    }
    return result;
  }
  return failure(CommandLineError::UnknownMode, mode);
}

std::string_view usage_text() noexcept {
  return "usage:\n"
         "  shader_compiler enumerate --targets <t>[,<t>...] --output-root <dir>\n"
         "  shader_compiler compile --shader <name> --permutation <id> --target <t>\n"
         "      --shader-root <dir> --output-root <dir> --depfile <path>\n"
         "      --dxc-library <path> [--converter-library <path>] [--symbols <dir>]\n"
         "<t> is d3d12, vulkan or metal. --converter-library is required when <t> is\n"
         "metal and ignored otherwise. --symbols enables symbol output.\n";
}

std::string_view target_name(ShaderTarget target) noexcept {
  switch (target) {
  case ShaderTarget::Direct3D12:
    return "d3d12";
  case ShaderTarget::Vulkan:
    return "vulkan";
  case ShaderTarget::Metal:
    return "metal";
  }
  return {};
}

std::optional<ShaderTarget> parse_target(std::string_view name) noexcept {
  for (const ShaderTarget target :
       {ShaderTarget::Direct3D12, ShaderTarget::Vulkan, ShaderTarget::Metal}) {
    if (name == target_name(target)) {
      return target;
    }
  }
  return std::nullopt;
}

std::string_view stage_profile(ShaderStage stage) noexcept {
  switch (stage) {
  case ShaderStage::Vertex:
    return "vs_6_4";
  case ShaderStage::Pixel:
    return "ps_6_4";
  case ShaderStage::Compute:
    return "cs_6_4";
  }
  return {};
}

void append_enumerate_line(const ShaderDescription& shader, PermutationId permutation,
                           ShaderTarget target, std::string& out) {
  out += shader.name;
  out += '\t';
  append_decimal(permutation.value, out);
  out += '\t';
  out += target_name(target);
  out += '\t';
  out += target_directory(target);
  out += '/';
  out += artifact_file_name(shader.name, permutation, shader.stage);
  out += '\n';
}

void append_compiler_arguments(ShaderTarget target, ShaderStage stage, std::string_view entry_point,
                               std::span<const Define> defines, std::string_view root_signature,
                               bool emit_symbols, std::vector<std::string>& out) {
  // The header's order: the fixed DXIL flags, symbols before
  // -disable-lifetime-markers, -E and -T last.
  const bool dxil = target != ShaderTarget::Vulkan;
  out.emplace_back("-HV");
  out.emplace_back("2021");
  out.emplace_back("-Zpr");
  out.emplace_back("-O3");
  out.emplace_back("-WX");
  out.emplace_back("-enable-16bit-types");
  if (emit_symbols) {
    if (dxil) {
      out.emplace_back("-Zss");
      out.emplace_back("-Zi");
      out.emplace_back("-Qstrip_debug");
    } else {
      out.emplace_back("-Zi");
    }
  }
  out.emplace_back("-disable-lifetime-markers");
  out.emplace_back("-D");
  out.emplace_back(target_define(target));
  for (const Define& define : defines) {
    out.emplace_back("-D");
    std::string text{define.name};
    text += '=';
    append_decimal(define.value, text);
    out.push_back(std::move(text));
  }
  if (dxil) {
    out.emplace_back("-D");
    std::string text{"BW_ROOT_SIGNATURE="};
    text += root_signature;
    out.push_back(std::move(text));
    out.emplace_back("-rootsig-define");
    out.emplace_back("BW_ROOT_SIGNATURE");
  } else {
    out.emplace_back("-spirv");
    out.emplace_back("-fspv-target-env=vulkan1.3");
    out.emplace_back("-fvk-use-dx-layout");
    out.emplace_back("-fspv-reflect");
    if (emit_symbols) {
      out.emplace_back("-fspv-debug=line");
    }
  }
  out.emplace_back("-E");
  out.emplace_back(entry_point);
  out.emplace_back("-T");
  out.emplace_back(stage_profile(stage));
}

} // namespace bw::shaders
