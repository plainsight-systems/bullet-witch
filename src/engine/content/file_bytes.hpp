#ifndef BW_CONTENT_FILE_BYTES_HPP
#define BW_CONTENT_FILE_BYTES_HPP

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

// Internal to the content module: the whole-file read the loader, the
// converted cache record and the Steam files share.
namespace bw::content::detail {

template <typename Bytes> struct FileRead {
  Bytes bytes{};
  // The OS message when the read failed; empty on success.
  std::string error{};
};

// The whole file through the C library, so a refusal carries errno's text.
// It reads until end of file rather than trusting a size taken beforehand.
// Bytes is a contiguous container of a byte type: std::string or
// std::vector<std::byte>.
template <typename Bytes>
[[nodiscard]] FileRead<Bytes> read_whole_file(const std::filesystem::path& path) {
  FileRead<Bytes> result{};
  const std::unique_ptr<std::FILE, int (*)(std::FILE*)> file(std::fopen(path.c_str(), "rb"),
                                                             &std::fclose);
  if (!file) {
    result.error = std::strerror(errno);
    return result;
  }
  std::array<typename Bytes::value_type, 65536> chunk{};
  while (std::feof(file.get()) == 0 && std::ferror(file.get()) == 0) {
    const std::size_t read = std::fread(chunk.data(), 1, chunk.size(), file.get());
    result.bytes.insert(result.bytes.end(), chunk.begin(),
                        chunk.begin() + static_cast<std::ptrdiff_t>(read));
  }
  if (std::ferror(file.get()) != 0) {
    result.error = std::strerror(errno);
    result.bytes.clear();
  }
  return result;
}

} // namespace bw::content::detail

#endif
