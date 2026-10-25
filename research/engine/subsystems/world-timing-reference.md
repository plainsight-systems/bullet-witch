# Timing ownership reference

The primary behavior is the recovered [world timing contract](world-timing-source.md).
The reference checkout is Unreal Engine revision
`f45729c0b6f097038e72b52d366a62baeca3275b`. The following concrete functions
were read before designing the reconstructed interfaces; no Unreal source is
copied into production code.

| Inspected source | Relevant contract | Application here |
| --- | --- | --- |
| `Engine/Source/Runtime/Engine/Private/UnrealEngine.cpp`, `UEngine::UpdateTimeAndHandleMaxTickRate` | Monotonic measurement, rate-limit waiting, elapsed publication and optional simulation clamps are separate operations. It samples time after waiting. Fixed timestep is an explicit alternative, not implied by a target presentation rate. | Engine frame timing retains monotonic samples and pacing deadlines. The native loop performs the wait and pumps devices. Bullet Witch's recovered 100 ms presentation clamp supplies the policy; no Unreal fixed-step or network framework is adopted. |
| `Engine/Source/Runtime/Engine/Private/LevelTick.cpp`, `UWorld::Tick` | Real elapsed, scaled delta and accumulated world time have separate fields. The world publishes delta before deciding actor ticks; paused worlds run a separate pause path. Ordinary timers and actors are gated without stopping platform input. | One engine world-clock owner publishes the interval and admitted simulation time. Game pause policy chooses the recovered normal/control route. Animation/material/camera consumers cannot independently sample wall time. |
| `Engine/Source/Runtime/Engine/Private/PlayerController.cpp`, `SetPause`, `IsPaused`, `TickPlayerInput`, `ProcessPlayerInput`, `TickActor` | Game pause state is queried through world ownership; input is processed with an explicit paused flag. The paused path clears old rotation input instead of retaining stale axes. | Game menu/input policy borrows one mission control owner. The platform keeps sampling while simulation is stopped, and gameplay receives admitted or neutral current input. No duplicate menu/controller pause authority. |

The original provides the delta multiply, pause gates and input neutralization.
Unreal informs explicit C++ ownership and the separation of paused control work
from normal world work where the reconstruction lacks a complete world object.
It does not supply replacement timing constants. The existing actor-local
balanced subdivision remains the separately documented movement adaptation;
it is not promoted into a world fixed-step loop.

Public finite/nonnegative interval and scale validation, rejecting backwards
monotonic samples, and checked pause-stack overflow are reconstruction safety
contracts. The original image does not establish those rejection behaviors.
Loading/paused resource transactions can rebase presentation sampling without
changing admitted world time. Media sample timestamps and device audio position
remain their established independent observations; they never drive world time.

The ownership review reread the same pinned `APlayerController::SetPause` and
`IsPaused`: the former sends a request through the authoritative game owner,
while the latter observes world settings. Our concrete user-pause handle applies
that command/observation distinction to the recovered Bullet Witch menu task;
it does not import Unreal networking, delegates or inheritance. The original
pause flags and stack algorithm are unchanged. Restricting mutable C++ access
and forbidding moves while borrowed are reconstruction interface contracts.
