// Fixed native UI observations; copied input has no device or timing authority.
// Core I.1 and performance MEM.9 require explicit values and no frame storage
// allocation. Shared by native menu policy and its original dialog task.
#pragma once
#include <cstdint>
namespace bw::game {
struct MenuInput {
  std::uint32_t pressed{};
  std::uint32_t repeated{};
  // Fixed UI receiver axis: physical stick plus wheel contribution, independent
  // of remapped gameplay movement. Copying it grants no device/state authority.
  float vertical_axis{};
};
}  // namespace bw::game
