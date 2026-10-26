# Neutral input and game mapping ownership

October 26, 2026. Architecture redesign implementation record.

## Evidence and chosen boundary

The original input task polls controller then keyboard/mouse; its separate
mapping pass builds held, pressed and repeating action banks. The retained
source trace is [input.md](input.md), especially `0x41cfd0`: held masks feed
`~previous & held`, with 0.5-second initial repeat and 0.125-second subtraction.
The reconstruction's accepted Cocoa event order and existing fixed UI aliases
remain unchanged in this ownership repair. This is not a correction of the
backlogged A/B button prompts.

Inspected pinned Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`:

- `Engine/Source/Runtime/ApplicationCore/Private/Mac/MacApplication.cpp`,
  `PollGameDeviceState`, `ProcessDeferredEvents`, `ProcessKeyDownEvent`:
  native device polling/event delivery emits physical observations to a message
  handler; key ordering is explicitly preserved.
- `Engine/Source/Runtime/Engine/Private/UserInterface/PlayerInput.cpp`,
  `UPlayerInput::InputKey`: player input accumulates key values/events separately
  from the native platform event loop.

Adopt the separation, not Unreal's threading/event queue or action algorithm.
This host is serialized on the main thread and retains one sampled frame, so
introducing another queue, thread, mutable shared owner or callback to game
objects is unnecessary. Original action compatibility and repeat policy belong
in game/player/input; engine ActionRepeat remains the bounded mechanism.

## Contracts and ordering

1. WindowHost begins a neutral sample, drains native events in existing order,
   retains fullscreen/window dispatch, then updates native windows.
2. InputSource records keyboard/mouse positions and short-tap presses, polls
   controller state, and clears state on focus loss. Controller availability
   remains observable while unfocused. No saved binding, menu bit, action name
   or repeat timer belongs to this platform source.
3. InputFrame carries copied PhysicalState and close observation. MappedInput
   belongs to game code and carries original UI/game intentions.
4. One application-owned ControllerInput maps fixed UI held/tap state, computes
   newly pressed bits, and advances its ActionRepeat from supplied presentation
   seconds. Wheel/mouse/default aliases retain their existing post-repeat order.
   Saved bindings and camera mouse scaling use the same sampled physical state;
   mouse scaling consumes supplied wall sample seconds, not presentation seconds.
5. Loading pumps use the same mapper to advance/discard input; they do not create
   a second repeat owner or accumulate a stale held input across loading.

The existing host merges multiple controllers by maximum axis magnitude, but
its fixed UI navigation admitted *every* controller's directions independently.
PhysicalState therefore retains raw per-axis extrema alongside the merged
movement value. Game mapping alone applies the existing navigation threshold.
This preserves simultaneous opposing directions without a platform UI policy,
extra device allocation or a silent one-controller behavior change.

Both design and implementation queried cpp_guidelines and cpp_performance.
Read full Core I.1 (`#ri-explicit`) and C.9 (`#rc-private`): explicit neutral
versus mapped values, supplied time and private noncopyable repeat owner.
Performance MEM.9 (`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) keeps
samples and repeat history in bounded inline storage. No per-pump authored heap
allocation is introduced; native framework enumeration retains its existing cost.

## Validation

Implemented at the proposed locations; coordinated repository build/validation
remains with the integrating agent. `git diff --check` passed after these edits.
The existing controller contract group now covers drained key taps, independent
wall/presentation deltas, original repeat delay/cadence, release/rearm, invalid
time rollback, opposing device axes and fixed UI versus saved bindings. The
existing native GameController test additionally exercises synthetic Cocoa
key down/up and focus reset, without a window or injecting user input.

Native user walkthrough remains separate from those contract checks. No visible
launch, commit or push was performed by this task.
