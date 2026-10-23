// Game title resource/presentation capability. The session borrows this owner
// until destruction; begin requests original title/font/loading resources,
// ready advances construction and never reports failed data ready, show copies
// current visual commands and end retires them before releasing resources.
// Main-thread only. Exceptions surface through startup failure. No native API
// or codec types enter game flow policy. Algorithm: begin -> poll readiness ->
// show each visible update -> end on retirement. Setup may allocate; show has
// bounded sprite/glyph work. Design MCP R.20 (#rr-owner) makes retained assets
// explicit; implementation E.6 (#re-raii) pairs cleanup, cpp-performance COPY.4
// (guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md) forbids accidental
// copying of this polymorphic capability. See startup-title-integration.md.
#pragma once
#include "title_flows.hpp"

namespace bw::game {
class TitleSurface {
 public:
  TitleSurface() = default;
  virtual ~TitleSurface() = default;
  TitleSurface(const TitleSurface&) = delete;
  TitleSurface& operator=(const TitleSurface&) = delete;
  TitleSurface(TitleSurface&&) = delete;
  TitleSurface& operator=(TitleSurface&&) = delete;
  virtual void begin() = 0;
  virtual bool ready() = 0;
  virtual void show(bool visible, TitlePresentation presentation) = 0;
  virtual void end() noexcept = 0;
};
}  // namespace bw::game
