#include "shared_library.hpp"

#include <dlfcn.h>

#include <string>
#include <string_view>
#include <utility>

// Responsibility: shared_library.hpp, over the POSIX dynamic loader.
// dlopen with RTLD_NOW reports a missing dependency at load rather than at
// first call; RTLD_LOCAL keeps the library's symbols out of the global
// namespace. dlopen searches only for a name without a slash, so a path
// without one is refused before the call; every accepted path is opened as
// given.
//
// Retrieved rules: R.1, E.6, I.11 (see the header).
namespace bw::shader_compiler::detail {

SharedLibrary::SharedLibrary(SharedLibrary&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr)) {}

SharedLibrary& SharedLibrary::operator=(SharedLibrary&& other) noexcept {
  if (this != &other) {
    if (handle_ != nullptr) {
      dlclose(handle_);
    }
    handle_ = std::exchange(other.handle_, nullptr);
  }
  return *this;
}

SharedLibrary::~SharedLibrary() {
  if (handle_ != nullptr) {
    dlclose(handle_);
  }
}

SharedLibrary SharedLibrary::open(std::string_view path, std::string& error) {
  SharedLibrary library{};
  if (path.find('/') == std::string_view::npos) {
    error = "'";
    error += path;
    error += "' is not a path: a library is loaded from an explicit path containing '/', "
             "never searched for";
    return library;
  }
  const std::string terminated{path};
  library.handle_ = dlopen(terminated.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (library.handle_ == nullptr) {
    const char* text = dlerror();
    error = text != nullptr ? text : "dlopen failed with no error text";
  }
  return library;
}

void* SharedLibrary::symbol(const char* name, std::string& error) const {
  if (handle_ == nullptr) {
    error = "library is not loaded";
    return nullptr;
  }
  dlerror();
  void* address = dlsym(handle_, name);
  if (address == nullptr) {
    const char* text = dlerror();
    error = text != nullptr ? text : "dlsym returned null";
  }
  return address;
}

} // namespace bw::shader_compiler::detail
