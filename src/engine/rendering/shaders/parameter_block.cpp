#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

// Responsibility: the HLSL generator of bw/rendering/shaders/parameter_block.hpp.
// It turns a BlockLayout into the struct text the bindless-access include
// consumes, and writes the binding line for a block.
//
// Contract: append_hlsl_struct appends to `out`, for the layout and first for
// each nested block not yet emitted in this call, exactly the text the header
// states, or returns a named HlslError. On an error `out` keeps what was
// appended before the failing member (nested structs already emitted, and the
// failing struct's opening and earlier lines); the header says the caller
// discards it. A layout built by BW_PARAMETER_BLOCK_LAYOUT never fails.
// Failure cases, each with `member` naming the offender:
//   - InvalidName: a block or member name is not [A-Za-z_][A-Za-z0-9_]*.
//   - MissingNestedLayout: a member of kind Block whose nested layout is
//     null; checked with the names, before anything of that block is emitted.
//   - OffsetBelowHlslPlacement: after 4-byte padding, place_in_hlsl puts the
//     member above its C++ offset (the C++ offset is below the HLSL
//     placement).
//   - LayoutExceedsBlockSize: the last member's HLSL end lies past the block
//     size; `member` is the last member.
// append_hlsl_block_binding writes one line and cannot fail.
// append_hlsl_buffer_loader validates names and nested layouts as the struct
// generator does (InvalidName, MissingNestedLayout), emits each nested
// block's loader first, once per call, then one assignment per member or
// array element at its C++ offset; it does no placement check, because the
// offsets it reads at are the C++ ones by construction. Element i of an
// array is at the member's offset + i * the C++ element size (the member's
// size over its count). The header writes that as 16 * i, which is the same
// for every value kind but not for an array of blocks or matrices; the C++
// offset, which the header also promises, is what is emitted.
//
// Algorithm, per block:
//   1. Validate the block name, every member name and every Block member's
//      nested layout.
//   2. Emit each nested block first, depth first, once per call.
//   3. Walk members in offset order with an HLSL running offset starting at 0:
//      while running < member.offset emit "uint bw_pad_<running>" and add 4;
//      then place_in_hlsl(member, running) must equal member.offset; emit the
//      member and set running to placement offset + placement size.
//   4. Running past the block size after the last member is
//      LayoutExceedsBlockSize naming that member; then, while running < block
//      size, emit padding words. A hand-built size that is not a multiple of 4
//      also ends past the size and is LayoutExceedsBlockSize naming the block.
//
// Design and cost: build-tool only. One pass over members per block, written
// straight into `out`; the emitted set is a small vector of observed layout
// pointers (nesting depth is a handful). Allocation is the output string and
// that vector.
//
// Retrieved rules: SL.str.2 (names are string_views into static storage; the
// result's `member` view points at the layout's own name), E.27 (HlslResult is
// a named error code the caller must check; partial text is never reported as
// success), SL.con.3 (every loop is bounded by the member span). Performance:
// EMB.2 caveat read (tiny static collections are a std::array or small vector;
// the emitted set is the latter because nesting is data-dependent), MEM.9
// (allocation confined to the build tool; nothing here runs in the game's
// steady state).
namespace bw::shaders {
namespace {

constexpr std::uint32_t kPaddingWordBytes = 4;

[[nodiscard]] bool is_identifier_start(char c) noexcept {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

[[nodiscard]] bool is_identifier_char(char c) noexcept {
  return is_identifier_start(c) || (c >= '0' && c <= '9');
}

// An HLSL identifier: non-empty, [A-Za-z_][A-Za-z0-9_]*.
[[nodiscard]] bool is_hlsl_identifier(std::string_view name) noexcept {
  if (name.empty() || !is_identifier_start(name.front())) {
    return false;
  }
  for (const char c : name) {
    if (!is_identifier_char(c)) {
      return false;
    }
  }
  return true;
}

void append_decimal(std::uint32_t value, std::string& out) {
  // to_chars is locale-independent; the text is an artifact hash input.
  std::array<char, 16> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  // A uint32 always fits 16 characters; the check keeps the contract visible.
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

[[nodiscard]] std::string_view hlsl_scalar_type(ParameterKind kind) noexcept {
  switch (kind) {
  case ParameterKind::Float:
    return "float";
  case ParameterKind::Int32:
    return "int";
  case ParameterKind::Uint32:
  case ParameterKind::TextureIndex:
  case ParameterKind::BufferIndex:
  case ParameterKind::SamplerIndex:
  case ParameterKind::StorageTextureIndex:
  case ParameterKind::StorageBufferIndex:
    return "uint";
  case ParameterKind::Float2:
    return "float2";
  case ParameterKind::Float3:
    return "float3";
  case ParameterKind::Float4:
    return "float4";
  case ParameterKind::Int4:
    return "int4";
  case ParameterKind::Uint4:
    return "uint4";
  case ParameterKind::Float4x4:
    return "float4x4";
  case ParameterKind::Block:
    return {};
  }
  return {};
}

// Precondition (step 1): a Block member has a nested layout.
void append_type(const MemberLayout& member, std::string& out) {
  if (member.kind == ParameterKind::Block && member.nested != nullptr) {
    out += "BW_";
    out += member.nested->name;
    return;
  }
  out += hlsl_scalar_type(member.kind);
}

void append_padding_word(std::uint32_t offset, std::string& out) {
  out += "  uint bw_pad_";
  append_decimal(offset, out);
  out += ";\n";
}

class StructEmitter {
public:
  explicit StructEmitter(std::string& text) : text_(text) {}

  // Steps 1-4 of the file algorithm for `layout`, nested blocks first.
  [[nodiscard]] HlslResult emit(const BlockLayout& layout) {
    if (!is_hlsl_identifier(layout.name)) {
      return HlslResult{.error = HlslError::InvalidName, .member = layout.name};
    }
    for (const MemberLayout& member : layout.members) {
      if (!is_hlsl_identifier(member.name)) {
        return HlslResult{.error = HlslError::InvalidName, .member = member.name};
      }
      if (member.kind == ParameterKind::Block && member.nested == nullptr) {
        return HlslResult{.error = HlslError::MissingNestedLayout, .member = member.name};
      }
    }
    // Step 2: mark before descending so a hand-built self-reference cannot
    // recurse forever; it then names an unemitted type, which DXC rejects.
    visited_.push_back(&layout);
    for (const MemberLayout& member : layout.members) {
      if (member.kind == ParameterKind::Block && !was_visited(member.nested)) {
        const HlslResult nested = emit(*member.nested);
        if (nested.error != HlslError::None) {
          return nested;
        }
      }
    }
    return emit_struct(layout);
  }

private:
  [[nodiscard]] bool was_visited(const BlockLayout* layout) const noexcept {
    for (const BlockLayout* seen : visited_) {
      if (seen == layout) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] HlslResult emit_struct(const BlockLayout& layout) {
    text_ += "struct BW_";
    text_ += layout.name;
    text_ += " {\n";
    std::uint32_t running = 0;
    // Step 3: padding words, then the member at its HLSL placement.
    for (const MemberLayout& member : layout.members) {
      while (running < member.offset) {
        append_padding_word(running, text_);
        running += kPaddingWordBytes;
      }
      const HlslPlacement placement = place_in_hlsl(member, running);
      if (placement.offset != member.offset) {
        return HlslResult{.error = HlslError::OffsetBelowHlslPlacement, .member = member.name};
      }
      text_ += "  ";
      append_type(member, text_);
      text_ += ' ';
      text_ += member.name;
      if (member.element_count != 0) {
        text_ += '[';
        append_decimal(member.element_count, text_);
        text_ += ']';
      }
      text_ += ";\n";
      running = placement.offset + placement.size;
    }
    // Step 4: the last member's HLSL end must lie within the block size; a
    // member is the only thing that can pass it (running > 0 implies a member).
    if (running > layout.size) {
      return HlslResult{.error = HlslError::LayoutExceedsBlockSize,
                        .member = layout.members.back().name};
    }
    // Tail padding to the C++ size.
    while (running < layout.size) {
      append_padding_word(running, text_);
      running += kPaddingWordBytes;
    }
    if (running != layout.size) {
      // A hand-built size that is not a multiple of 4: the last padding word
      // ends past it. Named by the block, since no member is at fault.
      return HlslResult{.error = HlslError::LayoutExceedsBlockSize, .member = layout.name};
    }
    text_ += "};\n";
    return HlslResult{};
  }

  std::string& text_;
  std::vector<const BlockLayout*> visited_;
};

// The text "offset + <decimal byte>" of one load's address.
void append_address(std::uint32_t byte, std::string& out) {
  out += "offset + ";
  append_decimal(byte, out);
}

// One raw load at `byte`: "BW_BUFFER(buffer).<method>(offset + <byte>)".
void append_raw_load(std::string_view method, std::uint32_t byte, std::string& out) {
  out += "BW_BUFFER(buffer).";
  out += method;
  out += '(';
  append_address(byte, out);
  out += ')';
}

// One value of `member`'s kind at `byte`, as the header's table states.
// Precondition (validated by the caller): a Block member has a nested
// layout.
void append_value_load(const MemberLayout& member, std::uint32_t byte, std::string& out) {
  switch (member.kind) {
  case ParameterKind::Float:
    out += "asfloat(";
    append_raw_load("Load", byte, out);
    out += ')';
    return;
  case ParameterKind::Int32:
    out += "asint(";
    append_raw_load("Load", byte, out);
    out += ')';
    return;
  case ParameterKind::Uint32:
  case ParameterKind::TextureIndex:
  case ParameterKind::BufferIndex:
  case ParameterKind::SamplerIndex:
  case ParameterKind::StorageTextureIndex:
  case ParameterKind::StorageBufferIndex:
    append_raw_load("Load", byte, out);
    return;
  case ParameterKind::Float2:
  case ParameterKind::Float3:
  case ParameterKind::Float4:
    out += "asfloat(";
    append_raw_load(member.kind == ParameterKind::Float2   ? "Load2"
                    : member.kind == ParameterKind::Float3 ? "Load3"
                                                           : "Load4",
                    byte, out);
    out += ')';
    return;
  case ParameterKind::Int4:
    out += "asint(";
    append_raw_load("Load4", byte, out);
    out += ')';
    return;
  case ParameterKind::Uint4:
    append_raw_load("Load4", byte, out);
    return;
  case ParameterKind::Float4x4:
    out += "float4x4(";
    for (std::uint32_t row = 0; row < 4U; ++row) {
      if (row != 0U) {
        out += ", ";
      }
      out += "asfloat(";
      append_raw_load("Load4", byte + (kConstantRowBytes * row), out);
      out += ')';
    }
    out += ')';
    return;
  case ParameterKind::Block:
    out += "bw_load_";
    out += member.nested->name;
    out += "(buffer, ";
    append_address(byte, out);
    out += ')';
    return;
  }
}

class LoaderEmitter {
public:
  explicit LoaderEmitter(std::string& text) : text_(text) {}

  // Nested loaders first, depth first and once per call, then `layout`'s.
  [[nodiscard]] HlslResult emit(const BlockLayout& layout) {
    if (!is_hlsl_identifier(layout.name)) {
      return HlslResult{.error = HlslError::InvalidName, .member = layout.name};
    }
    for (const MemberLayout& member : layout.members) {
      if (!is_hlsl_identifier(member.name)) {
        return HlslResult{.error = HlslError::InvalidName, .member = member.name};
      }
      if (member.kind == ParameterKind::Block && member.nested == nullptr) {
        return HlslResult{.error = HlslError::MissingNestedLayout, .member = member.name};
      }
    }
    visited_.push_back(&layout);
    for (const MemberLayout& member : layout.members) {
      if (member.kind == ParameterKind::Block && !was_visited(member.nested)) {
        const HlslResult nested = emit(*member.nested);
        if (nested.error != HlslError::None) {
          return nested;
        }
      }
    }
    emit_function(layout);
    return HlslResult{};
  }

private:
  [[nodiscard]] bool was_visited(const BlockLayout* layout) const noexcept {
    for (const BlockLayout* seen : visited_) {
      if (seen == layout) {
        return true;
      }
    }
    return false;
  }

  void emit_function(const BlockLayout& layout) {
    text_ += "BW_";
    text_ += layout.name;
    text_ += " bw_load_";
    text_ += layout.name;
    text_ += "(uint buffer, uint offset) {\n  BW_";
    text_ += layout.name;
    text_ += " block = (BW_";
    text_ += layout.name;
    text_ += ")0;\n";
    for (const MemberLayout& member : layout.members) {
      const std::uint32_t count = member.element_count == 0U ? 1U : member.element_count;
      // The C++ element stride: 16 for every value kind, a block's size or
      // 64 for a matrix (an Array element is its value aligned to 16).
      const std::uint32_t stride = member.size / count;
      for (std::uint32_t i = 0; i < count; ++i) {
        text_ += "  block.";
        text_ += member.name;
        if (member.element_count != 0U) {
          text_ += '[';
          append_decimal(i, text_);
          text_ += ']';
        }
        text_ += " = ";
        append_value_load(member, member.offset + (stride * i), text_);
        text_ += ";\n";
      }
    }
    text_ += "  return block;\n}\n";
  }

  std::string& text_;
  std::vector<const BlockLayout*> visited_;
};

} // namespace

HlslResult append_hlsl_struct(const BlockLayout& layout, std::string& out) {
  StructEmitter emitter{out};
  return emitter.emit(layout);
}

HlslResult append_hlsl_buffer_loader(const BlockLayout& layout, std::string& out) {
  LoaderEmitter emitter{out};
  return emitter.emit(layout);
}

void append_hlsl_block_binding(const BlockLayout& layout, bool is_static_slot,
                               std::uint32_t static_slot, std::string& out) {
  if (is_static_slot) {
    out += "BW_STATIC_BLOCK(BW_";
    out += layout.name;
    out += ", ";
    out += layout.name;
    out += ", ";
    append_decimal(static_slot, out);
    out += ")\n";
    return;
  }
  out += "BW_DRAW_BLOCK(BW_";
  out += layout.name;
  out += ", ";
  out += layout.name;
  out += ")\n";
}

} // namespace bw::shaders
