// Unit test: bw/memory/alignment.hpp.
#include "bw/memory/alignment.hpp"

#include <cstddef>
#include <cstdint>

using bw::memory::align_up;

static_assert(align_up(std::size_t{0}, 4096) == 0);
static_assert(align_up(std::size_t{1}, 4096) == 4096);
static_assert(align_up(std::size_t{4096}, 4096) == 4096);
static_assert(align_up(std::size_t{4097}, 4096) == 8192);
static_assert(align_up(std::size_t{12288}, 4096) == 12288);
static_assert(align_up(std::size_t{17}, 1) == 17);
static_assert(align_up(std::size_t{17}, 64) == 64);
static_assert(align_up(std::uint32_t{65}, 64) == 128);
static_assert(align_up(std::uint32_t{0xFFFF'FFC1}, 64) == 0);

int main() { return 0; }
