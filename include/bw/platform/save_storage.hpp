// Named save storage capability, independent of game formats. Caller supplies a
// leaf name and exact bounded size; reads own bytes, writes borrow only for the
// call. Single-threaded per instance. Ordinary I/O/missing/size errors are
// explicit outcomes; allocation failure may throw. Algorithm: validate name,
// bounded read or complete temporary write then replace. Cost O(bytes), one
// owned read allocation. Design/implementation: cpp-guidelines R.1 (#rr-raii)
// owns native handles; cpp-performance COPY.4 (guidelines/copy-move/
// COPY.4-rule-of-five-rule-of-zero.md) uses owning values and noncopyable
// bases.
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace bw::platform {
enum class SaveStatus : std::uint8_t { ok, missing, invalid, io_error };
struct SaveRead {
  SaveStatus status{SaveStatus::io_error};
  std::vector<std::byte> bytes;
};
class SaveStorage {
 public:
  SaveStorage() = default;
  virtual ~SaveStorage() = default;
  SaveStorage(const SaveStorage&) = delete;
  SaveStorage& operator=(const SaveStorage&) = delete;
  SaveStorage(SaveStorage&&) = delete;
  SaveStorage& operator=(SaveStorage&&) = delete;
  virtual SaveRead read(std::string_view name, std::size_t expected_size) = 0;
  virtual SaveStatus write(std::string_view name,
                           std::span<const std::byte> bytes) = 0;
};
std::filesystem::path default_save_directory();
// Directory is supplied by the target entry, separate from the read-only game.
std::unique_ptr<SaveStorage> make_save_storage(std::filesystem::path directory);
}  // namespace bw::platform
