// Synthetic original-wrapper layout for schema/selection contracts only.
// Names and parameters are invented fixture values, never original game data.
// Core ES.103 bounds malformed inputs in the exercised parser; CDSA.32 tests
// prepared indexes separately from authoring/storage layout.
#pragma once
#include "camera_fixture.hpp"
namespace behavior_fixture {
using bw::resources::Bytes;
using camera_fixture::word;
struct Node {
  std::uint32_t extent{}, parent{}, mode{};
  std::int32_t action{7};
  std::uint32_t evaluation{2};
  std::int16_t priority{3};
  std::uint32_t parameter{42};
  std::vector<Node> attached{};
  std::string_view name{"wait"};
  std::vector<std::uint32_t> parameters{};
};
inline Bytes chunk(std::string_view tag, const Bytes& content) {
  Bytes result(8 + content.size());
  std::copy(tag.begin(), tag.end(), result.begin());
  word(result, 4, static_cast<std::uint32_t>(content.size()));
  std::copy(content.begin(), content.end(), result.begin() + 8);
  return result;
}
inline void append(Bytes& target, const Bytes& input) {
  target.insert(target.end(), input.begin(), input.end());
}
inline Bytes references(const Bytes& data, const Bytes& refs = Bytes(4)) {
  auto result = chunk("refd", refs);
  append(result, chunk("data", data));
  return result;
}
inline Bytes fixture(std::span<const Node> nodes, std::uint32_t call = 8,
                     std::uint32_t label_target = 12) {
  Bytes root;
  append(root, chunk("work", Bytes(0x44)));
  append(root, chunk("head", Bytes(4)));
  Bytes calls(4);
  word(calls, 0, call);
  auto tree = chunk("call", calls);
  Bytes records(nodes.size() * 12), refs(nodes.size() * 4), payload;
  std::vector<std::string_view> names;
  auto record = [&](const Node& node, bool primary) {
    auto found = std::find(names.begin(), names.end(), node.name);
    if (found == names.end()) {
      names.push_back(node.name);
      found = names.end() - 1;
    }
    const auto name = static_cast<std::uint32_t>(found - names.begin());
    const std::size_t evaluation_bytes = primary ? 4 : 0;
    const std::size_t parameter_bytes =
        node.parameters.empty() ? 4 : node.parameters.size() * 4;
    Bytes data(32 + evaluation_bytes + parameter_bytes);
    word(data, 0, static_cast<std::uint32_t>(node.action));
    word(data, 4, node.mode);
    word(data, 8, node.evaluation);
    word(data, 12, static_cast<std::uint32_t>(node.priority));
    word(data, 16, name);
    const std::string_view eval = "eval";
    std::copy(eval.begin(), eval.end(), data.begin() + 20);
    word(data, 24, static_cast<std::uint32_t>(evaluation_bytes));
    if (primary) {
      data.at(28) = 'o';
      data.at(29) = 'k';
    }
    word(data, 28 + evaluation_bytes,
         static_cast<std::uint32_t>(parameter_bytes));
    if (node.parameters.empty()) {
      word(data, 32 + evaluation_bytes, node.parameter);
    } else {
      for (std::size_t i = 0; i < node.parameters.size(); ++i) {
        word(data, 32 + evaluation_bytes + i * 4, node.parameters[i]);
      }
    }
    return data;
  };
  for (std::size_t i = 0; i < nodes.size(); ++i) {
    word(records, i * 12, nodes[i].extent);
    word(records, i * 12 + 4, static_cast<std::uint32_t>(i));
    word(records, i * 12 + 8, nodes[i].parent);
    word(refs, i * 4, static_cast<std::uint32_t>(payload.size()));
    auto data = record(nodes[i], true);
    Bytes attached;
    for (const auto& action : nodes[i].attached) {
      auto companion = record(action, false);
      append(companion, chunk("subd", {}));
      append(attached, chunk(std::string_view("dag\0", 4), companion));
    }
    append(data, chunk("subd", attached));
    append(payload, chunk(std::string_view("dag\0", 4), data));
  }
  append(tree, records);
  append(root, chunk("tree", tree));
  Bytes string_data, string_refs(names.size() * 4);
  for (std::size_t i = 0; i < names.size(); ++i) {
    word(string_refs, i * 4, static_cast<std::uint32_t>(string_data.size()));
    string_data.insert(string_data.end(), names[i].begin(), names[i].end());
    string_data.push_back(0);
  }
  append(root, chunk(std::string_view("str\0", 4),
                     references(string_data, string_refs)));
  append(root, chunk("data", references(payload, refs)));
  Bytes labels(8);
  word(labels, 0, label_target);
  append(root, chunk("labl", labels));
  append(root, chunk("luac", Bytes{0, 0, 0, 4, 27, 'L', 'u', 'a', 0, 0, 0}));
  return chunk(std::string_view("ai\0\0", 4), root);
}
struct Corruption {
  std::uint32_t parent{12}, extent{20}, call{8};
};
inline Bytes fixture(Corruption fields = {}) {
  const std::array nodes{Node{fields.extent, 0xffffffffU, 1},
                         Node{8, fields.parent, 1}};
  return fixture(nodes, fields.call);
}
}  // namespace behavior_fixture
