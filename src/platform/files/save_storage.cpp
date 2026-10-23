// POSIX directory-backed save storage. Names are validated leaf components;
// reads reject wrong sizes before allocation. Writes use an exclusive sibling
// temporary, complete write/flush/fsync/close, then atomic rename. Failures
// before rename preserve the previous file and remove only this call's
// temporary. Directory supplied by target entry is never inferred from the game
// location. Main-thread per instance; another process may atomically replace a
// file. O(bytes), one read owner; borrowed writes retain no views. No
// durability claim beyond file fsync/rename (directory metadata is not synced).
// Design and implementation: cpp-guidelines R.1 (#rr-raii) pairs every handle;
// performance COPY.4 (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md)
// uses RAII composition. See profile-persistence.md for Unreal reference and
// departures.
#include "bw/platform/save_storage.hpp"

#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>
#include <system_error>
#include <utility>

namespace bw::platform {
namespace {
constexpr std::size_t kMaximumSaveBytes = 1024UL * 1024UL;
bool valid_name(std::string_view name) {
  return !name.empty() && name != "." && name != ".." &&
         name.find_first_of("/\\\0", 0, 3) == std::string_view::npos;
}
struct CloseFile {
  // The deleter receives the unique_ptr-owned FILE, released exactly once.
  void operator()(std::FILE* file) const noexcept {
    // NOLINTNEXTLINE(cppcoreguidelines-owning-memory)
    std::fclose(file);
  }
};
using File = std::unique_ptr<std::FILE, CloseFile>;
struct TemporaryPath {
  const std::string& value;
  ~TemporaryPath() { std::remove(value.c_str()); }
  TemporaryPath(const TemporaryPath&) = delete;
  TemporaryPath& operator=(const TemporaryPath&) = delete;
  TemporaryPath(TemporaryPath&&) = delete;
  TemporaryPath& operator=(TemporaryPath&&) = delete;
  explicit TemporaryPath(const std::string& path) : value(path) {}
};
class DirectorySaveStorage final : public SaveStorage {
 public:
  explicit DirectorySaveStorage(std::filesystem::path directory)
      : directory_(std::move(directory)) {}
  SaveRead read(std::string_view name, std::size_t expected_size) override {
    if (!valid_name(name) || expected_size > kMaximumSaveBytes) {
      return {SaveStatus::invalid, {}};
    }
    const auto path = directory_ / name;
    File file(std::fopen(path.c_str(), "rb"));
    if (!file) {
      return {errno == ENOENT ? SaveStatus::missing : SaveStatus::io_error, {}};
    }
    if (std::fseek(file.get(), 0, SEEK_END) != 0) {
      return {SaveStatus::io_error, {}};
    }
    const auto size = std::ftell(file.get());
    if (size < 0) {
      return {SaveStatus::io_error, {}};
    }
    if (static_cast<std::size_t>(size) != expected_size) {
      return {SaveStatus::invalid, {}};
    }
    if (std::fseek(file.get(), 0, SEEK_SET) != 0) {
      return {SaveStatus::io_error, {}};
    }
    std::vector<std::byte> bytes(expected_size);
    if (std::fread(bytes.data(), 1, bytes.size(), file.get()) != bytes.size() ||
        std::fgetc(file.get()) != EOF || std::ferror(file.get()) != 0) {
      return {SaveStatus::io_error, {}};
    }
    return {SaveStatus::ok, std::move(bytes)};
  }
  SaveStatus write(std::string_view name,
                   std::span<const std::byte> bytes) override {
    if (!valid_name(name) || bytes.size() > kMaximumSaveBytes) {
      return SaveStatus::invalid;
    }
    std::error_code error;
    std::filesystem::create_directories(directory_, error);
    if (error) {
      return SaveStatus::io_error;
    }
    auto pattern = (directory_ / (std::string(name) + ".tmp.XXXXXX")).string();
    const int descriptor = mkstemp(pattern.data());
    if (descriptor < 0) {
      return SaveStatus::io_error;
    }
    TemporaryPath cleanup(pattern);
    File file(fdopen(descriptor, "wb"));
    if (!file) {
      close(descriptor);
      return SaveStatus::io_error;
    }
    if (std::fwrite(bytes.data(), 1, bytes.size(), file.get()) !=
            bytes.size() ||
        std::fflush(file.get()) != 0 || fsync(descriptor) != 0) {
      return SaveStatus::io_error;
    }
    if (std::fclose(file.release()) != 0) {
      return SaveStatus::io_error;
    }
    std::filesystem::rename(cleanup.value, directory_ / name, error);
    return error ? SaveStatus::io_error : SaveStatus::ok;
  }

 private:
  std::filesystem::path directory_;
};
}  // namespace
std::unique_ptr<SaveStorage> make_save_storage(
    std::filesystem::path directory) {
  return std::make_unique<DirectorySaveStorage>(std::move(directory));
}
}  // namespace bw::platform
