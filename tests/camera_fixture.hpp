// Small synthetic camera/settings/collision inputs shared by in-process tests.
// No game bytes are embedded. Test backing is the explicit host allocation
// boundary; production metadata still uses the injected domain. Both MCP
// phases: R.2 (#rr-use-ptr) bounds byte arrays; MEM.6
// (guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md) supplies
// the exercised allocator. Fixed fixture layouts encode public disk contracts.
#pragma once
#include <bit>
#include <memory_resource>

#include "bw/formats/common.hpp"
#include "bw/memory/domain.hpp"
namespace camera_fixture {
class Backing final : public bw::memory::CpuBacking {
 public:
  void* allocate(std::size_t bytes, std::size_t alignment) noexcept override {
    try {
      return std::pmr::new_delete_resource()->allocate(bytes, alignment);
    } catch (const std::bad_alloc&) {
      return nullptr;
    }
  }
  void deallocate(void* pointer, std::size_t bytes,
                  std::size_t alignment) noexcept override {
    std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
  }
};
inline void require(bool condition, const char* message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}
inline void word(bw::resources::Bytes& bytes, std::size_t at,
                 std::uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) {
    bytes.at(at + i) = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU);
  }
}
inline void scalar(bw::resources::Bytes& bytes, std::size_t at, float value) {
  word(bytes, at, std::bit_cast<std::uint32_t>(value));
}
inline bw::resources::Bytes collision(
    std::string_view name = "wall", std::uint32_t flags = 0x64,
    const std::array<std::array<float, 3>, 3>& vertices = {
        {{-10, -10, 1}, {0, 10, 1}, {10, -10, 1}}}) {
  bw::resources::Bytes bytes(204);
  word(bytes, 36, 1);
  word(bytes, 40, 48);
  word(bytes, 48, 64);
  for (std::size_t axis = 0; axis < 3; ++axis) {
    scalar(bytes, 64 + 16 + axis * 4, 100);
  }
  word(bytes, 64 + 36, 3);
  word(bytes, 64 + 40, 96);
  word(bytes, 64 + 44, 1);
  word(bytes, 64 + 48, 132);
  word(bytes, 64 + 52, flags);
  require(name.size() < 32, "Fixture name fits collision field");
  std::copy(name.begin(), name.end(), bytes.begin() + 128);
  for (std::size_t vertex = 0; vertex < 3; ++vertex) {
    for (std::size_t axis = 0; axis < 3; ++axis) {
      scalar(bytes, 160 + vertex * 12 + axis * 4, vertices.at(vertex).at(axis));
    }
  }
  bytes.at(198) = 1;
  bytes.at(200) = 2;
  return bytes;
}
inline bw::resources::Bytes customization() {
  constexpr std::size_t names = 32, nodes = 128, payload = nodes + 64;
  bw::resources::Bytes bytes(nodes + 592);
  word(bytes, 0, 0x4d545343);
  word(bytes, 4, 0x04030201);
  word(bytes, 12, names);
  word(bytes, 16, nodes);
  constexpr std::array names_list{
      std::string_view("witch"),   std::string_view("actor"),
      std::string_view("pw0"),     std::string_view("behind_camera"),
      std::string_view("general"), std::string_view("camera")};
  constexpr std::array<std::uint32_t, 6> offsets{0, 16, 32, 48, 544, 560};
  std::size_t name_at = 0;
  for (std::size_t i = 0; i < offsets.size(); ++i) {
    const auto at = nodes + offsets.at(i);
    word(bytes, at, 0xffffffff);
    word(bytes, at + 4, 0xffffffff);
    word(bytes, at + 8, static_cast<std::uint32_t>(name_at));
    std::copy(names_list.at(i).begin(), names_list.at(i).end(),
              bytes.begin() + static_cast<std::ptrdiff_t>(names + name_at));
    name_at += names_list.at(i).size() + 1;
  }
  word(bytes, nodes + 4, 16);
  word(bytes, nodes + 16, 544);
  word(bytes, nodes + 20, 32);
  word(bytes, nodes + 36, 48);
  word(bytes, nodes + 548, 560);
  scalar(bytes, payload + 56, .7F);
  scalar(bytes, payload + 60, .5F);
  scalar(bytes, payload + 68, -3);
  scalar(bytes, payload + 80, 44);
  scalar(bytes, payload + 476, 5);
  scalar(bytes, nodes + 576, .01F);
  scalar(bytes, nodes + 580, 1000);
  return bytes;
}
}  // namespace camera_fixture
