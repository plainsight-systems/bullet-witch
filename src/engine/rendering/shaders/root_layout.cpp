#include "bw/rendering/shaders/root_layout.hpp"

#include "bw/rendering/scene/fixture_light.hpp"
#include "bw/rendering/scene/view.hpp"
#include "bw/rendering/shaders/parameter_block.hpp"

#include <array>
#include <charconv>
#include <cstdint>
#include <string>
#include <string_view>
#include <system_error>

// Responsibility: the engine's instance of the fixed root layout and the HLSL
// root signature text generator declared in
// bw/rendering/shaders/root_layout.hpp.
//
// Contract:
//   - engine_root_layout() returns one immutable RootLayout with static
//     storage duration: the View block in slot scene::kViewSlot, the
//     FixtureLight block in slot scene::kFixtureLightSlot, each with its
//     block's layout hash, every other slot empty, and root_constant_words
//     kRootConstantWords. A renderer slice
//     that adds a static block edits this definition and nothing else; the
//     root layout hash in every artifact then changes, so artifacts built
//     against the old table fail to load with RootLayoutMismatch instead of
//     binding against the wrong slots.
//   - append_root_signature_text appends exactly the text the header states,
//     root parameters in the header's order: SRV table, sampler
//     table, UAV table, draw CBV, static CBVs b0-b7, root constants; every
//     root CBV carries flags=DATA_STATIC; parameters and table ranges joined
//     by ", ", no newline. The only input
//     taken from the layout is root_constant_words: all kMaxStaticBlocks root
//     CBVs are emitted whether or not a slot is named, and the descriptor
//     tables come from placement(), so the text and the placement table cannot
//     disagree. It cannot fail; it only appends.
//
// Algorithm, root signature:
//   1. Root flags.
//   2. One descriptor table per table element in the order ResourceTable,
//      SamplerTable, StorageTable; within a table, one range per
//      DescriptorArray in enumerator order, each at register 0 of its space,
//      offset 0, numDescriptors "unbounded" when placement().max_descriptors
//      is 0, with the tier-2 range flags of its table.
//   3. The draw block CBV (b0, kDrawBlockSpace), then the eight static CBVs
//      (b0-b7, kStaticBlockSpace), each flags=DATA_STATIC.
//   4. The root constants (b0, kRootConstantSpace).
//
// Design and cost: the layout is a function-local constexpr object, so there
// is no initialization order or synchronization question and no runtime work.
// The text generator runs in the build tool only and appends to the caller's
// string.
//
// Retrieved rules: E.27 (read for the module's error style; nothing here can
// fail, so no error channel is invented), SL.str.2 (fixed text pieces are
// string_views). Performance: EMB.6 (the layout and placement table are
// compile-time data; the text is derived from them), MEM.9 (the only
// allocation is the caller's string, at build time).
namespace bw::shaders {
namespace {

void append_decimal(std::uint32_t value, std::string& out) {
  // to_chars is locale-independent; the text is an artifact hash input.
  std::array<char, 16> buffer{};
  const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  // A uint32 always fits 16 characters; the check keeps the contract visible.
  if (error == std::errc{}) {
    out.append(buffer.data(), end);
  }
}

// The range keyword and flags of a table element, in root parameter order
// (step 2).
struct TableText {
  RootElement table{};
  std::string_view range{};
  std::string_view flags{};
};
constexpr std::array kTables{
    TableText{RootElement::ResourceTable, "SRV",
              "DESCRIPTORS_VOLATILE|DATA_STATIC_WHILE_SET_AT_EXECUTE"},
    TableText{RootElement::SamplerTable, "Sampler", "DESCRIPTORS_VOLATILE"},
    TableText{RootElement::StorageTable, "UAV", "DESCRIPTORS_VOLATILE|DATA_VOLATILE"},
};

// One root CBV descriptor (step 3).
void append_root_cbv(std::uint32_t reg, std::uint32_t space, std::string& out) {
  out += ", CBV(b";
  append_decimal(reg, out);
  out += ", space=";
  append_decimal(space, out);
  out += ", flags=DATA_STATIC)";
}

void append_table(const TableText& table, std::string& out) {
  out += "DescriptorTable(";
  bool first = true;
  for (std::uint32_t index = 0; index < kDescriptorArrayCount; ++index) {
    const DescriptorArrayPlacement where = placement(static_cast<DescriptorArray>(index));
    if (where.table != table.table) {
      continue;
    }
    if (!first) {
      out += ", ";
    }
    first = false;
    out += table.range;
    out += '(';
    out += where.register_class;
    out += "0, space=";
    append_decimal(where.register_space, out);
    out += ", numDescriptors=";
    if (where.max_descriptors == 0U) {
      out += "unbounded";
    } else {
      append_decimal(where.max_descriptors, out);
    }
    out += ", offset=0, flags=";
    out += table.flags;
    out += ')';
  }
  out += ')';
}

} // namespace

namespace {

// Each slot names its block and carries the block's own hash, so the tool's
// check of a static block against its slot (StaticSlotHashMismatch) holds
// by construction.
template <class Block> constexpr StaticSlot slot_of() noexcept {
  return StaticSlot{BlockLayoutOf<Block>::value.name, BlockLayoutOf<Block>::value.hash};
}

constexpr RootLayout make_engine_root_layout() noexcept {
  RootLayout layout{};
  layout.static_slots[scene::kViewSlot] = slot_of<View>();
  layout.static_slots[scene::kFixtureLightSlot] = slot_of<FixtureLight>();
  return layout;
}

} // namespace

const RootLayout& engine_root_layout() noexcept {
  static constexpr RootLayout layout = make_engine_root_layout();
  return layout;
}

void append_root_signature_text(const RootLayout& layout, std::string& out) {
  // Step 1: flags.
  out += "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT)";
  // Step 2: the three descriptor tables.
  for (const TableText& table : kTables) {
    out += ", ";
    append_table(table, out);
  }
  // Step 3: draw block, then static blocks.
  append_root_cbv(0, kDrawBlockSpace, out);
  for (std::uint32_t slot = 0; slot < kMaxStaticBlocks; ++slot) {
    append_root_cbv(slot, kStaticBlockSpace, out);
  }
  // Step 4: root constants.
  out += ", RootConstants(num32BitConstants=";
  append_decimal(layout.root_constant_words, out);
  out += ", b0, space=";
  append_decimal(kRootConstantSpace, out);
  out += ')';
}

} // namespace bw::shaders
