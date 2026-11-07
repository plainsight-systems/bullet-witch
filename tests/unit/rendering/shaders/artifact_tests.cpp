// Unit test: public contracts of bw/rendering/shaders/artifact.hpp, one
// scenario per CTest entry. Each scenario names the header declaration it asserts.
// Byte offsets used to corrupt an image come from the header's byte format
// (artifact.hpp, kArtifactFormatVersion), which is a public contract.
#include "bw/rendering/shaders/artifact.hpp"

#include "bw/testing/harness.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fixture {
using bw::shaders::Float4;
using bw::shaders::Float4x4;
using bw::shaders::TextureIndex;
struct alignas(16) ViewBlock {
  Float4x4 world_to_clip{};
};
struct alignas(16) DrawParams {
  Float4 color{};
  TextureIndex texture{};
};
} // namespace fixture

using fixture::DrawParams;
using fixture::ViewBlock;
BW_PARAMETER_BLOCK_LAYOUT(ViewBlock, BW_PARAMETER_MEMBER(ViewBlock, world_to_clip));
BW_PARAMETER_BLOCK_LAYOUT(DrawParams, BW_PARAMETER_MEMBER(DrawParams, color),
                          BW_PARAMETER_MEMBER(DrawParams, texture));

namespace {
using bw::shaders::Artifact;
using bw::shaders::ArtifactError;
using bw::shaders::ArtifactHeader;
using bw::shaders::ArtifactLoadResult;
using bw::shaders::ArtifactWriteError;
using bw::shaders::BindingUse;
using bw::shaders::BlockLayout;
using bw::shaders::BlockLayoutOf;
using bw::shaders::BlockSlotKind;
using bw::shaders::DescriptorArray;
using bw::shaders::LayoutHash;
using bw::shaders::LoadError;
using bw::shaders::LoadExpectations;
using bw::shaders::PermutationId;
using bw::shaders::ReflectedBlock;
using bw::shaders::Reflection;
using bw::shaders::RootLayout;
using bw::shaders::ShaderStage;
using bw::shaders::ShaderTarget;
using bw::shaders::StaticSlot;
using bw::shaders::TrailerEntry;
using bw::shaders::TrailerKey;
using bw::shaders::VertexInput;

using bw::testing::require;

std::vector<std::byte> to_bytes(std::string_view text) {
  std::vector<std::byte> bytes;
  for (const char c : text) {
    bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  }
  return bytes;
}

std::uint32_t get_u32(std::span<const std::byte> bytes, std::size_t at) {
  std::uint32_t value = 0;
  for (unsigned i = 0; i < 4U; ++i) {
    value |= std::to_integer<std::uint32_t>(bytes[at + i]) << (8U * i);
  }
  return value;
}

void put_u32(std::vector<std::byte>& bytes, std::size_t at, std::uint32_t value) {
  for (unsigned i = 0; i < 4U; ++i) {
    bytes[at + i] = static_cast<std::byte>((value >> (8U * i)) & 0xFFU);
  }
}

constexpr std::uint32_t kViewSlot = 2;

constexpr std::uint32_t bit(DescriptorArray array) {
  return 1U << static_cast<std::uint32_t>(array);
}
constexpr std::uint32_t kFixtureArrays =
    bit(DescriptorArray::Sampler) | bit(DescriptorArray::Texture2D) | bit(DescriptorArray::Buffer);

// A complete, valid artifact's inputs. Not copyable: spans point at members.
class Fixture {
public:
  Fixture() {
    root.static_slots[kViewSlot] =
        StaticSlot{.block_name = "ViewBlock", .layout_hash = view().hash};
    header.shader_name = "fixture_shader";
    header.entry_point = "main";
    for (std::size_t i = 0; i < header.input_hash.size(); ++i) {
      header.input_hash[i] = static_cast<std::uint8_t>(i * 7U + 1U);
    }
    header.root_layout_hash = bw::shaders::root_layout_hash(root);
    header.permutation = PermutationId{1};
    header.target = ShaderTarget::Direct3D12;
    header.stage = ShaderStage::Vertex;
    reflection.blocks[0] = ReflectedBlock{.name = "ViewBlock",
                                          .layout_hash = view().hash,
                                          .slot = kViewSlot,
                                          .kind = BlockSlotKind::Static};
    reflection.blocks[1] = ReflectedBlock{
        .name = "DrawParams", .layout_hash = draw().hash, .slot = 0, .kind = BlockSlotKind::Draw};
    reflection.block_count = 2;
    reflection.vertex_inputs[0] =
        VertexInput{.semantic_name = "POSITION", .semantic_index = 0, .location = 0};
    reflection.vertex_inputs[1] =
        VertexInput{.semantic_name = "TEXCOORD", .semantic_index = 1, .location = 1};
    reflection.vertex_input_count = 2;
    reflection.root_constant_words = 4;
    reflection.descriptor_arrays = kFixtureArrays;
    trailer.push_back(TrailerEntry{.bytes = features, .key = TrailerKey::Features});
    trailer.push_back(TrailerEntry{.bytes = identity, .key = TrailerKey::CompilerIdentity});
  }
  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;
  Fixture(Fixture&&) = delete;
  Fixture& operator=(Fixture&&) = delete;
  ~Fixture() = default;

  static const BlockLayout& view() { return BlockLayoutOf<ViewBlock>::value; }
  static const BlockLayout& draw() { return BlockLayoutOf<DrawParams>::value; }

  [[nodiscard]] LoadExpectations expectations() const {
    return LoadExpectations{
        .root_layout = &root, .running_blocks = running, .permutation_count = 2};
  }
  [[nodiscard]] std::vector<std::byte> write() const {
    std::vector<std::byte> out;
    require(bw::shaders::write_artifact(header, reflection, code, trailer, out) ==
                ArtifactWriteError::None,
            "fixture writes");
    return out;
  }

  RootLayout root{};
  std::array<const BlockLayout*, 2> running{&view(), &draw()};
  ArtifactHeader header{};
  Reflection reflection{};
  // 13 bytes: the code block need not be a multiple of 4.
  std::vector<std::byte> code = to_bytes("DXBC-code-123");
  std::array<std::byte, 4> features{std::byte{3}, std::byte{0}, std::byte{0}, std::byte{0}};
  std::vector<std::byte> identity = to_bytes("dxc 1.9.2602");
  std::vector<TrailerEntry> trailer;
};

bool same_bytes(std::span<const std::byte> a, std::span<const std::byte> b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i] != b[i]) {
      return false;
    }
  }
  return true;
}

// (artifact.hpp, write_artifact), (artifact.hpp, load_artifact) and the byte
// format (artifact.hpp, kArtifactFormatVersion): write then load returns equal
// header, reflection (blocks in (kind, slot) order), code and trailer entries.
void write_then_load_round_trip() {
  Fixture f;
  // A buffer block, read from the buffer array, after the bound ones.
  f.reflection.blocks[2] = ReflectedBlock{
      .name = "DrawParams", .layout_hash = f.draw().hash, .slot = 0, .kind = BlockSlotKind::Buffer};
  f.reflection.block_count = 3;
  const std::vector<std::byte> bytes = f.write();
  const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, f.expectations());
  require(result.ok(), "a written artifact loads");
  const Artifact& artifact = result.artifact();
  require(artifact.header.shader_name == f.header.shader_name, "shader name");
  require(artifact.header.entry_point == f.header.entry_point, "entry point");
  require(artifact.header.input_hash == f.header.input_hash, "input hash");
  require(artifact.header.root_layout_hash == f.header.root_layout_hash, "root layout hash");
  require(artifact.header.permutation == f.header.permutation, "permutation");
  require(artifact.header.target == f.header.target, "target");
  require(artifact.header.stage == f.header.stage, "stage");
  const Reflection& r = artifact.reflection;
  require(r.block_count == 3U, "block count");
  require(r.blocks[0].name == "ViewBlock" && r.blocks[0].kind == BlockSlotKind::Static &&
              r.blocks[0].slot == kViewSlot && r.blocks[0].layout_hash == f.view().hash,
          "static block first");
  require(r.blocks[1].name == "DrawParams" && r.blocks[1].kind == BlockSlotKind::Draw &&
              r.blocks[1].slot == 0U && r.blocks[1].layout_hash == f.draw().hash,
          "draw block after static blocks");
  require(r.blocks[2].name == "DrawParams" && r.blocks[2].kind == BlockSlotKind::Buffer &&
              r.blocks[2].slot == 0U && r.blocks[2].layout_hash == f.draw().hash,
          "buffer block after the bound blocks");
  require(r.vertex_input_count == 2U && r.vertex_inputs[0].semantic_name == "POSITION" &&
              r.vertex_inputs[0].location == 0U && r.vertex_inputs[1].semantic_name == "TEXCOORD" &&
              r.vertex_inputs[1].semantic_index == 1U && r.vertex_inputs[1].location == 1U,
          "vertex inputs");
  require(r.root_constant_words == 4U && r.descriptor_arrays == kFixtureArrays,
          "descriptor arrays and root constants");
  require(r.thread_group_size == std::array<std::uint32_t, 3>{}, "thread group zero for vertex");
  require(same_bytes(artifact.code, f.code), "code bytes");
  const auto features = bw::shaders::find_trailer_entry(artifact.trailer, TrailerKey::Features);
  const auto identity =
      bw::shaders::find_trailer_entry(artifact.trailer, TrailerKey::CompilerIdentity);
  require(features.has_value() && same_bytes(*features, f.features), "features entry");
  require(identity.has_value() && same_bytes(*identity, f.identity), "compiler identity entry");
  require(get_u32(bytes, 8) == bytes.size(), "total_size is the image size");
}

// (artifact.hpp, write_artifact): blocks and inputs given in another order
// give identical bytes; write_artifact replaces the output's contents.
void identical_bytes_for_equal_inputs() {
  const Fixture f;
  const std::vector<std::byte> first = f.write();
  Fixture g;
  std::swap(g.reflection.blocks[0], g.reflection.blocks[1]);
  std::swap(g.reflection.vertex_inputs[0], g.reflection.vertex_inputs[1]);
  std::vector<std::byte> second = to_bytes("stale contents");
  require(bw::shaders::write_artifact(g.header, g.reflection, g.code, g.trailer, second) ==
              ArtifactWriteError::None,
          "reordered reflection writes");
  require(same_bytes(first, second), "identical bytes for equal inputs");
}

void expect_error(std::span<const std::byte> bytes, const LoadExpectations& expectations,
                  ArtifactError code, std::string_view what) {
  const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, expectations);
  require(!result.ok() && result.error().code == code, what);
}

// (artifact.hpp, load_artifact) (the numbered checks), (artifact.hpp, ArtifactError)
// and (artifact.hpp, LoadError) (codes, compared values): one corrupted image per code and per site
// where a code arises at two checks (Truncated at 1 and 7, MalformedHeader at 6, 7 and 8), each
// with a single fault, plus one image with two faults showing the stated order.
void named_failures() {
  const Fixture f;
  const std::vector<std::byte> valid = f.write();
  const LoadExpectations expect = f.expectations();
  const std::uint32_t header_size = get_u32(valid, 12);
  const std::uint32_t reflection_size = get_u32(valid, 64);
  const std::uint32_t code_size = get_u32(valid, 68);
  const auto total = static_cast<std::uint32_t>(valid.size());
  require(bw::shaders::load_artifact(valid, expect).ok(), "the unmodified image loads");

  // Check 1, Truncated: short of the fixed header's 76 bytes.
  const std::span<const std::byte> short_header = std::span{valid}.first(75);
  const ArtifactLoadResult too_short = bw::shaders::load_artifact(short_header, expect);
  require(!too_short.ok() && too_short.error().code == ArtifactError::Truncated &&
              too_short.error().expected == 76U && too_short.error().actual == 75U,
          "Truncated: fixed header (check 1)");
  // Check 7, Truncated: cut at each section boundary with total_size
  // rewritten to the cut, so a declared section runs past the end.
  for (const std::uint32_t cut : {header_size - 2U, header_size, header_size + reflection_size,
                                  header_size + reflection_size + code_size, total - 2U}) {
    std::vector<std::byte> bytes(valid.begin(), valid.begin() + cut);
    put_u32(bytes, 8, cut);
    const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, expect);
    require(!result.ok() && result.error().code == ArtifactError::Truncated &&
                result.error().expected == total && result.error().actual == cut,
            "Truncated: a section runs past the end (check 7)");
  }
  {
    // Check 2.
    std::vector<std::byte> bytes = valid;
    bytes[0] = std::byte{'X'};
    expect_error(bytes, expect, ArtifactError::BadMagic, "BadMagic (check 2)");
  }
  {
    // Check 3.
    std::vector<std::byte> bytes = valid;
    put_u32(bytes, 4, bw::shaders::kArtifactFormatVersion + 1U);
    const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, expect);
    require(!result.ok() && result.error().code == ArtifactError::UnsupportedVersion &&
                result.error().expected == bw::shaders::kArtifactFormatVersion &&
                result.error().actual == bw::shaders::kArtifactFormatVersion + 1U,
            "UnsupportedVersion (check 3)");
  }
  {
    // Check 4: bytes cut off without rewriting total_size, and one extra byte.
    const ArtifactLoadResult cut =
        bw::shaders::load_artifact(std::span{valid}.first(total - 1U), expect);
    require(!cut.ok() && cut.error().code == ArtifactError::SizeMismatch &&
                cut.error().expected == total && cut.error().actual == total - 1U,
            "SizeMismatch: short span (check 4)");
    std::vector<std::byte> longer = valid;
    longer.push_back(std::byte{0});
    expect_error(longer, expect, ArtifactError::SizeMismatch, "SizeMismatch: long span (check 4)");
  }
  {
    // Check 5.
    std::vector<std::byte> bytes = valid;
    bytes[16] = std::byte{9};
    expect_error(bytes, expect, ArtifactError::UnknownTarget, "UnknownTarget (check 5)");
  }
  {
    // Check 5.
    std::vector<std::byte> bytes = valid;
    bytes[17] = std::byte{7};
    expect_error(bytes, expect, ArtifactError::UnknownStage, "UnknownStage (check 5)");
  }
  {
    // Check 6: a reserved field non-zero.
    std::vector<std::byte> bytes = valid;
    bytes[18] = std::byte{1};
    expect_error(bytes, expect, ArtifactError::MalformedHeader,
                 "MalformedHeader: reserved field (check 6)");
  }
  {
    // Check 6: code_size zero, the other sizes unchanged.
    std::vector<std::byte> bytes = valid;
    put_u32(bytes, 68, 0);
    expect_error(bytes, expect, ArtifactError::MalformedHeader,
                 "MalformedHeader: code_size zero (check 6)");
  }
  {
    // Check 7: the sections fall short of total_size (four bytes appended and
    // total_size rewritten to the longer span).
    std::vector<std::byte> bytes = valid;
    bytes.insert(bytes.end(), 4, std::byte{0});
    put_u32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
    expect_error(bytes, expect, ArtifactError::MalformedHeader,
                 "MalformedHeader: sections fall short (check 7)");
  }
  {
    // Check 8: the shader name's length runs past header_size.
    std::vector<std::byte> bytes = valid;
    put_u32(bytes, 76, header_size);
    expect_error(bytes, expect, ArtifactError::MalformedHeader,
                 "MalformedHeader: header string past header_size (check 8)");
  }
  {
    // Check 9: a descriptor_arrays bit at kDescriptorArrayCount (the u32 at
    // the start of the reflection section).
    std::vector<std::byte> bytes = valid;
    put_u32(bytes, header_size, kFixtureArrays | (1U << bw::shaders::kDescriptorArrayCount));
    expect_error(bytes, expect, ArtifactError::MalformedReflection,
                 "MalformedReflection: unknown descriptor array bit (check 9)");
  }
  {
    // Check 10.
    std::vector<std::byte> bytes = valid;
    put_u32(bytes, bytes.size() - 4U, get_u32(valid, 72) + 1U);
    expect_error(bytes, expect, ArtifactError::TrailerCorrupt, "TrailerCorrupt (check 10)");
  }
  {
    // Check 11.
    LoadExpectations narrow = expect;
    narrow.permutation_count = 1;
    expect_error(valid, narrow, ArtifactError::PermutationOutOfRange,
                 "PermutationOutOfRange (check 11)");
  }
  {
    // Check 11 skipped: permutation_count 0 means the caller has no
    // description (artifact.hpp, LoadExpectations), so an id past any domain loads.
    Fixture far;
    far.header.permutation = PermutationId{5000};
    const std::vector<std::byte> bytes = far.write();
    LoadExpectations no_count = far.expectations();
    no_count.permutation_count = 0;
    const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, no_count);
    require(result.ok() && result.artifact().header.permutation == PermutationId{5000},
            "permutation_count 0 skips the range check (check 11)");
  }
  {
    // Check 12: a different layout, and a null layout (expected 0).
    RootLayout other = f.root;
    other.root_constant_words = 8;
    LoadExpectations changed = expect;
    changed.root_layout = &other;
    const ArtifactLoadResult result = bw::shaders::load_artifact(valid, changed);
    require(!result.ok() && result.error().code == ArtifactError::RootLayoutMismatch &&
                result.error().expected == bw::shaders::root_layout_hash(other).value &&
                result.error().actual == f.header.root_layout_hash.value,
            "RootLayoutMismatch (check 12)");
    LoadExpectations null_layout = expect;
    null_layout.root_layout = nullptr;
    const ArtifactLoadResult null_result = bw::shaders::load_artifact(valid, null_layout);
    require(!null_result.ok() && null_result.error().code == ArtifactError::RootLayoutMismatch &&
                null_result.error().expected == 0U,
            "RootLayoutMismatch: null layout, expected 0 (check 12)");
  }
  {
    // Check 13.
    const std::array<const BlockLayout*, 1> only_view{&Fixture::view()};
    LoadExpectations missing = expect;
    missing.running_blocks = only_view;
    const ArtifactLoadResult result = bw::shaders::load_artifact(valid, missing);
    require(!result.ok() && result.error().code == ArtifactError::UnknownBlock &&
                result.error().block_name == "DrawParams",
            "UnknownBlock (check 13)");
  }
  {
    // Check 14.
    const BlockLayout changed_draw{.name = "DrawParams",
                                   .members = Fixture::draw().members,
                                   .hash = LayoutHash{Fixture::draw().hash.value ^ 1U},
                                   .size = Fixture::draw().size};
    const std::array<const BlockLayout*, 2> running{&Fixture::view(), &changed_draw};
    LoadExpectations changed = expect;
    changed.running_blocks = running;
    const ArtifactLoadResult result = bw::shaders::load_artifact(valid, changed);
    require(!result.ok() && result.error().code == ArtifactError::LayoutHashMismatch &&
                result.error().block_name == "DrawParams" &&
                result.error().expected == changed_draw.hash.value &&
                result.error().actual == Fixture::draw().hash.value,
            "LayoutHashMismatch (check 14)");
  }
  {
    // Check 15: the root layout places ViewBlock in slot 5; the artifact says
    // slot 2 but was built against that layout's hash.
    Fixture moved;
    moved.root.static_slots[kViewSlot] = StaticSlot{};
    moved.root.static_slots[5] =
        StaticSlot{.block_name = "ViewBlock", .layout_hash = Fixture::view().hash};
    moved.header.root_layout_hash = bw::shaders::root_layout_hash(moved.root);
    const std::vector<std::byte> bytes = moved.write();
    const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, moved.expectations());
    require(!result.ok() && result.error().code == ArtifactError::SlotMismatch &&
                result.error().block_name == "ViewBlock" && result.error().expected == 5U &&
                result.error().actual == kViewSlot,
            "SlotMismatch (check 15)");
  }
  {
    // Check 15: the root layout has no slot for ViewBlock; expected is
    // kMaxStaticBlocks.
    Fixture unplaced;
    unplaced.root.static_slots[kViewSlot] = StaticSlot{};
    unplaced.header.root_layout_hash = bw::shaders::root_layout_hash(unplaced.root);
    const std::vector<std::byte> bytes = unplaced.write();
    const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, unplaced.expectations());
    require(!result.ok() && result.error().code == ArtifactError::SlotMismatch &&
                result.error().expected == bw::shaders::kMaxStaticBlocks,
            "SlotMismatch: unplaced block (check 15)");
  }
  {
    // Two faults, wrong magic (check 2) and wrong version (check 3): the
    // earlier check is reported.
    std::vector<std::byte> bytes = valid;
    bytes[0] = std::byte{'X'};
    put_u32(bytes, 4, 2);
    expect_error(bytes, expect, ArtifactError::BadMagic, "check order: BadMagic before version");
  }
}

ArtifactWriteError write_with(const Fixture& f) {
  std::vector<std::byte> out;
  return bw::shaders::write_artifact(f.header, f.reflection, f.code, f.trailer, out);
}

// (artifact.hpp, ArtifactWriteError): each named write failure, one fault per
// input, and a thread group accepted on a compute stage
// (artifact.hpp, ArtifactWriteError::ThreadGroupOnNonCompute). ImageTooLarge and
// TrailerEntryTooLarge are not exercised: each needs a code block, trailer entry or image
// over 4 GiB, and constructing a span of that size over a smaller buffer would break
// std::span's precondition.
void write_errors_named() {
  {
    Fixture f;
    // A value that is not an enumerator, which the contract names; the enum
    // has a fixed underlying type, so the value is representable.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    f.header.target = static_cast<ShaderTarget>(9);
    require(write_with(f) == ArtifactWriteError::InvalidTargetOrStage,
            "InvalidTargetOrStage: target");
  }
  {
    Fixture f;
    // A value that is not an enumerator, which the contract names; the enum
    // has a fixed underlying type, so the value is representable.
    // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
    f.header.stage = static_cast<ShaderStage>(7);
    require(write_with(f) == ArtifactWriteError::InvalidTargetOrStage,
            "InvalidTargetOrStage: stage");
  }
  {
    Fixture f;
    const std::string long_name(bw::shaders::kMaxArtifactString + 1U, 'n');
    f.header.shader_name = long_name;
    require(write_with(f) == ArtifactWriteError::StringTooLong, "StringTooLong: header");
  }
  {
    Fixture f;
    const std::string long_name(bw::shaders::kMaxArtifactString + 1U, 's');
    f.reflection.vertex_inputs[0].semantic_name = long_name;
    require(write_with(f) == ArtifactWriteError::StringTooLong, "StringTooLong: semantic");
  }
  {
    Fixture f;
    f.reflection.block_count = bw::shaders::kMaxReflectedBlocks + 1U;
    require(write_with(f) == ArtifactWriteError::TooManyBlocks, "TooManyBlocks");
  }
  {
    Fixture f;
    f.reflection.vertex_input_count = bw::shaders::kMaxVertexInputs + 1U;
    require(write_with(f) == ArtifactWriteError::TooManyVertexInputs, "TooManyVertexInputs");
  }
  {
    Fixture f;
    f.reflection.blocks[0].slot = bw::shaders::kMaxStaticBlocks;
    require(write_with(f) == ArtifactWriteError::SlotOutOfRange, "SlotOutOfRange");
  }
  {
    Fixture f;
    f.reflection.blocks[1] = f.reflection.blocks[0];
    f.reflection.blocks[1].name = "OtherBlock";
    require(write_with(f) == ArtifactWriteError::DuplicateSlot, "DuplicateSlot");
  }
  {
    Fixture f;
    f.reflection.thread_group_size[0] = 8;
    require(write_with(f) == ArtifactWriteError::ThreadGroupOnNonCompute,
            "ThreadGroupOnNonCompute");
  }
  {
    Fixture f;
    f.reflection.descriptor_arrays = 1U << bw::shaders::kDescriptorArrayCount;
    require(write_with(f) == ArtifactWriteError::UnknownDescriptorArray, "UnknownDescriptorArray");
  }
  {
    Fixture f;
    std::vector<std::byte> out;
    require(bw::shaders::write_artifact(f.header, f.reflection, {}, f.trailer, out) ==
                ArtifactWriteError::EmptyCode,
            "EmptyCode");
  }
  {
    // A compute shader may carry a thread group size.
    Fixture f;
    f.header.stage = ShaderStage::Compute;
    f.reflection.vertex_input_count = 0;
    f.reflection.thread_group_size = {8, 8, 1};
    require(write_with(f) == ArtifactWriteError::None, "thread group on compute writes");
  }
}

// (artifact.hpp, TrailerKey): an unknown trailer key, as a later tool would
// write it, is kept and ignored. The entry is spliced into a written image
// ahead of the known entries, and the size fields are rewritten as the byte
// format states (artifact.hpp, kArtifactFormatVersion).
// (artifact.hpp, Artifact::trailer): it spans the whole trailer section,
// closing length included, so its size equals the header's trailer_size.
// (artifact.hpp, find_trailer_entry): a malformed trailer passed to it yields
// no value.
void unknown_trailer_key_ignored() {
  Fixture f;
  f.trailer.resize(1); // Features only.
  const std::vector<std::byte> written = f.write();
  const std::uint32_t old_trailer = get_u32(written, 72);
  const std::size_t trailer_at = written.size() - old_trailer;
  const std::vector<std::byte> payload = to_bytes("future");
  std::vector<std::byte> entry{std::byte{'q'}, std::byte{0}, std::byte{0}, std::byte{0},
                               std::byte{0}};
  put_u32(entry, 1, static_cast<std::uint32_t>(payload.size()));
  entry.insert(entry.end(), payload.begin(), payload.end());
  std::vector<std::byte> bytes(written.begin(),
                               written.begin() + static_cast<std::ptrdiff_t>(trailer_at));
  bytes.insert(bytes.end(), entry.begin(), entry.end());
  bytes.insert(bytes.end(), written.begin() + static_cast<std::ptrdiff_t>(trailer_at),
               written.end());
  const auto new_trailer = static_cast<std::uint32_t>(old_trailer + entry.size());
  put_u32(bytes, 8, static_cast<std::uint32_t>(bytes.size()));
  put_u32(bytes, 72, new_trailer);
  put_u32(bytes, bytes.size() - 4U, new_trailer);

  const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, f.expectations());
  require(result.ok(), "an unknown key does not fail the load");
  const std::span<const std::byte> trailer = result.artifact().trailer;
  require(trailer.size() == new_trailer && same_bytes(trailer.first(entry.size()), entry),
          "the unknown entry is kept in the trailer");
  const auto features = bw::shaders::find_trailer_entry(trailer, TrailerKey::Features);
  require(features.has_value() && same_bytes(*features, f.features),
          "a known key after it is found");
  require(!bw::shaders::find_trailer_entry(trailer, TrailerKey::CompilerIdentity).has_value(),
          "an absent key has no value");

  // (artifact.hpp, find_trailer_entry): a malformed trailer yields no value, even for a key
  // whose entry is present. A well-formed one-entry trailer is the baseline.
  std::vector<std::byte> good{std::byte{'x'}, std::byte{0}, std::byte{0}, std::byte{0},
                              std::byte{0},   std::byte{3}, std::byte{0}, std::byte{0},
                              std::byte{0},   std::byte{0}, std::byte{0}, std::byte{0},
                              std::byte{0}};
  put_u32(good, 1, 4);
  put_u32(good, 9, static_cast<std::uint32_t>(good.size()));
  require(bw::shaders::find_trailer_entry(good, TrailerKey::Features).has_value(),
          "baseline trailer finds its entry");
  std::vector<std::byte> bad_closer = good;
  put_u32(bad_closer, 9, static_cast<std::uint32_t>(good.size() + 1U));
  require(!bw::shaders::find_trailer_entry(bad_closer, TrailerKey::Features).has_value(),
          "a bad closing length yields no value");
  std::vector<std::byte> past_end = good;
  put_u32(past_end, 1, 100);
  require(!bw::shaders::find_trailer_entry(past_end, TrailerKey::Features).has_value(),
          "an entry running past the end yields no value");
  const std::array<std::byte, 2> too_short{std::byte{0}, std::byte{0}};
  require(!bw::shaders::find_trailer_entry(too_short, TrailerKey::Features).has_value(),
          "a trailer shorter than its closing length yields no value");
}

// (artifact.hpp, derive_binding_use) and (root_layout.hpp, BindingUse): the same reflection gives
// the same BindingUse whatever the target, with the descriptor array mask.
void binding_use_same_for_same_reflection() {
  Fixture f;
  const BindingUse direct = bw::shaders::derive_binding_use(f.reflection);
  require(direct.static_slot_mask == (1U << kViewSlot), "static slot bit");
  require(direct.uses_draw_block && direct.draw_block_hash == Fixture::draw().hash,
          "draw block use and hash");
  require(direct.root_constant_words == 4U && direct.descriptor_arrays == kFixtureArrays,
          "root constants and descriptor arrays");
  // (artifact.hpp, BlockSlotKind::Buffer): derive_binding_use ignores a
  // buffer block.
  Fixture with_buffer;
  with_buffer.reflection.blocks[2] = ReflectedBlock{
      .name = "ViewBlock", .layout_hash = f.view().hash, .slot = 2, .kind = BlockSlotKind::Buffer};
  with_buffer.reflection.block_count = 3;
  require(bw::shaders::derive_binding_use(with_buffer.reflection) == direct,
          "a buffer block sets nothing");
  for (const ShaderTarget target :
       {ShaderTarget::Direct3D12, ShaderTarget::Vulkan, ShaderTarget::Metal}) {
    f.header.target = target;
    const std::vector<std::byte> bytes = f.write();
    const ArtifactLoadResult result = bw::shaders::load_artifact(bytes, f.expectations());
    require(result.ok(), "loads on each target");
    require(bw::shaders::derive_binding_use(result.artifact().reflection) == direct,
            "same binding use on every target");
  }
}

// (artifact.hpp, artifact_file_name) and (artifact.hpp, target_directory):
// "<shader>.<permutation>.<suffix>.bwsh" and the target directory names.
void file_name_convention() {
  require(bw::shaders::artifact_file_name("fullscreen_triangle", PermutationId{1},
                                          ShaderStage::Vertex) == "fullscreen_triangle.1.vs.bwsh",
          "vertex file name");
  require(bw::shaders::artifact_file_name("blur", PermutationId{1023}, ShaderStage::Compute) ==
              "blur.1023.cs.bwsh",
          "compute file name");
  require(bw::shaders::stage_suffix(ShaderStage::Pixel) == "ps", "pixel suffix");
  require(bw::shaders::target_directory(ShaderTarget::Direct3D12) == "d3d12" &&
              bw::shaders::target_directory(ShaderTarget::Vulkan) == "vulkan" &&
              bw::shaders::target_directory(ShaderTarget::Metal) == "metal",
          "target directories");
}

using Scenario = bw::testing::Scenario<void>;
constexpr std::array scenarios{
    Scenario{"write_then_load_round_trip", write_then_load_round_trip},
    Scenario{"identical_bytes_for_equal_inputs", identical_bytes_for_equal_inputs},
    Scenario{"named_failures", named_failures},
    Scenario{"write_errors_named", write_errors_named},
    Scenario{"unknown_trailer_key_ignored", unknown_trailer_key_ignored},
    Scenario{"binding_use_same_for_same_reflection", binding_use_same_for_same_reflection},
    Scenario{"file_name_convention", file_name_convention},
};
} // namespace

int main(int argc, char** argv) { return bw::testing::run_scenario(argc, argv, scenarios); }
