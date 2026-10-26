// Prepared original Leaderboards shell and network acknowledgment. Borrows
// resource/output capabilities for its lifetime; owns tag25 archive leases,
// font/art and fixed draw storage. begin/ready admit assets, show consumes only
// a copied snapshot, end releases partial or complete residency. No provider,
// input, profile or network authority enters rendering. Core I.1 and
// performance MEM.9, read for design, require explicit ownership and no frame
// allocations.
#pragma once
#include <memory>

#include "bw/render/sprites.hpp"
#include "bw/resources/service.hpp"
#include "ranking_flow.hpp"
namespace bw::game {
class RankingSurface final : public RankingOutput {
 public:
  RankingSurface(resources::ResourceService& resources,
                 render::SpriteOutput& output,
                 resources::ArchiveBytes input_icons);
  ~RankingSurface() override;
  RankingSurface(const RankingSurface&) = delete;
  RankingSurface& operator=(const RankingSurface&) = delete;
  RankingSurface(RankingSurface&&) = delete;
  RankingSurface& operator=(RankingSurface&&) = delete;
  void begin() override;
  bool ready() override;
  void show(const RankingPresentation& presentation) override;
  void end() noexcept override;

 private:
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace bw::game
