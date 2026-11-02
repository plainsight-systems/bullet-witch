#ifndef BW_SHADER_COMPILER_SHARED_LIBRARY_HPP
#define BW_SHADER_COMPILER_SHARED_LIBRARY_HPP
#include <string>
#include <string_view>

// Responsibility: an owning handle to a library loaded from an explicit path
// (DXC, the Metal Shader Converter). Private to shader_compiler.
//
// Contract: open() loads the library at exactly the path given, with no
// search. A path without a '/' would make the dynamic loader search its
// default directories, so it is refused before loading, with `error` naming
// the requirement. On any failure the handle is empty and `error` holds the
// refusal or the loader's error text. symbol() returns the address of an exported symbol
// or null, writing the loader's text on failure. The destructor closes the
// library. Every object created through the library must be released before
// the handle is destroyed; callers declare the handle before those objects so
// destruction order guarantees it.
//
// Core guidelines read: R.1 and E.6 (the handle owns the library and closes
// it on every path), I.11 (no raw ownership leaves the class). Performance
// guidelines read: COPY.4 and COPY.2 (move-only, all five special members
// declared, moves noexcept).
namespace bw::shader_compiler::detail {

class SharedLibrary {
public:
  SharedLibrary() noexcept = default;
  SharedLibrary(const SharedLibrary&) = delete;
  SharedLibrary& operator=(const SharedLibrary&) = delete;
  SharedLibrary(SharedLibrary&& other) noexcept;
  SharedLibrary& operator=(SharedLibrary&& other) noexcept;
  ~SharedLibrary();

  [[nodiscard]] static SharedLibrary open(std::string_view path, std::string& error);
  [[nodiscard]] bool loaded() const noexcept { return handle_ != nullptr; }
  [[nodiscard]] void* symbol(const char* name, std::string& error) const;

private:
  void* handle_{};
};

} // namespace bw::shader_compiler::detail
#endif
