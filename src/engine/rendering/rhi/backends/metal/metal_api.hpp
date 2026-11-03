#ifndef BW_RENDERING_RHI_BACKENDS_METAL_METAL_API_HPP
#define BW_RENDERING_RHI_BACKENDS_METAL_METAL_API_HPP
// The Metal backend's one include of metal-cpp and the Metal Shader Converter
// runtime shim, and the two ownership helpers every file of the backend uses.
//
// Contract.
//   metal-cpp is used with manual retain and release: an object returned by
//   a new*, alloc or Create call is owned (+1) and released by its owner's
//   destructor or by a ScopedRelease at the end of the scope that made it; an
//   object returned by any other call is autoreleased and is retained when it
//   must outlive the AutoreleaseScope it was obtained in. Every entry point
//   of the backend that can autorelease opens an AutoreleaseScope.
//   The shim is included in its metal-cpp form (IR_RUNTIME_METALCPP). Its
//   implementation, like metal-cpp's, is emitted by metal_implementation.cpp
//   alone; no other translation unit defines an implementation macro.
//
// The shim is included with IR_RUNTIME_METALCPP 1 after metal-cpp.
// AutoreleaseScope is one NS::AutoreleasePool per call site. Owned objects are
// released by hand in destructors, not held in NS::SharedPtr.
//
// Core guidelines read: R.1 (each owned API object has one owner that
// releases it), C.67 (the helpers are not copyable).
#include "bw/rendering/rhi/build.hpp"

#include <Foundation/Foundation.hpp>
#include <Metal/Metal.hpp>

#ifndef IR_RUNTIME_METALCPP
#define IR_RUNTIME_METALCPP 1
#endif
#include <metal_irconverter_runtime.h>

#include <string>
#include <string_view>

namespace bw::rhi::metal {

// One autorelease pool for the scope of a backend entry point.
class AutoreleaseScope {
public:
  AutoreleaseScope() noexcept : pool_(NS::AutoreleasePool::alloc()->init()) {}
  AutoreleaseScope(const AutoreleaseScope&) = delete;
  AutoreleaseScope& operator=(const AutoreleaseScope&) = delete;
  ~AutoreleaseScope() { pool_->release(); }

private:
  NS::AutoreleasePool* pool_;
};

// Releases an owned (+1) object at the end of the scope; for descriptors and
// other temporaries a function builds and does not keep.
template <class T> class ScopedRelease {
public:
  explicit ScopedRelease(T* object) noexcept : object_(object) {}
  ScopedRelease(const ScopedRelease&) = delete;
  ScopedRelease& operator=(const ScopedRelease&) = delete;
  ~ScopedRelease() {
    if (object_ != nullptr) {
      object_->release();
    }
  }
  [[nodiscard]] T* get() const noexcept { return object_; }
  [[nodiscard]] T* operator->() const noexcept { return object_; }

private:
  T* object_;
};

// The UTF-8 text of an NS::String, empty for null.
[[nodiscard]] inline std::string text_of(const NS::String* text) {
  if (text == nullptr) {
    return {};
  }
  const char* utf8 = text->utf8String();
  return utf8 != nullptr ? std::string{utf8} : std::string{};
}

// The localized description of an NS::Error, or `fallback` for null.
[[nodiscard]] inline std::string error_text(const NS::Error* error, std::string_view fallback) {
  if (error == nullptr) {
    return std::string{fallback};
  }
  return text_of(error->localizedDescription());
}

// An autoreleased NS::String of the given bytes (UTF-8).
[[nodiscard]] inline NS::String* make_string(std::string_view text) {
  const std::string copy{text};
  return NS::String::string(copy.c_str(), NS::UTF8StringEncoding);
}

// Debug names (resources.hpp, DebugName; build.hpp): the label of a Metal
// object, set only with BW_RHI_DEBUG 1 and only for a non-empty name.
// Precondition: an AutoreleaseScope is open (the label string is
// autoreleased).
template <class T> void set_label(T& object, std::string_view name) {
#if BW_RHI_DEBUG
  if (!name.empty()) {
    object.setLabel(make_string(name));
  }
#else
  static_cast<void>(object);
  static_cast<void>(name);
#endif
}

// The bind points the converter's code expects (metal_irconverter_runtime.h,
// kIRDescriptorHeapBindPoint, kIRSamplerHeapBindPoint,
// kIRArgumentBufferBindPoint), checked against the values this backend binds
// at. Defined in metal_implementation.cpp, where the shim's values are
// defined. Returns an empty view when they match, else the name of the first
// that differs.
inline constexpr NS::UInteger kDescriptorHeapBindPoint = 0;
inline constexpr NS::UInteger kSamplerHeapBindPoint = 1;
inline constexpr NS::UInteger kArgumentBufferBindPoint = 2;
[[nodiscard]] std::string_view irconverter_bind_point_mismatch() noexcept;

} // namespace bw::rhi::metal
#endif
