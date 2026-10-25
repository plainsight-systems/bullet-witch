# Progression ownership and established reference

October 24, 2026. This is an implementation design comparison, not a claim that
Unreal implements Bullet Witch's rules. No reference source is incorporated.

## Original owners being decomposed

The original Record service owns permanent stat, weapon and magic levels and
upgrade currency. Its power-up task queries eligibility, confirms a purchase,
subtracts the cost and increases a level. The mission owner separately retains
checkpoint player values and mission masks. Player setup applies either Record
levels or the per-field maximum of checkpoint and Record levels; script masks
remain mission-local. Save tasks copy fixed Record/System bytes to platform I/O.
See [source progression](player-progression-state.md) for the original anchors.

The reconstruction names these responsibilities `Profile`, `Progression`,
`progression_save` and `MissionPlayerState`. `Profile` remains the sole mutable
permanent owner; typed snapshots cannot mutate it. Its fixed Record payload is
canonical, and codec helpers are its only mapped progression readers/writers.
An accepted mutation validates a value candidate before encoding mapped fields.
Unknown Record bytes survive. The mission owns its effective values independently
because the original deliberately distinguishes mission overrides from Record.
This is decomposition of recovered owners, not a new universal state framework.

## Inspected established implementation

Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`, from the authorized
ignored reference checkout:

- `Engine/Source/Runtime/Engine/Classes/GameFramework/SaveGame.h`: `USaveGame`
  defines an explicitly populated saved-data object; `ULocalPlayerSaveGame`
  separates pre/post-save lifecycle and success/failure from live player state.
- `Engine/Source/Runtime/Engine/Private/GameplayStatics.cpp`:
  `SaveGameToMemory` serializes state before `SaveDataToSlot` hands bytes to
  `ISaveGameSystem`. `LoadGameFromMemory` constructs a separate candidate object.
  `SaveGameToSlot`/`LoadGameFromSlot` compose encoding and platform storage.
  Async callbacks assert game-thread execution.
- `Engine/Source/Runtime/Engine/Classes/Engine/GameInstance.h`: game-session
  ownership is separate from map/player lifetimes.

Adopt the explicit game-data / encoding / platform-storage boundary, candidate
publication and single game-thread mutation. The existing portable `SaveStorage`
already implements the storage boundary. Preserve Bullet Witch's original byte
layout and synchronous save-task behavior; do not import Unreal reflection,
object graphs, slot formats, asynchronous jobs or networking.

Original source determines defaults, costs, rewards, inheritance and masks.
Unreal informs only the decomposition and transactional publication discipline
where Cavia's source-level class declarations are unavailable. The development
completed-game profile is an explicitly authorized project policy, not an
original unlock algorithm or an Unreal convention.
