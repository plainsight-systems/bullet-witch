// Small indexed navigation archive shared by decoder and route contract tests.
// Own synthetic bytes only; original assets are reserved for corpus validation.
// Core SL.con.3 preserves bounded records; MEM.9 confines fixture allocation
// to setup before consumer updates.
#pragma once
#include "behavior_fixture.hpp"
namespace navigation_fixture {
using behavior_fixture::append;
using behavior_fixture::chunk;
using behavior_fixture::references;
using bw::resources::Bytes;
using camera_fixture::word;
inline Bytes archive() {
  Bytes path(8 + 2 * 12);
  word(path, 0, 7);
  word(path, 4, 2);
  camera_fixture::scalar(path, 8, 1.0F);
  camera_fixture::scalar(path, 20, 4.0F);
  const auto patrol =
      chunk("ptrl", chunk("patr", references(chunk("patr", path))));
  Bytes vertices;
  Bytes refs(8);
  for (std::size_t index = 0; index < 2; ++index) {
    word(refs, index * 4, static_cast<std::uint32_t>(vertices.size()));
    Bytes info(16);
    camera_fixture::scalar(info, 0, static_cast<float>(index));
    word(info, 12, static_cast<std::uint32_t>(index));
    append(vertices, chunk("vect", chunk("info", info)));
  }
  auto search = chunk("pint", references(vertices, refs));
  Bytes edge(16);
  word(edge, 4, 1);
  append(search, chunk("pass", edge));
  const auto graph = chunk("scrh", search);
  Bytes data = patrol;
  data.resize(data.size() + 4);  // Valid groups need not be adjacent chunks.
  const auto graph_offset = data.size();
  append(data, graph);
  Bytes directory(32);
  word(directory, 4, static_cast<std::uint32_t>(patrol.size()));
  word(directory, 16, static_cast<std::uint32_t>(graph_offset));
  word(directory, 20, static_cast<std::uint32_t>(graph.size()));
  word(directory, 24, 11);
  auto root = chunk("refs", directory);
  append(root, chunk("data", data));
  constexpr std::string_view names{"patrol.pat\0search.pat\0", 22};
  append(root, chunk("strs", Bytes(names.begin(), names.end())));
  return chunk("arcs", root);
}
}  // namespace navigation_fixture
