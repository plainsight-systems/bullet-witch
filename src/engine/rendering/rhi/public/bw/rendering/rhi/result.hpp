#ifndef BW_RENDERING_RHI_RESULT_HPP
#define BW_RENDERING_RHI_RESULT_HPP

#include <cstdint>
#include <string_view>
#include <type_traits>
#include <utility>

// Responsibility: the one result shape every RHI creation and lock call
// returns (docs/architecture/rhi.md, invariants 2, 6 and 10: creation failure
// is a named error, never a fallback object). A Result<T, E> holds either a
// value or an error code of a class enum whose first enumerator is None; it
// never holds both and never neither.
//
// Contract: ok() is true exactly when the error is E::None; value() has
// precondition ok() and error() has precondition !ok(). A failure may carry a
// name (the descriptor's debug name, a capability name, a shader name) in
// `detail`, a view the caller owns or a string literal; it is never stored
// past the call that produced it unless the caller copies it. No allocation.
//
// Core guidelines read: E.27 (no exceptions on the RHI path; one systematic
// error shape with a named code), E.4 (a failed creation leaves no half-made
// object: the value is absent, not a stub), I.1 (the error is a return value,
// never ambient state). Performance guidelines read: COPY.4 (the result owns
// nothing beyond the value and declares no special members); the corpus has
// no rule on result types, which is recorded here rather than citing one.
namespace bw::rhi {

template <class E>
concept ErrorCode = std::is_enum_v<E> && requires { E::None; };

template <class T, ErrorCode E> class Result {
public:
  // The value-initialized Result is a failure: a default E{} would be None, so
  // this constructor is not the default; use success() or failure().
  Result() = delete;

  [[nodiscard]] static Result success(T value) noexcept(std::is_nothrow_move_constructible_v<T>) {
    return Result{std::move(value), E::None, {}};
  }
  [[nodiscard]] static Result failure(E error, std::string_view detail = {}) noexcept {
    return Result{T{}, error, detail};
  }

  [[nodiscard]] bool ok() const noexcept { return error_ == E::None; }
  // Precondition: ok().
  [[nodiscard]] T& value() noexcept { return value_; }
  [[nodiscard]] const T& value() const noexcept { return value_; }
  // Precondition: !ok().
  [[nodiscard]] E error() const noexcept { return error_; }
  // What failed, by name; empty when the failure has no name.
  [[nodiscard]] std::string_view detail() const noexcept { return detail_; }

private:
  Result(T value, E error,
         std::string_view detail) noexcept(std::is_nothrow_move_constructible_v<T>)
      : value_(std::move(value)), error_(error), detail_(detail) {}

  T value_;
  E error_;
  std::string_view detail_;
};

// A result with no value: the error code alone.
template <ErrorCode E> class Status {
public:
  constexpr Status() noexcept = default;
  constexpr Status(E error,
                   std::string_view detail = {}) noexcept // NOLINT(google-explicit-constructor)
      : error_(error), detail_(detail) {}
  [[nodiscard]] constexpr bool ok() const noexcept { return error_ == E::None; }
  [[nodiscard]] constexpr E error() const noexcept { return error_; }
  [[nodiscard]] constexpr std::string_view detail() const noexcept { return detail_; }

private:
  E error_{E::None};
  std::string_view detail_{};
};

} // namespace bw::rhi
#endif
