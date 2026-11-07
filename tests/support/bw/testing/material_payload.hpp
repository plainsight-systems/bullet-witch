#ifndef BW_TESTS_SUPPORT_BW_TESTING_MATERIAL_PAYLOAD_HPP
#define BW_TESTS_SUPPORT_BW_TESTING_MATERIAL_PAYLOAD_HPP
// A cooked material payload built from named records, laid out as
// bw/rendering/materials/material_asset.hpp states: the header at 0, then
// switch, scalar, vector and texture records and the string table,
// each section at the next 16-byte boundary. Shared by the materials unit
// tests and the material buffer integration test through the harness's
// include directory.
#include "bw/rendering/materials/material_asset.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace bw::testing::materials {

using Bytes = std::vector<std::byte>;

struct PayloadSpec {
  std::string_view template_name{bw::materials::kLitTemplate};
  std::vector<std::pair<std::string_view, std::uint32_t>> switches{};
  std::vector<std::pair<std::string_view, float>> scalars{};
  std::vector<std::pair<std::string_view, std::array<float, 4>>> vectors{};
  // Parameter name, then texture id.
  std::vector<std::pair<std::string_view, std::string_view>> textures{};
};

inline void pad16(Bytes& bytes) { bytes.resize((bytes.size() + 15U) & ~std::size_t{15}); }

template <class T> void write_at(Bytes& bytes, std::size_t offset, const T& value) {
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

inline Bytes build_payload(const PayloadSpec& spec) {
  namespace m = bw::materials;
  std::string strings;
  const auto add = [&strings](std::string_view s) {
    const auto offset = static_cast<std::uint32_t>(strings.size());
    strings += s;
    return std::pair{offset, static_cast<std::uint32_t>(s.size())};
  };
  m::MaterialPayloadHeader h{};
  std::tie(h.template_name_offset, h.template_name_length) = add(spec.template_name);
  std::vector<m::SwitchRecord> switches;
  for (const auto& [name, value] : spec.switches) {
    m::SwitchRecord r{};
    std::tie(r.name_offset, r.name_length) = add(name);
    r.value = value;
    switches.push_back(r);
  }
  std::vector<m::ScalarRecord> scalars;
  for (const auto& [name, value] : spec.scalars) {
    m::ScalarRecord r{};
    std::tie(r.name_offset, r.name_length) = add(name);
    r.value = value;
    scalars.push_back(r);
  }
  std::vector<m::VectorRecord> vectors;
  for (const auto& [name, value] : spec.vectors) {
    m::VectorRecord r{};
    std::tie(r.name_offset, r.name_length) = add(name);
    r.value = value;
    vectors.push_back(r);
  }
  std::vector<m::TextureRecord> textures;
  for (const auto& [name, id] : spec.textures) {
    m::TextureRecord r{};
    std::tie(r.name_offset, r.name_length) = add(name);
    std::tie(r.id_offset, r.id_length) = add(id);
    textures.push_back(r);
  }
  Bytes bytes(sizeof(m::MaterialPayloadHeader));
  const auto append = [&bytes](const auto& records) {
    pad16(bytes);
    const auto offset = static_cast<std::uint32_t>(bytes.size());
    for (const auto& r : records) {
      bytes.resize(bytes.size() + sizeof(r));
      write_at(bytes, bytes.size() - sizeof(r), r);
    }
    return offset;
  };
  h.switch_count = static_cast<std::uint32_t>(switches.size());
  h.scalar_count = static_cast<std::uint32_t>(scalars.size());
  h.vector_count = static_cast<std::uint32_t>(vectors.size());
  h.texture_count = static_cast<std::uint32_t>(textures.size());
  h.string_bytes = static_cast<std::uint32_t>(strings.size());
  h.switches_offset = append(switches);
  h.scalars_offset = append(scalars);
  h.vectors_offset = append(vectors);
  h.textures_offset = append(textures);
  pad16(bytes);
  h.strings_offset = static_cast<std::uint32_t>(bytes.size());
  for (const char c : strings) {
    bytes.push_back(static_cast<std::byte>(c));
  }
  pad16(bytes);
  write_at(bytes, 0, h);
  return bytes;
}

} // namespace bw::testing::materials

#endif
