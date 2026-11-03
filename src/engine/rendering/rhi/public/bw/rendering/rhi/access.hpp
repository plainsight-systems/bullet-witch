#ifndef BW_RENDERING_RHI_ACCESS_HPP
#define BW_RENDERING_RHI_ACCESS_HPP

#include <cstdint>

// Responsibility: the resource access states a transition names, the
// pipelines (queues) a transition crosses, and the tracked access every
// buffer and texture carries (docs/architecture/rhi.md, invariant 3 and
// "Transitions"). Pure data and constexpr predicates; nothing here allocates
// or touches an API.
//
// Contract: Access is a bit set. A single access is one bit; a combined
// access is legal only when is_valid_access says so: any set of read-only
// bits, or one writable bit, or the mergeable set (read-only bits with
// unordered access bits). Unknown is zero and means "resolve from the tracked
// state" on the before side; it is never a legal after state. Pipeline is a
// bit set over the two queues; AsyncCompute collapses to Graphics on a device
// whose capability report has no async compute (device.hpp).
//
// Core guidelines read: Enum.3 (class enums), Con.5 (every mask and predicate
// is constexpr), I.1 (the state is a parameter of every call, never ambient).
// Performance guidelines read: EMB.6 (masks are compile-time data); the
// corpus has no rule on GPU resource states, recorded here rather than
// citing an unrelated rule.
namespace bw::rhi {

enum class Access : std::uint32_t {
  Unknown = 0,
  // Read states.
  CpuRead = 1U << 0U,
  Present = 1U << 1U,
  IndirectArguments = 1U << 2U,
  VertexOrIndexBuffer = 1U << 3U,
  ShaderResourceCompute = 1U << 4U,
  ShaderResourcePixel = 1U << 5U,
  ShaderResourceNonPixel = 1U << 6U,
  CopySource = 1U << 7U,
  ResolveSource = 1U << 8U,
  DepthStencilRead = 1U << 9U,
  // Read-write states.
  UnorderedAccessCompute = 1U << 10U,
  UnorderedAccessGraphics = 1U << 11U,
  RenderTarget = 1U << 12U,
  CopyDestination = 1U << 13U,
  ResolveDestination = 1U << 14U,
  DepthStencilWrite = 1U << 15U,
  // The released state of a transient resource: no contents.
  Discard = 1U << 16U,
};

[[nodiscard]] constexpr Access operator|(Access a, Access b) noexcept {
  // A flag combination is not an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<Access>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr Access operator&(Access a, Access b) noexcept {
  // A flag combination is not an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<Access>(static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b));
}
[[nodiscard]] constexpr Access operator~(Access a) noexcept {
  // A flag combination is not an enumerator.
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange)
  return static_cast<Access>(~static_cast<std::uint32_t>(a));
}
[[nodiscard]] constexpr bool has_any(Access value, Access bits) noexcept {
  return (value & bits) != Access::Unknown;
}
[[nodiscard]] constexpr bool has_all(Access value, Access bits) noexcept {
  return (value & bits) == bits;
}

// Masks. The mergeable mask is the read-only mask with the unordered access
// bits.
inline constexpr Access kShaderResourceMask =
    Access::ShaderResourceCompute | Access::ShaderResourcePixel | Access::ShaderResourceNonPixel;
inline constexpr Access kUnorderedAccessMask =
    Access::UnorderedAccessCompute | Access::UnorderedAccessGraphics;
inline constexpr Access kReadOnlyExclusiveMask =
    Access::CpuRead | Access::Present | Access::IndirectArguments | Access::VertexOrIndexBuffer |
    kShaderResourceMask | Access::CopySource | Access::ResolveSource;
inline constexpr Access kReadOnlyMask = kReadOnlyExclusiveMask | Access::DepthStencilRead;
inline constexpr Access kReadableMask = kReadOnlyMask | kUnorderedAccessMask;
inline constexpr Access kWriteOnlyExclusiveMask =
    Access::RenderTarget | Access::CopyDestination | Access::ResolveDestination;
inline constexpr Access kWriteOnlyMask = kWriteOnlyExclusiveMask | Access::DepthStencilWrite;
inline constexpr Access kWritableMask = kWriteOnlyMask | kUnorderedAccessMask;
inline constexpr Access kMergeableMask = kReadOnlyMask | kUnorderedAccessMask;

// Predicates. A value is valid when it is one write-only bit, or any set
// within the mergeable mask, or Discard alone: two writable bits together
// (RenderTarget|CopyDestination) are invalid, because no backend of this
// project can hold two write states at once and a declared transition names
// one.
[[nodiscard]] constexpr bool is_read_only(Access access) noexcept {
  return access != Access::Unknown && (access & ~kReadOnlyMask) == Access::Unknown;
}
[[nodiscard]] constexpr bool is_write_only(Access access) noexcept {
  return access != Access::Unknown && (access & ~kWriteOnlyMask) == Access::Unknown;
}
[[nodiscard]] constexpr bool is_writable(Access access) noexcept {
  return has_any(access, kWritableMask);
}
[[nodiscard]] constexpr bool is_readable(Access access) noexcept {
  return has_any(access, kReadableMask);
}
[[nodiscard]] constexpr bool is_single_bit(Access access) noexcept {
  const auto bits = static_cast<std::uint32_t>(access);
  return bits != 0U && (bits & (bits - 1U)) == 0U;
}
[[nodiscard]] constexpr bool is_valid_access(Access access) noexcept {
  if (access == Access::Unknown) {
    return false;
  }
  if (access == Access::Discard) {
    return true;
  }
  if ((access & ~kMergeableMask) == Access::Unknown) {
    return true;
  }
  return is_single_bit(access) && is_write_only(access);
}

// The architecture puts compute work on the graphics queue until a backend
// proves async compute, so a declaration naming AsyncCompute on such a
// device is executed on Graphics.
enum class Pipeline : std::uint8_t {
  None = 0,
  Graphics = 1U << 0U,
  AsyncCompute = 1U << 1U,
  All = Graphics | AsyncCompute,
};
[[nodiscard]] constexpr Pipeline operator|(Pipeline a, Pipeline b) noexcept {
  return static_cast<Pipeline>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr Pipeline operator&(Pipeline a, Pipeline b) noexcept {
  return static_cast<Pipeline>(static_cast<std::uint8_t>(a) & static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr bool is_single_pipeline(Pipeline pipeline) noexcept {
  return pipeline == Pipeline::Graphics || pipeline == Pipeline::AsyncCompute;
}

// Every buffer and texture carries one (resources.hpp, Viewable); it is
// written only by the execution of a transition and read to resolve an
// Unknown before state. Pipelines None reads as Graphics.
struct TrackedAccess {
  Access access{Access::Unknown};
  Pipeline pipelines{Pipeline::Graphics};
  [[nodiscard]] friend constexpr bool operator==(TrackedAccess, TrackedAccess) noexcept = default;
};

} // namespace bw::rhi
#endif
