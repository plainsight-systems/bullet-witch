// Source NPC approach/retreat hysteresis, separate from route finding and
// motion. The actor owns this retained state and its route follower. All
// scene/ground observations are immutable borrows; returned steering is a value
// consumed by the existing actor motion owner. This class cannot write actor or
// world state. Source 5ce940 selects idle/approach/retreat; the route provider
// owns waypoint progress and graph policy. Neither owns a clock: advance uses
// an admitted interval. C.32 (#rc-dtor-ptr) makes capability borrows explicit;
// CDSA.14
// (guidelines/cpu-dsa/CDSA.14-precompile-bounded-automata-cache-the-rest.md)
// keeps the three-state controller bounded and separate from route storage.
#pragma once
#include <cstdint>

#include "bw/math/vector.hpp"
namespace bw::game {
using NpcNavigationVector = math::Vector3<float>;
// I.1 makes the original two receivers explicit: routed movement retains its
// request pair and retries sensor failures; direct movement may complete/reset
// on a failed sensor. Neither policy is inferred from animation posture.
enum class NpcNavigationReceiver : std::uint8_t { routed, direct };
struct NpcNavigationRequest {
  static constexpr float kMinimumSquared = 0.3F;
  NpcNavigationVector destination{};
  float approach_start_squared{}, approach_stop_squared{};
  float retreat_start_squared{-1}, retreat_distance{-1};
  float minimum_squared{kMinimumSquared};
  bool trace_path{true};
  NpcNavigationReceiver receiver{NpcNavigationReceiver::routed};
  // Original action permission to publish a destination despite a failed
  // movement sensor. This is not a request to turn the visible model.
  bool force_movement{};
};
struct NpcNavigationFrame {
  NpcNavigationVector position{};
  float delta_seconds{};
  // Source5d2c80 movement-context fields: idle threshold contribution and
  // retreat route arrival threshold. Supplied by the actor settings owner.
  float idle_threshold_adjustment{}, retreat_arrival_squared{};
};
struct NpcRouteResult {
  NpcNavigationVector waypoint{};
  bool arrived{}, blocked{};
};
class NpcRouteFollower {
 public:
  NpcRouteFollower() = default;
  virtual ~NpcRouteFollower() = default;
  NpcRouteFollower(const NpcRouteFollower&) = delete;
  NpcRouteFollower& operator=(const NpcRouteFollower&) = delete;
  NpcRouteFollower(NpcRouteFollower&&) = delete;
  NpcRouteFollower& operator=(NpcRouteFollower&&) = delete;
  virtual NpcRouteResult advance(const NpcNavigationVector& position,
                                 const NpcNavigationVector& destination,
                                 float arrival_squared, float delta_seconds,
                                 bool trace_path) = 0;
  virtual void reset() = 0;
  virtual bool has_route() const = 0;
};
class NpcNavigationQueries {
 public:
  NpcNavigationQueries() = default;
  virtual ~NpcNavigationQueries() = default;
  NpcNavigationQueries(const NpcNavigationQueries&) = delete;
  NpcNavigationQueries& operator=(const NpcNavigationQueries&) = delete;
  NpcNavigationQueries(NpcNavigationQueries&&) = delete;
  NpcNavigationQueries& operator=(NpcNavigationQueries&&) = delete;
  virtual bool visible(const NpcNavigationVector& destination) const = 0;
  // Source5cf0c0 destination projection; retreat additionally applies source
  // obstacle clearance and ground/foot offsets. The world adapter implements
  // these using shared query capabilities, never a second collision solver.
  virtual NpcNavigationVector project(const NpcNavigationVector& position,
                                      const NpcNavigationVector& destination,
                                      bool retreat) const = 0;
};
struct NpcNavigationResult {
  NpcNavigationVector direction{}, destination{};
  bool moving{}, blocked{}, arrived{}, retreating{};
};
class NpcNavigationController {
 public:
  NpcNavigationController(const NpcNavigationQueries&, NpcRouteFollower&);
  ~NpcNavigationController() = default;
  NpcNavigationController(const NpcNavigationController&) = delete;
  NpcNavigationController& operator=(const NpcNavigationController&) = delete;
  NpcNavigationController(NpcNavigationController&&) = delete;
  NpcNavigationController& operator=(NpcNavigationController&&) = delete;
  NpcNavigationResult advance(const NpcNavigationRequest&,
                              const NpcNavigationFrame&);
  void reset();

 private:
  enum class State : std::uint8_t { idle, approach, retreat };
  const NpcNavigationQueries& queries_;
  NpcRouteFollower& route_;
  State state_{State::idle};
  NpcNavigationVector heading_{};
};
}  // namespace bw::game
