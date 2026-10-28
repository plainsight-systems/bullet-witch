// Prepare world collision once; controllers and placement receivers borrow its
// immutable engine query interface. Core I.11/E.6 and performance MEM.9/MEM.6
// were queried/read during design: returned value owns domain metadata while
// original stage definitions outlive it. Setup may allocate/throw; no renderer,
// actor, clock or provider lifetime is hidden in this preparation function.
#pragma once
#include "bw/spatial/static_collision.hpp"
#include "stage_source.hpp"
namespace bw::game {
scene::StaticCollision prepare_world_collision(
    const content::StageDefinition&, StageBackgroundSource,
    const std::shared_ptr<memory::Domain>&);
}  // namespace bw::game
