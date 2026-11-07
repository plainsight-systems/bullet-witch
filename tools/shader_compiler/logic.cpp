#include "bw/shader_compiler/logic.hpp"

#include <blake3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Responsibility: the pure decisions of shader_compiler declared in
// bw/shader_compiler/logic.hpp. Nothing here touches DXC, a file or the
// environment.
//
// Contract: as the header states, function by function. The only allocation
// is the strings and vectors the functions return or append to.
//
// Algorithm notes:
//   - Classification walks the eight placements of root_layout.hpp; the
//     table is tiny, so a scan is the lookup (CDSA.9).
//   - assemble_reflection resolves a static slot through the root layout's
//     slot name and the description's block of that name, so the Reflection
//     names and hashes are the registry's whatever name the backend gave the
//     resource. Every buffer block the description declares is added as a
//     Buffer block at its index (permutation.hpp, buffer_blocks); logic.hpp
//     states static and draw blocks only and is silent on these.
//   - append_generated_include emits, after the structs of the draw and
//     static blocks, the struct of each buffer block, then the binding lines
//     of the draw and static blocks, then the loader of each buffer block;
//     logic.hpp's text names the first two parts only.
//   - compute_input_hash streams the items through one BLAKE3 hasher; each
//     item is preceded by its byte length as eight little-endian bytes.
//
// Retrieved rules: E.27 (named errors), SL.con.3 (every index is checked
// against its bound), SL.str.2 (string_view in, std::string out). Performance:
// CDSA.9 (linear scans of tiny tables), COPY.1 (no std::move on returns).
namespace bw::shader_compiler {
namespace {

using shaders::BlockLayout;
using shaders::DescriptorArray;
using shaders::DescriptorArrayPlacement;
using shaders::kDescriptorArrayCount;
using shaders::kMaxStaticBlocks;

void append_signed_decimal(std::int32_t value, std::string& out) {
  // to_chars is locale-independent; the text feeds the input hash.
  std::array<char, 16> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

// The hasher state and the item framing of compute_input_hash.
class InputHasher {
public:
  InputHasher() noexcept { blake3_hasher_init(&hasher_); }

  void raw_u64(std::uint64_t value) noexcept {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
    blake3_hasher_update(&hasher_, bytes.data(), bytes.size());
  }

  void item(std::span<const std::uint8_t> bytes) noexcept {
    raw_u64(bytes.size());
    blake3_hasher_update(&hasher_, bytes.data(), bytes.size());
  }

  void item(std::string_view text) noexcept {
    raw_u64(text.size());
    blake3_hasher_update(&hasher_, text.data(), text.size());
  }

  void item_u32(std::uint32_t value) noexcept {
    std::array<std::uint8_t, 4> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
    item(bytes);
  }

  void item_u64(std::uint64_t value) noexcept {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
      bytes[i] = static_cast<std::uint8_t>((value >> (8U * i)) & 0xFFU);
    }
    item(bytes);
  }

  void item_u8(std::uint8_t value) noexcept {
    const std::array<std::uint8_t, 1> bytes{value};
    item(bytes);
  }

  [[nodiscard]] shaders::InputHash finish() noexcept {
    shaders::InputHash hash{};
    blake3_hasher_finalize(&hasher_, hash.data(), hash.size());
    return hash;
  }

private:
  blake3_hasher hasher_{};
};

// Every block reachable from `layout`, itself included, by name.
void collect_block_names(const BlockLayout& layout, std::vector<std::string_view>& names) {
  if (std::find(names.begin(), names.end(), layout.name) != names.end()) {
    return;
  }
  names.push_back(layout.name);
  for (const shaders::MemberLayout& member : layout.members) {
    if (member.nested != nullptr) {
      collect_block_names(*member.nested, names);
    }
  }
}

const BlockLayout* find_static_block(const shaders::ShaderDescription& shader,
                                     std::string_view name) noexcept {
  for (const BlockLayout* block : shader.static_blocks) {
    if (block != nullptr && block->name == name) {
      return block;
    }
  }
  return nullptr;
}

void append_escaped(std::string_view path, std::string& out) {
  for (const char c : path) {
    if (c == ' ' || c == '#') {
      out += '\\';
      out += c;
    } else if (c == '$') {
      out += "$$";
    } else {
      out += c;
    }
  }
}

} // namespace

std::optional<BlockBinding> classify_dxil_constant_buffer(std::uint32_t register_index,
                                                          std::uint32_t space) noexcept {
  if (space == shaders::kStaticBlockSpace && register_index < kMaxStaticBlocks) {
    return BlockBinding{BlockBindingKind::Static, register_index};
  }
  if (space == shaders::kDrawBlockSpace && register_index == 0U) {
    return BlockBinding{BlockBindingKind::Draw, 0};
  }
  if (space == shaders::kRootConstantSpace && register_index == 0U) {
    return BlockBinding{BlockBindingKind::RootConstants, 0};
  }
  return std::nullopt;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): (set, binding) is Vulkan's own order.
std::optional<BlockBinding> classify_vulkan_uniform_buffer(std::uint32_t set,
                                                           std::uint32_t binding) noexcept {
  if (set != shaders::kVulkanUniformSet) {
    return std::nullopt;
  }
  if (binding < kMaxStaticBlocks) {
    return BlockBinding{BlockBindingKind::Static, binding};
  }
  if (binding == kMaxStaticBlocks) {
    return BlockBinding{BlockBindingKind::Draw, 0};
  }
  return std::nullopt;
}

std::optional<DescriptorArray> descriptor_array_for_register(char register_class,
                                                             std::uint32_t space) noexcept {
  for (std::uint32_t index = 0; index < kDescriptorArrayCount; ++index) {
    const auto array = static_cast<DescriptorArray>(index);
    const DescriptorArrayPlacement where = shaders::placement(array);
    if (where.register_class == register_class && where.register_space == space) {
      return array;
    }
  }
  return std::nullopt;
}

std::optional<DescriptorArray> descriptor_array_for_vulkan_binding(std::uint32_t set,
                                                                   std::uint32_t binding) noexcept {
  for (std::uint32_t index = 0; index < kDescriptorArrayCount; ++index) {
    const auto array = static_cast<DescriptorArray>(index);
    const DescriptorArrayPlacement where = shaders::placement(array);
    if (where.vulkan_set == set && where.vulkan_binding == binding) {
      return array;
    }
  }
  return std::nullopt;
}

std::optional<Semantic> split_semantic(std::string_view semantic) {
  std::size_t digits_begin = semantic.size();
  while (digits_begin > 0 && semantic[digits_begin - 1] >= '0' &&
         semantic[digits_begin - 1] <= '9') {
    --digits_begin;
  }
  if (digits_begin == 0) {
    return std::nullopt;
  }
  Semantic result{};
  if (digits_begin < semantic.size()) {
    const char* first = semantic.data() + digits_begin;
    const char* last = semantic.data() + semantic.size();
    const auto [end, error] = std::from_chars(first, last, result.index);
    if (error != std::errc{} || end != last) {
      return std::nullopt;
    }
  }
  result.name.reserve(digits_begin);
  for (const char c : semantic.substr(0, digits_begin)) {
    result.name += (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
  }
  return result;
}

bool keeps_vertex_input(std::string_view semantic, bool system_value) noexcept {
  if (system_value) {
    return false;
  }
  const bool sv_prefix = semantic.size() >= 3 && (semantic[0] == 'S' || semantic[0] == 's') &&
                         (semantic[1] == 'V' || semantic[1] == 'v') && semantic[2] == '_';
  return !sv_prefix;
}

AssembledReflection assemble_reflection(const BackendReflection& backend,
                                        const shaders::ShaderDescription& shader,
                                        const shaders::RootLayout& layout) noexcept {
  AssembledReflection result{};
  shaders::Reflection& out = result.reflection;
  for (const BoundBlock& bound : backend.blocks) {
    if (out.block_count >= shaders::kMaxReflectedBlocks) {
      result.error = ReflectionError::TooManyBlocks;
      result.slot = bound.binding.slot;
      return result;
    }
    shaders::ReflectedBlock block{};
    if (bound.binding.kind == BlockBindingKind::Static) {
      const std::uint32_t slot = bound.binding.slot;
      const std::string_view name =
          slot < kMaxStaticBlocks ? layout.static_slots[slot].block_name : std::string_view{};
      const BlockLayout* declared = name.empty() ? nullptr : find_static_block(shader, name);
      if (declared == nullptr) {
        result.error = ReflectionError::StaticBlockNotDeclared;
        result.slot = slot;
        return result;
      }
      block = shaders::ReflectedBlock{declared->name, declared->hash, slot,
                                      shaders::BlockSlotKind::Static};
    } else if (bound.binding.kind == BlockBindingKind::Draw) {
      if (shader.draw_block == nullptr) {
        result.error = ReflectionError::DrawBlockNotDeclared;
        return result;
      }
      block = shaders::ReflectedBlock{shader.draw_block->name, shader.draw_block->hash, 0,
                                      shaders::BlockSlotKind::Draw};
    } else {
      // Root constants are reported through uses_root_constants; a block
      // entry of that kind carries no layout of its own.
      continue;
    }
    for (std::uint32_t i = 0; i < out.block_count; ++i) {
      if (out.blocks[i].kind == block.kind && out.blocks[i].slot == block.slot) {
        result.error = ReflectionError::DuplicateBlock;
        result.slot = block.slot;
        return result;
      }
    }
    out.blocks[out.block_count] = block;
    ++out.block_count;
  }
  // Buffer blocks bind nothing, so no backend reports them: each one the
  // description declares is recorded at its index in buffer_blocks, so its
  // layout hash is checked at load (artifact.hpp, BlockSlotKind::Buffer). A
  // null entry never gets here: append_generated_include refuses it first
  // (NullBlock).
  for (std::size_t i = 0; i < shader.buffer_blocks.size(); ++i) {
    const BlockLayout* declared = shader.buffer_blocks[i];
    if (declared == nullptr) {
      continue;
    }
    if (out.block_count >= shaders::kMaxReflectedBlocks) {
      result.error = ReflectionError::TooManyBlocks;
      result.slot = static_cast<std::uint32_t>(i);
      return result;
    }
    out.blocks[out.block_count] =
        shaders::ReflectedBlock{declared->name, declared->hash, static_cast<std::uint32_t>(i),
                                shaders::BlockSlotKind::Buffer};
    ++out.block_count;
  }
  std::sort(out.blocks.begin(), out.blocks.begin() + out.block_count,
            [](const shaders::ReflectedBlock& a, const shaders::ReflectedBlock& b) {
              return a.kind != b.kind ? a.kind < b.kind : a.slot < b.slot;
            });

  if (backend.vertex_inputs.size() > shaders::kMaxVertexInputs) {
    result.error = ReflectionError::TooManyVertexInputs;
    return result;
  }
  for (const BackendVertexInput& input : backend.vertex_inputs) {
    if (input.semantic.name.size() > shaders::kMaxArtifactString) {
      result.error = ReflectionError::SemanticTooLong;
      return result;
    }
    out.vertex_inputs[out.vertex_input_count] =
        shaders::VertexInput{input.semantic.name, input.semantic.index, input.location};
    ++out.vertex_input_count;
  }
  out.thread_group_size = backend.thread_group_size;
  out.root_constant_words = backend.uses_root_constants ? layout.root_constant_words : 0U;
  out.descriptor_arrays = backend.descriptor_arrays;
  return result;
}

shaders::InputHash compute_input_hash(const HashInputs& inputs) {
  InputHasher hasher{};
  hasher.item_u32(shaders::kArtifactFormatVersion);
  hasher.item(inputs.compiler_identity);
  hasher.item_u8(static_cast<std::uint8_t>(inputs.target));
  hasher.item_u8(static_cast<std::uint8_t>(inputs.stage));
  hasher.item(inputs.entry_point);
  hasher.raw_u64(inputs.defines.size());
  for (const shaders::Define& define : inputs.defines) {
    std::string text{define.name};
    text += '=';
    append_signed_decimal(define.value, text);
    hasher.item(text);
  }
  hasher.raw_u64(inputs.arguments.size());
  for (const std::string& argument : inputs.arguments) {
    hasher.item(argument);
  }
  hasher.item_u64(inputs.root_layout_hash.value);
  hasher.item(inputs.generated_include);
  hasher.item(inputs.main_source);
  hasher.raw_u64(inputs.includes.size());
  for (const IncludedFile& include : inputs.includes) {
    hasher.item(include.path);
    hasher.item(include.text);
  }
  return hasher.finish();
}

std::string input_hash_hex(const shaders::InputHash& hash) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string text;
  text.reserve(hash.size() * 2);
  for (const std::uint8_t byte : hash) {
    text += digits[byte >> 4U];
    text += digits[byte & 0x0FU];
  }
  return text;
}

std::string converter_identity(const std::array<std::uint8_t, 16>& uuid) {
  constexpr std::string_view digits = "0123456789abcdef";
  std::string text{"metal-shaderconverter uuid:"};
  for (const std::uint8_t byte : uuid) {
    text += digits[byte >> 4U];
    text += digits[byte & 0x0FU];
  }
  return text;
}

std::optional<std::array<std::uint8_t, 16>> mach_o_uuid(std::span<const std::byte> image) noexcept {
  constexpr std::uint64_t kHeaderBytes = 32;
  constexpr std::uint64_t kLoadCommandBytes = 8;
  constexpr std::uint64_t kUuidCommandBytes = 24;
  constexpr std::uint32_t kMagic64 = 0xfeedfacfU;
  constexpr std::uint32_t kLcUuid = 0x1bU;
  const auto u32_at = [&image](std::uint64_t at) noexcept {
    std::uint32_t value = 0;
    for (std::uint64_t i = 0; i < 4; ++i) {
      value |= static_cast<std::uint32_t>(image[static_cast<std::size_t>(at + i)]) << (8U * i);
    }
    return value;
  };
  if (image.size() < kHeaderBytes || u32_at(0) != kMagic64) {
    return std::nullopt;
  }
  const std::uint32_t command_count = u32_at(16);
  const std::uint64_t commands_size = u32_at(20);
  if (commands_size > image.size() - kHeaderBytes) {
    return std::nullopt;
  }
  // Offsets are relative to the first load command; every sum is of values
  // below 2^33, so 64-bit arithmetic cannot wrap.
  std::uint64_t offset = 0;
  for (std::uint32_t i = 0; i < command_count; ++i) {
    if (commands_size - offset < kLoadCommandBytes) {
      return std::nullopt;
    }
    const std::uint64_t at = kHeaderBytes + offset;
    const std::uint32_t command = u32_at(at);
    const std::uint64_t command_size = u32_at(at + 4);
    if (command_size < kLoadCommandBytes || command_size > commands_size - offset) {
      return std::nullopt;
    }
    if (command == kLcUuid) {
      if (command_size < kUuidCommandBytes) {
        return std::nullopt;
      }
      std::array<std::uint8_t, 16> uuid{};
      for (std::size_t b = 0; b < uuid.size(); ++b) {
        uuid[b] = static_cast<std::uint8_t>(image[static_cast<std::size_t>(at + 8 + b)]);
      }
      return uuid;
    }
    offset += command_size;
  }
  return std::nullopt;
}

IncludeResult append_generated_include(const shaders::ShaderDescription& shader,
                                       const shaders::RootLayout& layout, std::string& out) {
  // The top-level blocks in emission order: draw block, then static blocks,
  // then the blocks the shader reads from the buffer array.
  std::vector<const BlockLayout*> top_level;
  if (shader.draw_block != nullptr) {
    top_level.push_back(shader.draw_block);
  }
  std::vector<std::uint32_t> static_slots;
  for (const BlockLayout* block : shader.static_blocks) {
    if (block == nullptr) {
      return IncludeResult{{}, IncludeError::NullBlock, shaders::HlslError::None};
    }
    const std::optional<std::uint32_t> slot = shaders::find_static_slot(layout, block->name);
    if (!slot.has_value()) {
      return IncludeResult{block->name, IncludeError::StaticBlockWithoutSlot,
                           shaders::HlslError::None};
    }
    if (layout.static_slots[*slot].layout_hash != block->hash) {
      return IncludeResult{block->name, IncludeError::StaticSlotHashMismatch,
                           shaders::HlslError::None};
    }
    static_slots.push_back(*slot);
    top_level.push_back(block);
  }
  for (const BlockLayout* block : shader.buffer_blocks) {
    if (block == nullptr) {
      return IncludeResult{{}, IncludeError::NullBlock, shaders::HlslError::None};
    }
    top_level.push_back(block);
  }

  // A block reachable from two top-level blocks would be emitted twice.
  std::vector<std::string_view> seen;
  for (const BlockLayout* block : top_level) {
    std::vector<std::string_view> reachable;
    collect_block_names(*block, reachable);
    for (const std::string_view name : reachable) {
      if (std::find(seen.begin(), seen.end(), name) != seen.end()) {
        return IncludeResult{name, IncludeError::SharedBlock, shaders::HlslError::None};
      }
    }
    seen.insert(seen.end(), reachable.begin(), reachable.end());
  }

  for (const BlockLayout* block : top_level) {
    const shaders::HlslResult hlsl = shaders::append_hlsl_struct(*block, out);
    if (hlsl.error != shaders::HlslError::None) {
      return IncludeResult{hlsl.member, IncludeError::Hlsl, hlsl.error};
    }
  }
  if (shader.draw_block != nullptr) {
    shaders::append_hlsl_block_binding(*shader.draw_block, false, 0, out);
  }
  for (std::size_t i = 0; i < shader.static_blocks.size(); ++i) {
    shaders::append_hlsl_block_binding(*shader.static_blocks[i], true, static_slots[i], out);
  }
  // A buffer block binds nothing; its loader reads it at the offsets of the
  // C++ declaration (parameter_block.hpp, append_hlsl_buffer_loader).
  for (const BlockLayout* block : shader.buffer_blocks) {
    const shaders::HlslResult hlsl = shaders::append_hlsl_buffer_loader(*block, out);
    if (hlsl.error != shaders::HlslError::None) {
      return IncludeResult{hlsl.member, IncludeError::Hlsl, hlsl.error};
    }
  }
  return IncludeResult{};
}

std::string generated_include_path(std::string_view shader_name) {
  std::string path{"bw/generated/"};
  path += shader_name;
  path += ".hlsli";
  return path;
}

std::optional<std::string> normalize_include_path(std::string_view requested) {
  std::string unified{requested};
  std::replace(unified.begin(), unified.end(), '\\', '/');
  if (unified.empty() || unified.front() == '/') {
    return std::nullopt;
  }
  std::vector<std::string_view> components;
  const std::string_view view{unified};
  std::size_t begin = 0;
  while (begin <= view.size()) {
    const std::size_t end = std::min(view.find('/', begin), view.size());
    const std::string_view component = view.substr(begin, end - begin);
    if (component.find(':') != std::string_view::npos) {
      // A drive or URL prefix: not a path under the shader root.
      return std::nullopt;
    }
    if (component == "..") {
      if (components.empty()) {
        return std::nullopt;
      }
      components.pop_back();
    } else if (!component.empty() && component != ".") {
      components.push_back(component);
    }
    begin = end + 1;
  }
  if (components.empty()) {
    return std::nullopt;
  }
  std::string normalized;
  for (const std::string_view component : components) {
    if (!normalized.empty()) {
      normalized += '/';
    }
    normalized += component;
  }
  return normalized;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): root then relative, as a path reads.
std::string join_path(std::string_view root, std::string_view relative) {
  std::string path{root};
  if (!path.empty() && path.back() != '/') {
    path += '/';
  }
  path += relative;
  return path;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): target first, as the Makefile rule reads.
void append_depfile(std::string_view target, std::string_view shader_root,
                    std::string_view main_source, std::span<const std::string> includes,
                    std::string& out) {
  append_escaped(target, out);
  out += ": \\\n  ";
  append_escaped(join_path(shader_root, main_source), out);
  for (const std::string& include : includes) {
    out += " \\\n  ";
    append_escaped(join_path(shader_root, include), out);
  }
  out += '\n';
}

std::string artifact_relative_path(shaders::ShaderTarget target, std::string_view shader_name,
                                   shaders::PermutationId permutation, shaders::ShaderStage stage) {
  std::string path{shaders::target_directory(target)};
  path += '/';
  path += shaders::artifact_file_name(shader_name, permutation, stage);
  return path;
}

std::string symbol_file_name(const shaders::InputHash& hash, shaders::ShaderTarget target) {
  std::string name = input_hash_hex(hash);
  name += target == shaders::ShaderTarget::Vulkan ? ".spv" : ".pdb";
  return name;
}

} // namespace bw::shader_compiler
