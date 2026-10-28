// Prepare source behavior metadata once; execution never reinterprets wrapper
// pointers. Validate bounded chunks before resolving preorder links and named
// data, then publish the original Lua view. Core ES.103 (#res-overflow) uses
// wide/subtraction-first range checks. Performance CDSA.32
// (guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md)
// stores the actual traversal indexes once for shared immutable consumers.
// Both implementation corpora were queried/read; header owns full contracts.
#include "bw/content/formats/behavior_tree.hpp"

#include <bit>

#include "bw/content/formats/binary_reader.hpp"

namespace bw::formats {
namespace {
using resources::detail::malformed;
using resources::detail::Reader;
using Bytes = std::span<const std::uint8_t>;
constexpr std::size_t kChunkHeader = 8, kNodeBytes = 12, kWordBytes = 4;
constexpr std::uint32_t kNoParent = 0xffffffffU;
constexpr std::string_view kRootTag{"ai\0\0", 4};
constexpr std::string_view kStringTag{"str\0", 4};
constexpr std::string_view kNodeTag{"dag\0", 4};
constexpr std::size_t kMinimumWork = 0x44;

Bytes payload(Bytes input, std::size_t position) {
  const Reader reader{input};
  reader.range(position, kChunkHeader);
  const auto size = reader.u32(position + kWordBytes);
  const auto begin = position + kChunkHeader;
  reader.range(begin, size);
  return input.subspan(begin, size);
}
Bytes chunk(Bytes input, std::string_view tag) {
  const Reader reader{input};
  std::optional<Bytes> found;
  for (std::size_t position = 0; position < input.size();) {
    const auto part = payload(input, position);
    if (reader.tag_view(position) == tag) {
      if (found) malformed("Duplicate behavior resource chunk");
      found = part;
    }
    position += kChunkHeader + part.size();
  }
  if (!found) malformed("Missing behavior resource chunk");
  return *found;
}
struct ReferenceTable {
  Bytes offsets, data;
  explicit ReferenceTable(Bytes source)
      : offsets(chunk(source, "refd")), data(chunk(source, "data")) {
    if (offsets.size() % kWordBytes != 0) {
      malformed("Misaligned behavior reference table");
    }
  }
  std::size_t size() const { return offsets.size() / kWordBytes; }
  std::size_t offset(std::size_t index) const {
    if (index >= size()) malformed("Behavior reference index exceeds table");
    const auto result = Reader{offsets}.u32(index * kWordBytes);
    Reader{data}.range(result, 1);
    return result;
  }
};
std::size_t node_index(std::uint64_t offset, std::size_t count) {
  if (offset % kNodeBytes != 0 || offset / kNodeBytes >= count) {
    malformed("Behavior target is outside the node table");
  }
  return static_cast<std::size_t>(offset / kNodeBytes);
}
struct ParsedNode {
  BehaviorNode node;
  Bytes attached;
  std::int32_t name_index{};
};
ParsedNode node_data(Bytes source, std::size_t offset) {
  const Reader all{source};
  if (all.tag_view(offset) != kNodeTag) {
    malformed("Unsupported behavior node data tag");
  }
  // The record's own size bounds evaluator/parameters, independently of its
  // reference table and the encompassing wrapper.
  const auto body = payload(source, offset);
  constexpr std::size_t kFixedBytes = 28, kEvaluatorTag = 20,
                        kEvaluatorBytes = 24, kEvaluationMode = 8,
                        kPriority = 12;
  Reader reader{body};
  reader.range(0, kFixedBytes);
  if (reader.tag_view(kEvaluatorTag) != "eval") {
    malformed("Behavior evaluator record is missing");
  }
  BehaviorNode node;
  node.action = std::bit_cast<std::int32_t>(reader.u32(0));
  node.eligibility_mode = std::bit_cast<std::int32_t>(reader.u32(4));
  node.evaluation_mode =
      std::bit_cast<std::int32_t>(reader.u32(kEvaluationMode));
  node.priority = std::bit_cast<std::int16_t>(reader.u16(kPriority));
  constexpr std::int32_t kMaximumMode = 5;
  if (node.eligibility_mode < 0 || node.eligibility_mode > kMaximumMode ||
      node.evaluation_mode < 0 || node.evaluation_mode > kMaximumMode) {
    malformed("Behavior evaluation mode is out of range");
  }
  const auto name_bytes = reader.u32(kEvaluatorBytes);
  if (name_bytes != 0) {
    node.evaluator = reader.text_view({kFixedBytes, name_bytes}, 0);
  }
  const auto parameter_offset =
      kFixedBytes + static_cast<std::uint64_t>(name_bytes);
  const auto parameter_bytes = reader.u32(parameter_offset);
  const auto start = parameter_offset + kWordBytes;
  reader.range(start, parameter_bytes);
  node.parameters =
      body.subspan(static_cast<std::size_t>(start), parameter_bytes);
  // The native linked-action list starts immediately after primary parameters.
  // Validate its whole envelope before publishing any borrowed action record.
  const auto attached_offset =
      static_cast<std::size_t>(start) + parameter_bytes;
  if (reader.tag_view(attached_offset) != "subd") {
    malformed("Behavior attached-action directory is missing");
  }
  const auto attached = payload(body, attached_offset);
  if (attached.size() != body.size() - attached_offset - kChunkHeader) {
    malformed("Behavior attached-action directory has trailing bytes");
  }
  constexpr std::size_t kNameIndex = 16;
  return {node, attached, std::bit_cast<std::int32_t>(reader.u32(kNameIndex))};
}
}  // namespace

BehaviorTreeDefinition::BehaviorTreeDefinition(
    const std::shared_ptr<memory::Domain>& domain)
    : nodes_(memory::Allocator<BehaviorNode>(domain)),
      attached_(memory::Allocator<BehaviorAttachedAction>(domain)),
      entries_(memory::Allocator<std::size_t>(domain)),
      labels_(memory::Allocator<std::size_t>(domain)),
      strings_(memory::Allocator<std::string_view>(domain)) {}

BehaviorTreeDefinition BehaviorTreeDefinition::parse(
    std::span<const std::uint8_t> bytes,
    const std::shared_ptr<memory::Domain>& domain) {
  const Reader reader{bytes};
  if (reader.tag_view(0) != kRootTag) {
    malformed("Unsupported behavior resource signature");
  }
  const auto root = payload(bytes, 0);
  if (root.size() != bytes.size() - kChunkHeader ||
      chunk(root, "work").size() < kMinimumWork ||
      chunk(root, "head").size() < kWordBytes) {
    malformed("Invalid behavior resource header/work allocation");
  }
  const auto tree = chunk(root, "tree");
  if (Reader{tree}.tag_view(0) != "call") {
    malformed("Behavior tree has no call directory");
  }
  const auto calls = payload(tree, 0);
  const auto records = tree.subspan(kChunkHeader + calls.size());
  if (calls.size() % kWordBytes != 0 || records.empty() ||
      records.size() % kNodeBytes != 0) {
    malformed("Invalid behavior node/call table size");
  }
  const auto count = records.size() / kNodeBytes;
  const ReferenceTable strings(chunk(root, kStringTag));
  const ReferenceTable data(chunk(root, "data"));
  BehaviorTreeDefinition result(domain);
  result.strings_.reserve(strings.size());
  for (std::size_t i = 0; i < strings.size(); ++i) {
    result.strings_.push_back(Reader{strings.data}.text_view(
        {0, strings.data.size()},
        static_cast<std::uint32_t>(strings.offset(i))));
  }

  // The nearest still-open ancestor must be the stored parent. This validates
  // containment without recursion or a second per-node scratch allocation.
  result.nodes_.reserve(count);
  std::optional<std::size_t> enclosing;
  const Reader nodes{records};
  for (std::size_t i = 0; i < count; ++i) {
    while (enclosing && i >= result.nodes_.at(*enclosing).subtree_end) {
      enclosing = result.nodes_.at(*enclosing).parent;
    }
    const auto at = i * kNodeBytes;
    const auto length = static_cast<std::uint64_t>(nodes.u32(at)) + kWordBytes;
    if (length < kNodeBytes || length % kNodeBytes != 0 ||
        length > records.size() - at) {
      malformed("Behavior subtree extent is invalid");
    }
    const auto back = nodes.u32(at + 2 * kWordBytes);
    std::optional<std::size_t> parent;
    if (i == 0) {
      if (back != kNoParent || length != records.size()) {
        malformed("Behavior root does not contain the tree");
      }
    } else {
      if (back == 0 || back > at) malformed("Invalid behavior parent distance");
      parent = node_index(at - back, count);
      if (parent != enclosing) malformed("Behavior preorder parent mismatch");
    }
    const auto data_index = nodes.u32(at + kWordBytes);
    const auto parsed = node_data(data.data, data.offset(data_index));
    auto node = parsed.node;
    node.parent = parent;
    node.subtree_end = i + static_cast<std::size_t>(length / kNodeBytes);
    if (parent) {
      const auto end = result.nodes_.at(*parent).subtree_end;
      if (node.subtree_end > end) malformed("Behavior child exceeds parent");
      if (node.subtree_end < end) node.next_sibling = node.subtree_end;
    }
    if (length > kNodeBytes) node.first_child = i + 1;
    node.action_name = result.string(parsed.name_index);
    node.attached_begin = result.attached_.size();
    for (std::size_t at = 0; at < parsed.attached.size();) {
      const auto child = node_data(parsed.attached, at);
      // Native preparation reads only one companion level. The purchased
      // corpus has no nested companions; reject rather than discard new data.
      if (!child.attached.empty()) {
        malformed("Nested behavior companion actions are unsupported");
      }
      result.attached_.push_back({child.node.action,
                                  result.string(child.name_index),
                                  child.node.parameters});
      at += kChunkHeader + payload(parsed.attached, at).size();
    }
    node.attached_count = result.attached_.size() - node.attached_begin;
    result.nodes_.push_back(node);
    enclosing = i;
  }
  result.entries_.reserve(calls.size() / kWordBytes);
  for (std::size_t at = 0; at < calls.size(); at += kWordBytes) {
    result.entries_.push_back(node_index(
        static_cast<std::uint64_t>(Reader{calls}.u32(at)) + kWordBytes, count));
  }
  const auto labels = chunk(root, "labl");
  constexpr std::size_t kLabelBytes = 8;
  if (labels.size() % kLabelBytes != 0) {
    malformed("Invalid behavior label table");
  }
  result.labels_.reserve(labels.size() / kLabelBytes);
  for (std::size_t at = 0; at < labels.size(); at += kLabelBytes) {
    result.labels_.push_back(node_index(Reader{labels}.u32(at), count));
  }
  const auto lua = chunk(root, "luac");
  Reader{lua}.range(0, kWordBytes);
  // The embedded Lua stream keeps its big-endian length on disk; the outer
  // chunk may include alignment padding which is not part of the VM input.
  std::uint32_t bytecode_size = 0;
  for (const auto byte : lua.first(kWordBytes)) {
    bytecode_size = (bytecode_size << resources::detail::kByteBits) | byte;
  }
  Reader{lua}.range(kWordBytes, bytecode_size);
  result.bytecode_ = lua.subspan(kWordBytes, bytecode_size);
  return result;
}
std::string_view BehaviorTreeDefinition::string(std::int32_t index) const {
  return index < 0 || static_cast<std::size_t>(index) >= strings_.size()
             ? std::string_view{}
             : strings_.at(static_cast<std::size_t>(index));
}
std::span<const BehaviorAttachedAction>
BehaviorTreeDefinition::attached_actions(std::size_t node) const {
  const auto& record = nodes_.at(node);
  return std::span<const BehaviorAttachedAction>(attached_).subspan(
      record.attached_begin, record.attached_count);
}
}  // namespace bw::formats
