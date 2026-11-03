#ifndef BW_RENDERING_SHADERS_PARAMETER_BLOCK_HPP
#define BW_RENDERING_SHADERS_PARAMETER_BLOCK_HPP
#include "bw/memory/alignment.hpp"

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>

// Responsibility: C++ parameter block declarations as the single source of
// constant buffer layout truth (docs/architecture/shaders.md, invariant 4).
// A block is a plain standard-layout struct of the value types below; its
// BlockLayout is built once at compile time and carries name, size, members
// and a layout hash. The HLSL generator emits a struct whose member offsets
// equal the C++ offsets by construction. Consumers: shader_compiler (HLSL
// generation, reflection lookup), the RHI (layout hash check at load, upload
// size), every renderer module (declares blocks).
//
// Contract. Inputs: a block type and the list of its members through the two
// macros at the end of this header. Outputs: BlockLayoutOf<Block>::value, a
// constexpr BlockLayout; generated HLSL text. Preconditions a block must
// meet, each checked when the layout is built and a compile error otherwise:
//   - standard layout, trivially copyable, alignof 16, sizeof a multiple of 16;
//   - every member listed, each listed once, names non-empty and unique;
//   - members dense: each member starts at align_up(end of the previous
//     member, its alignment) and the last one ends within 16 bytes of sizeof.
//     An unlisted member leaves a gap and fails; a member of a type not in the
//     value set fails to match a trait.
// Postconditions: members are sorted by offset; layout hash is a pure
// function of (name, size, members); the HLSL generator never fails for a
// layout that built. Failure: generation of a layout that did not come from
// the macros reports a named HlslError. Ownership: a BlockLayout refers to
// static storage only (its member array and nested layouts are static
// constexpr objects); nothing is owned or freed. No synchronization: all data
// is immutable after static initialization.
//
// Algorithm, build time:
//   1. The value types carry the member alignment in the type (Float2 8,
//      Float3 and wider 16, arrays 16 per element). The C++ compiler then
//      places each member at align_up(previous end, alignment).
//   2. BW_PARAMETER_MEMBER records name, kind, offsetof, sizeof and element
//      count; BW_PARAMETER_BLOCK_LAYOUT sorts the records by offset, checks
//      the preconditions, hashes, and stores the BlockLayout.
//   3. append_hlsl_struct walks the members, inserting 4-byte padding members
//      until the HLSL running offset reaches each C++ offset, then tail
//      padding to the C++ size. Nested blocks are emitted first.
//   4. The tool compiles the generated text; reflection of the compiled
//      shader is compared against the C++ offsets by the integration test.
// Algorithm, load time: the RHI compares the artifact's per-block layout hash
// with BlockLayoutOf<Block>::value.hash (artifact.hpp).
//
// Design and cost: every layout and hash is a compile-time constant; no
// allocation or work at runtime except generation, which appends to a
// std::string in the build tool. Work scales with member count. The layout
// hash covers every member, not only resources.
//
// Core guidelines read: I.1 (layout is passed explicitly; no registry),
// I.30 and ES.31 (the two macros exist only because offsetof and the member
// name cannot be obtained otherwise; each expands to one constexpr call),
// Enum.3 (ParameterKind), Con.5 and T.123 (constexpr layout and hash), I.9
// and T.10 (concepts constrain every template parameter), R.14 and SL.str.2
// (span and string_view for borrowed data), C.32 (the nested pointer is a
// non-owning observer), SL.con.3 (no index reaches past a span). Performance
// guidelines read: CACHE.5 (value types order members by alignment and the
// density rule forbids hidden padding), EMB.6 (layouts and hashes in
// read-only data), CDSA.32 (the layout is a named type with documented
// strides, transformed once at build time). A search of the performance
// corpus for GPU constant buffer layout and padding rules returned no
// applicable rule; recorded here rather than citing an unrelated rule.
namespace bw::shaders {

inline constexpr std::uint32_t kBlockAlignment = 16;
inline constexpr std::uint32_t kArrayElementAlignment = 16;
// One HLSL constant register row.
inline constexpr std::uint32_t kConstantRowBytes = 16;

// Value types. Alignment is part of the type so that a plain struct of them
// has the block layout: the compiler places each member at
// align_up(previous end, alignment). Float3 is 16-aligned, so a float3 member
// occupies 16 bytes and the next member starts after it. A struct with
// alignas has the same size and alignment as an aligned member and needs no
// per-member macro.
struct alignas(8) Float2 {
  float x{};
  float y{};
};
struct alignas(kBlockAlignment) Float3 {
  float x{};
  float y{};
  float z{};
};
struct alignas(kBlockAlignment) Float4 {
  float x{};
  float y{};
  float z{};
  float w{};
};
struct alignas(kBlockAlignment) Int4 {
  std::int32_t x{};
  std::int32_t y{};
  std::int32_t z{};
  std::int32_t w{};
};
struct alignas(kBlockAlignment) Uint4 {
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t z{};
  std::uint32_t w{};
};
// Row-major storage: rows[r] is HLSL row r at byte 16 * r. The tool passes
// -Zpr on every target (compiler_options.hpp), so the HLSL float4x4 reads the
// same bytes.
struct alignas(kBlockAlignment) Float4x4 {
  std::array<Float4, 4> rows{};
};
// Descriptor array indices (root_layout.hpp, DescriptorArray). In the
// generated HLSL each is a uint; the bindless-access include turns it into
// the resource of the array it names. A TextureIndex addresses the same slot
// in every read texture array; a BufferIndex a slot of the buffer array; the
// two storage indices address the two storage arrays, below
// kStorageDescriptors.
// Each occupies one 4-byte slot of the block.
struct TextureIndex {
  std::uint32_t value{};
};
struct BufferIndex {
  std::uint32_t value{};
};
struct SamplerIndex {
  std::uint32_t value{};
};
struct StorageTextureIndex {
  std::uint32_t value{};
};
struct StorageBufferIndex {
  std::uint32_t value{};
};
// Fixed-size array: every element on a 16-byte stride, as in HLSL (array
// alignment 16 and element size a multiple of 16). elements[i].value is
// element i.
template <class T, std::size_t N>
  requires(N > 0)
struct Array {
  struct alignas(kArrayElementAlignment) Element {
    T value{};
  };
  std::array<Element, N> elements{};
  [[nodiscard]] constexpr T& operator[](std::size_t index) noexcept {
    return elements[index].value;
  }
  [[nodiscard]] constexpr const T& operator[](std::size_t index) const noexcept {
    return elements[index].value;
  }
};

enum class ParameterKind : std::uint8_t {
  Float,
  Int32,
  Uint32,
  Float2,
  Float3,
  Float4,
  Int4,
  Uint4,
  Float4x4,
  TextureIndex,
  BufferIndex,
  SamplerIndex,
  StorageTextureIndex,
  StorageBufferIndex,
  Block,
};

struct LayoutHash {
  std::uint64_t value{};
  [[nodiscard]] friend constexpr bool operator==(LayoutHash, LayoutHash) noexcept = default;
};

struct BlockLayout;

// One member of a block. size is the C++ sizeof of the declared member;
// element_count is 0 for a single value and N for Array<T, N>; nested is set
// for kind Block only and observes a static layout.
struct MemberLayout {
  std::string_view name{};
  const BlockLayout* nested{};
  std::uint32_t offset{};
  std::uint32_t size{};
  std::uint32_t element_count{};
  ParameterKind kind{};
};

// Invariants, each stated for the tests: members are sorted by offset and
// dense as defined in the contract; size is sizeof(Block), a multiple of 16;
// hash is the FNV-1a 64 of the canonical sequence below.
struct BlockLayout {
  std::string_view name{};
  std::span<const MemberLayout> members{};
  LayoutHash hash{};
  std::uint32_t size{};
};

// Specialized by BW_PARAMETER_BLOCK_LAYOUT. Undefined for any other type.
template <class Block> struct BlockLayoutOf;

template <class T>
concept ParameterBlock = requires {
  { BlockLayoutOf<T>::value } -> std::convertible_to<const BlockLayout&>;
};

// Alignment and HLSL size per kind. A value's HLSL size is what DXC assigns
// (float3 is 12, a matrix row is 16); its C++ size is sizeof.
// Matrix size (rows - 1) * 16 + columns * 4, vector size columns * 4, array
// size (N - 1) * align16(element) + element.
[[nodiscard]] constexpr std::uint32_t kind_alignment(ParameterKind kind) noexcept;
[[nodiscard]] constexpr std::uint32_t kind_hlsl_size(ParameterKind kind,
                                                     const BlockLayout* nested) noexcept;
// Alignment of a member as declared: 16 for arrays and blocks, otherwise the
// kind's alignment.
[[nodiscard]] constexpr std::uint32_t member_alignment(const MemberLayout& member) noexcept;

// Where DXC places a member whose predecessor ended at `running`: scalars at
// the next multiple of 4, vectors at the next multiple of their alignment
// (a vector never crosses a 16-byte row), arrays, matrices and blocks at the
// next multiple of 16. offset >= running always. The generator pads so that
// offset == member.offset; that equality for every member of every declared
// block is the unit test, and the reflected offsets of the compiled shader
// are the integration test.
struct HlslPlacement {
  std::uint32_t offset{};
  std::uint32_t size{};
};
[[nodiscard]] constexpr HlslPlacement place_in_hlsl(const MemberLayout& member,
                                                    std::uint32_t running) noexcept;

// Layout hash: FNV-1a 64 (offset basis 0xcbf29ce484222325, prime
// 0x100000001b3) over, in order: the block name bytes; size as four
// little-endian bytes; then for each member in offset order its name bytes,
// kind as one byte, offset and element_count as four little-endian bytes
// each, and the nested block's hash as eight little-endian bytes (zero when
// not nested). Stable under reordering of the macro's member list, because
// members are sorted first; changed by any change of a member's name, kind,
// offset, count or nested layout, of the block name or of its size.
// FNV-1a keeps the hash constexpr with no dependency. Binding flags and
// static slot bits are not hashed: a block's slot is in the root layout hash
// (root_layout.hpp).
[[nodiscard]] constexpr LayoutHash
layout_hash(std::string_view name, std::uint32_t size,
            std::span<const MemberLayout> sorted_members) noexcept;

// Generated HLSL. append_hlsl_struct appends, for `layout` and first for each
// nested block not yet emitted in this call, exactly:
//   "struct BW_<name> {\n"
//   one line per member or padding, "  <type> <member>;\n" or
//     "  <type> <member>[<N>];\n" or "  uint bw_pad_<offset>;\n"
//   "};\n"
// where <type> is float, int, uint, float2, float3, float4, int4, uint4,
// float4x4, uint (for the five index kinds) or BW_<nested name>, and
// <offset> is the decimal HLSL offset the padding word fills. Padding is
// one 4-byte word at a time until the HLSL running offset equals the member's
// C++ offset, and after the last member until it equals the block size, so
// the HLSL struct and the C++ struct have the same size. The text has no
// comments and no other whitespace; it is an input of the artifact hash.
// A struct is emitted, not a cbuffer of globals, so nested blocks are nested
// types and the declaration line can be a ConstantBuffer<T> or an indexed
// array per target.
// Each names a layout that did not come from the macros; a layout that did
// never fails.
enum class HlslError : std::uint8_t {
  None,
  // A member's C++ offset is below where HLSL would place it.
  OffsetBelowHlslPlacement,
  // The last member's HLSL end lies past the block size.
  LayoutExceedsBlockSize,
  // A member of kind Block has no nested layout.
  MissingNestedLayout,
  // A member or block name is not an HLSL identifier.
  InvalidName,
};
// On any error `out` holds what was appended before the failing member; the
// caller discards it.
// member names what failed: the member for a member-level error, the block
// name for an invalid block name or a LayoutExceedsBlockSize with no member
// at fault; empty on None.
struct HlslResult {
  HlslError error{HlslError::None};
  std::string_view member{};
};
[[nodiscard]] HlslResult append_hlsl_struct(const BlockLayout& layout, std::string& out);

// The declaration line that binds a block, in terms of the two macros the
// bindless-access include defines per target:
//   static slot:  "BW_STATIC_BLOCK(BW_<name>, <name>, <slot>)\n"
//   per-draw:     "BW_DRAW_BLOCK(BW_<name>, <name>)\n"
// so the generated include is target-neutral and the target difference stays
// in that one include (invariant 1).
void append_hlsl_block_binding(const BlockLayout& layout, bool is_static_slot,
                               std::uint32_t static_slot, std::string& out);

// ---- traits and builders used by the macros -------------------------------

template <class T> struct ParameterTraits;
#define BW_SHADERS_SCALAR_TRAIT(Type, Kind)                                                        \
  template <> struct ParameterTraits<Type> {                                                       \
    static constexpr ParameterKind kind = ParameterKind::Kind;                                     \
  }
BW_SHADERS_SCALAR_TRAIT(float, Float);
BW_SHADERS_SCALAR_TRAIT(std::int32_t, Int32);
BW_SHADERS_SCALAR_TRAIT(std::uint32_t, Uint32);
BW_SHADERS_SCALAR_TRAIT(Float2, Float2);
BW_SHADERS_SCALAR_TRAIT(Float3, Float3);
BW_SHADERS_SCALAR_TRAIT(Float4, Float4);
BW_SHADERS_SCALAR_TRAIT(Int4, Int4);
BW_SHADERS_SCALAR_TRAIT(Uint4, Uint4);
BW_SHADERS_SCALAR_TRAIT(Float4x4, Float4x4);
BW_SHADERS_SCALAR_TRAIT(TextureIndex, TextureIndex);
BW_SHADERS_SCALAR_TRAIT(BufferIndex, BufferIndex);
BW_SHADERS_SCALAR_TRAIT(SamplerIndex, SamplerIndex);
BW_SHADERS_SCALAR_TRAIT(StorageTextureIndex, StorageTextureIndex);
BW_SHADERS_SCALAR_TRAIT(StorageBufferIndex, StorageBufferIndex);
#undef BW_SHADERS_SCALAR_TRAIT

template <class T>
concept ParameterValue = requires { ParameterTraits<T>::kind; };

namespace detail {
// Called only when a precondition fails during constant evaluation; it has
// no definition, so the evaluation is not constant and the compiler reports
// `what` in its diagnostic.
void layout_violation(const char* what);

struct Fnv1a64 {
  std::uint64_t state{0xcbf29ce484222325ULL};
  constexpr void append_byte(std::uint8_t byte) noexcept {
    state ^= byte;
    state *= 0x100000001b3ULL;
  }
  constexpr void append(std::string_view text) noexcept {
    for (const char c : text) {
      append_byte(static_cast<std::uint8_t>(c));
    }
  }
  constexpr void append_u32(std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32U; shift += 8U) {
      append_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
  constexpr void append_u64(std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64U; shift += 8U) {
      append_byte(static_cast<std::uint8_t>((value >> shift) & 0xFFU));
    }
  }
};

template <class T> struct MemberKind {
  static constexpr ParameterKind kind = ParameterTraits<T>::kind;
  static constexpr std::uint32_t element_count = 0;
  static constexpr const BlockLayout* nested = nullptr;
};
template <ParameterBlock T> struct MemberKind<T> {
  static constexpr ParameterKind kind = ParameterKind::Block;
  static constexpr std::uint32_t element_count = 0;
  static constexpr const BlockLayout* nested = &BlockLayoutOf<T>::value;
};
template <class T, std::size_t N> struct MemberKind<Array<T, N>> {
  static constexpr ParameterKind kind = MemberKind<T>::kind;
  static constexpr std::uint32_t element_count = static_cast<std::uint32_t>(N);
  static constexpr const BlockLayout* nested = MemberKind<T>::nested;
};

template <class T>
concept MemberType = ParameterValue<T> || ParameterBlock<T> ||
                     requires(T t) { []<class U, std::size_t N>(Array<U, N>) {}(t); };

template <MemberType T>
[[nodiscard]] constexpr MemberLayout describe_member(std::string_view name, std::size_t offset,
                                                     std::size_t size) noexcept {
  return MemberLayout{.name = name,
                      .nested = MemberKind<T>::nested,
                      .offset = static_cast<std::uint32_t>(offset),
                      .size = static_cast<std::uint32_t>(size),
                      .element_count = MemberKind<T>::element_count,
                      .kind = MemberKind<T>::kind};
}

// Sorts by offset (insertion sort; member counts are small) and checks the
// block preconditions. Any violation calls layout_violation.
template <class Block, std::same_as<MemberLayout>... Members>
[[nodiscard]] consteval std::array<MemberLayout, sizeof...(Members)>
sorted_members(Members... members) {
  static_assert(std::is_standard_layout_v<Block>, "a parameter block is standard layout");
  static_assert(std::is_trivially_copyable_v<Block>, "a parameter block is trivially copyable");
  static_assert(alignof(Block) == kBlockAlignment, "a parameter block is 16-byte aligned");
  static_assert(sizeof(Block) % kBlockAlignment == 0,
                "a parameter block is a multiple of 16 bytes");
  std::array<MemberLayout, sizeof...(Members)> sorted{members...};
  for (std::size_t i = 1; i < sorted.size(); ++i) {
    for (std::size_t j = i; j > 0 && sorted[j - 1].offset > sorted[j].offset; --j) {
      const MemberLayout tmp = sorted[j - 1];
      sorted[j - 1] = sorted[j];
      sorted[j] = tmp;
    }
  }
  std::uint32_t running = 0;
  for (std::size_t i = 0; i < sorted.size(); ++i) {
    const MemberLayout& member = sorted[i];
    if (member.name.empty()) {
      layout_violation("parameter block member has an empty name");
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (sorted[j].name == member.name) {
        layout_violation("parameter block member listed twice");
      }
    }
    if (member.offset != bw::memory::align_up(running, member_alignment(member))) {
      layout_violation("parameter block member is not dense: a member is unlisted, or a type "
                       "has the wrong alignment");
    }
    running = member.offset + member.size;
  }
  if (bw::memory::align_up(running, kBlockAlignment) != sizeof(Block)) {
    layout_violation("parameter block has trailing members that are not listed");
  }
  return sorted;
}

template <class Block, std::size_t N>
[[nodiscard]] consteval BlockLayout make_block_layout(std::string_view name,
                                                      const std::array<MemberLayout, N>& members) {
  if (name.empty()) {
    layout_violation("parameter block has an empty name");
  }
  const std::span<const MemberLayout> span{members};
  return BlockLayout{.name = name,
                     .members = span,
                     .hash = layout_hash(name, static_cast<std::uint32_t>(sizeof(Block)), span),
                     .size = static_cast<std::uint32_t>(sizeof(Block))};
}
} // namespace detail

// ---- constexpr definitions ------------------------------------------------

constexpr std::uint32_t kind_alignment(ParameterKind kind) noexcept {
  switch (kind) {
  case ParameterKind::Float:
  case ParameterKind::Int32:
  case ParameterKind::Uint32:
  case ParameterKind::TextureIndex:
  case ParameterKind::BufferIndex:
  case ParameterKind::SamplerIndex:
  case ParameterKind::StorageTextureIndex:
  case ParameterKind::StorageBufferIndex:
    return 4;
  case ParameterKind::Float2:
    return 8;
  case ParameterKind::Float3:
  case ParameterKind::Float4:
  case ParameterKind::Int4:
  case ParameterKind::Uint4:
  case ParameterKind::Float4x4:
  case ParameterKind::Block:
    return kBlockAlignment;
  }
  return kBlockAlignment;
}

constexpr std::uint32_t kind_hlsl_size(ParameterKind kind, const BlockLayout* nested) noexcept {
  switch (kind) {
  case ParameterKind::Float:
  case ParameterKind::Int32:
  case ParameterKind::Uint32:
  case ParameterKind::TextureIndex:
  case ParameterKind::BufferIndex:
  case ParameterKind::SamplerIndex:
  case ParameterKind::StorageTextureIndex:
  case ParameterKind::StorageBufferIndex:
    return 4;
  case ParameterKind::Float2:
    return 8;
  case ParameterKind::Float3:
    return 12;
  case ParameterKind::Float4:
  case ParameterKind::Int4:
  case ParameterKind::Uint4:
    return 16;
  case ParameterKind::Float4x4:
    return 64;
  case ParameterKind::Block:
    return nested == nullptr ? 0 : nested->size;
  }
  return 0;
}

constexpr std::uint32_t member_alignment(const MemberLayout& member) noexcept {
  if (member.element_count != 0 || member.kind == ParameterKind::Block) {
    return kArrayElementAlignment;
  }
  return kind_alignment(member.kind);
}

constexpr HlslPlacement place_in_hlsl(const MemberLayout& member, std::uint32_t running) noexcept {
  const std::uint32_t element = kind_hlsl_size(member.kind, member.nested);
  if (member.element_count != 0) {
    const std::uint32_t stride = bw::memory::align_up(element, kArrayElementAlignment);
    return HlslPlacement{.offset = bw::memory::align_up(running, kArrayElementAlignment),
                         .size = (member.element_count - 1U) * stride + element};
  }
  const std::uint32_t alignment = member_alignment(member);
  std::uint32_t offset = bw::memory::align_up(running, alignment);
  // A vector may not cross a 16-byte row.
  if (member.kind != ParameterKind::Block && element <= kConstantRowBytes &&
      (offset % kConstantRowBytes) + element > kConstantRowBytes) {
    offset = bw::memory::align_up(offset, kConstantRowBytes);
  }
  return HlslPlacement{.offset = offset, .size = element};
}

constexpr LayoutHash layout_hash(std::string_view name, std::uint32_t size,
                                 std::span<const MemberLayout> sorted_members) noexcept {
  detail::Fnv1a64 hash{};
  hash.append(name);
  hash.append_u32(size);
  for (const MemberLayout& member : sorted_members) {
    hash.append(member.name);
    hash.append_byte(static_cast<std::uint8_t>(member.kind));
    hash.append_u32(member.offset);
    hash.append_u32(member.element_count);
    hash.append_u64(member.nested == nullptr ? 0ULL : member.nested->hash.value);
  }
  return LayoutHash{hash.state};
}

} // namespace bw::shaders

// Records one member: its name, kind (from the declared type), offsetof,
// sizeof and element count. Block must be the complete block type.
// Core I.30: the macro is the one place offsetof and stringizing are used.
#define BW_PARAMETER_MEMBER(Block, member)                                                         \
  ::bw::shaders::detail::describe_member<decltype(Block::member)>(                                 \
      #member, offsetof(Block, member), sizeof(Block::member))

// Defines BlockLayoutOf<Block> from the BW_PARAMETER_MEMBER records of every
// member. Place it at global scope after the block's definition and after
// the layouts of any nested blocks. Example:
//   struct alignas(16) ViewBlock { Float4x4 world_to_clip; Float3 eye; float near{}; };
// Scalar members carry `{}` so the block is value-initialized like the value
// types; the project's member-init check requires it of every block.
//   BW_PARAMETER_BLOCK_LAYOUT(ViewBlock, BW_PARAMETER_MEMBER(ViewBlock, world_to_clip),
//                             BW_PARAMETER_MEMBER(ViewBlock, eye),
//                             BW_PARAMETER_MEMBER(ViewBlock, near));
#define BW_PARAMETER_BLOCK_LAYOUT(Block, ...)                                                      \
  template <> struct bw::shaders::BlockLayoutOf<Block> {                                           \
    static constexpr auto members = ::bw::shaders::detail::sorted_members<Block>(__VA_ARGS__);     \
    static constexpr ::bw::shaders::BlockLayout value =                                            \
        ::bw::shaders::detail::make_block_layout<Block>(#Block, members);                          \
  }
#endif
