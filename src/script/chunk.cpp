// E2: translate observed little-endian Lua 5.0 32-bit chunks to the pinned
// host Lua ABI. Input is borrowed for the call; output owns bytes. Throws Error
// on bounds/version/depth/trailing-data failure. No script executes here.
// Algorithm: check header; recursively copy prototype structure, widening only
// serialized size_t and Instruction fields. Opcode words/numbers remain exact.
// Costs O(chunk bytes), one output allocation stream; nesting capped at 100.
// Optimization: no decompilation/recompilation and no per-instruction objects.
// Design/implementation MCP: I.30 (#ri-encapsulate) confines C/runtime details;
// SL.con.3 (#rsl-bounds) checks byte/index access. cpp-performance COPY.5
// (guidelines/copy-move/COPY.5-resource-owning-types-are-move-only.md):
// unique runtime owners cannot be copied; moves preserve stable heap identity.

#include "bw/script/chunk.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
namespace bw::script {
namespace {
using resources::Error;
using resources::ErrorCode;
// Lua 5.0.2 defines Instruction as unsigned long; this is an ABI fact.
// NOLINTNEXTLINE(google-runtime-int)
constexpr std::size_t kInstructionBytes = sizeof(unsigned long);
constexpr std::uint32_t kByteMask = 0xffU;
constexpr std::size_t kHeader = 22, kWord = 4, kNumber = 8, kMaxDepth = 100;
class Converter {
 public:
  explicit Converter(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
  std::string run() {
    constexpr std::array<std::uint8_t, 14> header{
        0x1b, 'L', 'u', 'a', 0x50, 1, 4, 4, 4, 6, 8, 9, 9, 8};
    if (bytes_.size() < kHeader ||
        !std::equal(header.begin(), header.end(), bytes_.begin())) {
      fail("Lua 5.0 chunk header");
    }
    // Upstream converts this sentinel to long while checking the ABI. Reject
    // NaN/out-of-range values before that conversion can become undefined.
    std::array<std::uint8_t, kNumber> encoded{};
    std::copy_n(bytes_.begin() + header.size(), kNumber, encoded.begin());
    const auto number = std::bit_cast<double>(encoded);
    constexpr double number_test = 3.14159265358979323846E7;
    if (!std::isfinite(number) ||
        std::trunc(number) != std::trunc(number_test)) {
      fail("Lua number representation");
    }
    copy(kHeader);
    constexpr std::size_t size_field = 7, instruction_field = 8;
    output_[size_field] = static_cast<char>(sizeof(std::size_t));
    output_[instruction_field] = static_cast<char>(kInstructionBytes);
    function(0);
    if (pos_ != bytes_.size()) {
      fail("Trailing Lua bytes");
    }
    return std::move(output_);
  }

 private:
  [[noreturn]] static void fail(const char* message) {
    throw Error(ErrorCode::malformed, message);
  }
  void bound(std::size_t n) const {
    if (n > bytes_.size() - pos_) {
      fail("Truncated Lua chunk");
    }
  }
  std::uint32_t word() {
    bound(kWord);
    std::uint32_t n = 0;
    for (unsigned i = 0; i < kWord; ++i) {
      constexpr unsigned bits = 8;
      n |= static_cast<std::uint32_t>(bytes_[pos_++]) << (i * bits);
    }
    return n;
  }
  void emit(std::uint32_t n, std::size_t width) {
    constexpr unsigned bits = 8;
    for (std::size_t i = 0; i < width; ++i) {
      output_.push_back(
          i < kWord ? static_cast<char>((n >> (i * bits)) & kByteMask) : '\0');
    }
  }
  void copy(std::size_t n) {
    bound(n);
    for (const auto byte : bytes_.subspan(pos_, n)) {
      output_.push_back(static_cast<char>(byte));
    }
    pos_ += n;
  }
  std::uint32_t count() {
    auto n = word();
    if (n > bytes_.size() - pos_ ||
        n > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
      fail("Lua count exceeds input");
    }
    emit(n, kWord);
    return n;
  }
  void string() {
    auto n = word();
    bound(n);
    emit(n, sizeof(std::size_t));
    if (n && bytes_[pos_ + n - 1] != 0) {
      fail("Unterminated Lua string");
    }
    copy(n);
  }
  void function(std::size_t depth) {
    if (depth > kMaxDepth) {
      fail("Lua prototype nesting");
    }
    string();
    copy(2 * kWord);
    auto lines = count();
    copy(static_cast<std::size_t>(lines) * kWord);
    auto locals = count();
    for (std::uint32_t i = 0; i < locals; ++i) {
      string();
      copy(2 * kWord);
    }
    auto upvalues = count();
    for (std::uint32_t i = 0; i < upvalues; ++i) {
      string();
    }
    auto constants = count();
    for (std::uint32_t i = 0; i < constants; ++i) {
      bound(1);
      auto type = bytes_[pos_];
      copy(1);
      if (type == 3) {
        copy(kNumber);
      } else if (type == 4) {
        string();
      } else if (type != 0) {
        fail("Lua constant type");
      }
    }
    auto children = count();
    for (std::uint32_t i = 0; i < children; ++i) {
      function(depth + 1);
    }
    auto code = count();
    for (std::uint32_t i = 0; i < code; ++i) {
      emit(word(), kInstructionBytes);
    }
  }
  std::span<const std::uint8_t> bytes_;
  std::size_t pos_{};
  std::string output_;
};
}  // namespace
std::string host_chunk(std::span<const std::uint8_t> bytes) {
  static_assert(std::endian::native == std::endian::little);
  static_assert(sizeof(int) == kWord && sizeof(double) == kNumber);
  return Converter(bytes).run();
}
}  // namespace bw::script
