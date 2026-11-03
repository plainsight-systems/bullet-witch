#ifndef BW_RENDERING_RHI_QUERIES_HPP
#define BW_RENDERING_RHI_QUERIES_HPP
#include "bw/rendering/rhi/lifetime.hpp"
#include "bw/rendering/rhi/resources.hpp"
#include "bw/rendering/rhi/result.hpp"

#include <cstdint>
#include <string_view>

// Responsibility: occlusion and absolute-time queries (docs/architecture/rhi.md,
// "Scope by game need": timestamp queries with calibrated clocks are
// reported as a capability, off until a backend proves it). Queries live in
// pools; a pool has one kind and a fixed count; the command list records
// begin and end (command_list.hpp); the device reads results (device.hpp).
//
// Contract.
//   A QueryPool of kind AbsoluteTime is created only when the capability
//   report has timestamp_queries; otherwise creation returns KindUnsupported
//   by name. An Occlusion pool is always available. count is at least 1 and
//   the debug name non-empty.
//   Occlusion: begin_query and end_query bracket draws inside a render pass
//   whose description names the pool; the result is the number of samples
//   that passed depth and stencil (not fragments, not a boolean). A begin
//   outside such a pass is rejected by the validation layer.
//   AbsoluteTime: end_query writes the GPU timestamp at that point;
//   begin_query is not recorded for this kind (the validation layer reports
//   one as QueryKindMismatch). The result is in
//   microseconds on the GPU's clock; two results from the same pool and
//   frame are comparable; a CPU clock correlation is not part of this
//   contract.
//   Results: Device::query_result(pool, index, policy) returns the value
//   once the frame that ended the query has completed; PollOnly returns
//   NotReady before that; Block waits for that frame, and returns
//   NotSubmitted without blocking when that frame has not been submitted
//   (no end_frame yet: the wait would never return). A query that has not
//   been ended since the pool was created, or since it was last begun or
//   ended again, returns NotEnded. A result is stable until the query is
//   recorded again.
//
// Core guidelines read: I.1 and E.27 (kind and count explicit; named
// failures), C.35 and C.67 (through Resource). Performance guidelines read:
// GPU.7 (results are read a frame late by default; the Block policy is the
// caller's visible choice), TLM.11 was read for timestamp correlation and
// is left to the slice that measures GPU time.
namespace bw::rhi {

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIDefinitions.h:250-258: ERenderQueryType
// {RQT_Undefined, RQT_Occlusion ("samples not culled"), RQT_AbsoluteTime
// ("current time in microseconds, not a duration")}. Followed.
enum class QueryKind : std::uint8_t { Occlusion, AbsoluteTime };

struct QueryPoolDescription {
  QueryKind kind{QueryKind::Occlusion};
  std::uint32_t count{};
  std::string_view debug_name{};
};

// Unreal f45729c0b6f097038e72b52d366a62baeca3275b
// Engine/Source/Runtime/RHI/Public/RHIResources.h:2454-2458: FRHIRenderQuery;
// :2460-2522: FRHIRenderQueryPool and FRHIPooledRenderQuery. Followed as one
// pool type with indices. Omitted: the default pool of individually created
// queries (Engine/Source/Runtime/RHI/Public/DynamicRHI.h:78-95) and the
// occlusion batch machinery (RHICommandList.h:4055-4071), which Metal's
// visibility result buffer makes a property of the render pass here.
class QueryPool : public Resource {
public:
  [[nodiscard]] QueryKind kind() const noexcept { return kind_; }
  [[nodiscard]] std::uint32_t count() const noexcept { return count_; }
  [[nodiscard]] std::string_view debug_name() const noexcept { return name_.view(); }

protected:
  QueryPool(DeferredDeleteQueue& queue, const QueryPoolDescription& description) noexcept
      : Resource(queue), kind_(description.kind), count_(description.count),
        name_(description.debug_name) {}

private:
  QueryKind kind_;
  std::uint32_t count_;
  DebugName name_;
};

enum class QueryError : std::uint8_t {
  None,
  // AbsoluteTime without the timestamp_queries capability.
  KindUnsupported,
  ZeroCount,
  MissingDebugName,
  IndexOutOfRange,
  // The query has not been ended since the pool was created or since it
  // was last recorded again.
  NotEnded,
  // PollOnly and the frame that ended the query has not completed.
  NotReady,
  // Block and the frame that ended the query has not been submitted.
  NotSubmitted,
  OutOfMemory,
  BackendRejected,
};

// The pure part of Device::create_query_pool, in this order: ZeroCount,
// MissingDebugName. KindUnsupported needs the capability report and is the
// device's.
[[nodiscard]] constexpr QueryError validate_query_pool(const QueryPoolDescription& d) noexcept {
  if (d.count == 0U) {
    return QueryError::ZeroCount;
  }
  if (d.debug_name.empty()) {
    return QueryError::MissingDebugName;
  }
  return QueryError::None;
}

struct QueryValue {
  std::uint64_t value{};
};
using QueryPoolResult = Result<Ref<QueryPool>, QueryError>;
using QueryResult = Result<QueryValue, QueryError>;

} // namespace bw::rhi
#endif
