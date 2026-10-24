// Synthetic shape offsets and scalar keys for CPU and game-policy contracts.
// No game bytes; setup allocations are test-owned. See shape_pose_test.cpp.
#pragma once
#include <algorithm>

#include "model_fixture.hpp"
namespace shape_fixture {
inline bw::resources::Bytes offsets() {
  bw::resources::Bytes result(152);
  result[0] = 'T';
  result[1] = 'S';
  result[2] = '2';
  result[3] = 'V';
  model_fixture::word(result, 4, 0x11223344);
  model_fixture::word(result, 8, 1);
  model_fixture::word(result, 12, 3);
  model_fixture::word(result, 16, 1);
  model_fixture::word(result, 72, 80);
  model_fixture::word(result, 76, 128);
  model_fixture::word(result, 80, 96);
  const std::string_view name = "smile";
  std::ranges::copy(name, result.begin() + 96);
  for (std::size_t i = 0; i < 3; ++i) {
    model_fixture::word(result, 128 + i * 8, 0x38003c00);
    model_fixture::word(result, 132 + i * 8, 0x7c00b400);
  }
  return result;
}
inline bw::resources::Bytes shape_clip() {
  bw::resources::Bytes result(104);
  result[0] = '_';
  result[1] = 'S';
  result[2] = 'A';
  result[3] = 'B';
  model_fixture::word(result, 8, std::bit_cast<std::uint32_t>(1.0F));
  model_fixture::word(result, 12, std::bit_cast<std::uint32_t>(30.0F));
  model_fixture::word(result, 16, 1);
  model_fixture::word(result, 20, 1);
  const std::string_view name = "smile";
  std::ranges::copy(name, result.begin() + 24);
  model_fixture::word(result, 56, 60);
  model_fixture::word(result, 68, 2);
  model_fixture::word(result, 76, std::bit_cast<std::uint32_t>(1.0F));
  model_fixture::word(result, 80, 88);
  model_fixture::word(result, 84, 96);
  model_fixture::word(result, 92, std::bit_cast<std::uint32_t>(1.0F));
  model_fixture::word(result, 100, std::bit_cast<std::uint32_t>(1.0F));
  return result;
}
}  // namespace shape_fixture
