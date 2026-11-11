#ifndef BW_RENDERING_GRAPH_ACCESS_HPP
#define BW_RENDERING_GRAPH_ACCESS_HPP

#include "bw/rendering/rhi/access.hpp"

#include <cstdint>

// Responsibility: the ways a pass touches a resource and the kinds of pass
// (docs/architecture/render-graph.md, invariant 1), and the one place that
// maps them to the RHI's access states (rhi/access.hpp). Pure data and
// constexpr functions; nothing here allocates.
//
// Contract.
//   1. A pass is Raster, Compute or Copy. A use is one Access. The table
//      is_allowed(access, kind) says which uses a kind admits:
//        Sampled          Raster, Compute
//        Attachment       Raster
//        DepthTest        Raster
//        DepthWrite       Raster
//        ComputeWrite     Compute, Raster
//        Arguments        Raster, Compute
//        CopySource       Copy
//        CopyDestination  Copy
//      A use a kind does not admit is the named declaration error
//      GraphError::AccessNotAllowedForPassKind (builder.hpp).
//   2. rhi_access(access, kind) is the state the resource must be in for
//      that use, and the only mapping from graph access to RHI access:
//        Sampled          Raster: ShaderResourcePixel | ShaderResourceNonPixel
//                         Compute: ShaderResourceCompute
//        Attachment       RenderTarget
//        DepthTest        DepthStencilRead
//        DepthWrite       DepthStencilWrite
//        ComputeWrite     Raster: UnorderedAccessGraphics
//                         Compute: UnorderedAccessCompute
//        Arguments        IndirectArguments
//        CopySource       CopySource
//        CopyDestination  CopyDestination
//      A Raster pass's sampled state carries both shader stages because a
//      vertex shader pulls its vertices from buffers (geometry.md), so one
//      state serves every draw of the pass.
//   3. is_read(access) is true for Sampled, DepthTest, Arguments and
//      CopySource; is_write for the rest. is_attachment is true for
//      Attachment, DepthTest and DepthWrite: the uses a render pass binds.
//   4. Passes that read one resource one after another share one state
//      (invariant 8): the state is the OR of their rhi_access values, which
//      is a valid read-only access (rhi/access.hpp, is_valid_access). A
//      write never shares a state. shares_state(a, b) states that rule.
//
// A pass runs because an output is marked as leaving the frame, never by a flag
// (invariant 3); compute runs on the graphics queue and passes are recorded on
// one thread. A depth texture is transitioned whole, depth and stencil
// together: the stencil bit of the forward pass is written in the same pass
// that writes depth. One combined sampled state is declared on every backend.
// There is no multisample resolve (scene-renderer.md, invariant 7) and no
// shading rate image, so neither has an access.
//
// Core guidelines read: Enum.3 (class enums), Con.5 (every table is
// constexpr), I.1 (the pass kind is an argument of the mapping, never
// ambient). Performance guidelines read: GPU.8 (a state names the stages
// that use the resource and no more; the merged read state is the OR of the
// real readers), EMB.6 (the tables are compile-time data).
namespace bw::graph {

enum class PassKind : std::uint8_t { Raster, Compute, Copy };

enum class Access : std::uint8_t {
  Sampled,
  Attachment,
  DepthTest,
  DepthWrite,
  ComputeWrite,
  Arguments,
  CopySource,
  CopyDestination,
};

[[nodiscard]] constexpr bool is_read(Access access) noexcept {
  return access == Access::Sampled || access == Access::DepthTest || access == Access::Arguments ||
         access == Access::CopySource;
}
[[nodiscard]] constexpr bool is_write(Access access) noexcept { return !is_read(access); }
[[nodiscard]] constexpr bool is_attachment(Access access) noexcept {
  return access == Access::Attachment || access == Access::DepthTest ||
         access == Access::DepthWrite;
}

[[nodiscard]] constexpr bool is_allowed(Access access, PassKind kind) noexcept {
  switch (access) {
  case Access::Sampled:
  case Access::Arguments:
    return kind == PassKind::Raster || kind == PassKind::Compute;
  case Access::Attachment:
  case Access::DepthTest:
  case Access::DepthWrite:
    return kind == PassKind::Raster;
  case Access::ComputeWrite:
    return kind == PassKind::Compute || kind == PassKind::Raster;
  case Access::CopySource:
  case Access::CopyDestination:
    return kind == PassKind::Copy;
  }
  return false;
}

[[nodiscard]] constexpr rhi::Access rhi_access(Access access, PassKind kind) noexcept {
  switch (access) {
  case Access::Sampled:
    return kind == PassKind::Compute
               ? rhi::Access::ShaderResourceCompute
               : rhi::Access::ShaderResourcePixel | rhi::Access::ShaderResourceNonPixel;
  case Access::Attachment:
    return rhi::Access::RenderTarget;
  case Access::DepthTest:
    return rhi::Access::DepthStencilRead;
  case Access::DepthWrite:
    return rhi::Access::DepthStencilWrite;
  case Access::ComputeWrite:
    return kind == PassKind::Compute ? rhi::Access::UnorderedAccessCompute
                                     : rhi::Access::UnorderedAccessGraphics;
  case Access::Arguments:
    return rhi::Access::IndirectArguments;
  case Access::CopySource:
    return rhi::Access::CopySource;
  case Access::CopyDestination:
    return rhi::Access::CopyDestination;
  }
  return rhi::Access::Unknown;
}

// Two consecutive uses of one resource share one state when both are reads
// (contract 4). The shared state is the OR of the two.
// A write never shares a state, unordered access included: the game declares no
// overlapping unordered-access passes, has one queue, and the RHI's transitions
// carry no flags.
[[nodiscard]] constexpr bool shares_state(Access previous, Access next) noexcept {
  return is_read(previous) && is_read(next);
}

} // namespace bw::graph

#endif
