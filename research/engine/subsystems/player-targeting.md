# Player targeting selection and camera context

Right-thumbstick click selects the original Shooter targeting mode. It is not
an instruction to wait for the raised-weapon animation timer. The prior host
connected the button to a Stage presentation latch, omitting targeting context.
Its second click could only stop posture refresh; eventual gun lowering was an
incorrect acceptance criterion for leaving aim.

## Recovered owner and phase

`62ed80` increments Shooter+28's signed low nibble modulo the current weapon's
`617500` mode count. The installed unenchanted weapons expose two modes. Mode1
keeps targeting active; mode0 stops target/raycast/crosshair selection when RT
is released. RT can still request targeting/fire calculations without selecting
mode1. The upper-owner filter precedes that trigger admission.

`62ebe0` runs before input. In mode0 it restores ordinary camera context2 via
`604600` after any retained short camera grace has expired. `62ed80` requests
context1 when entering mode1. General-camera+14 controls automatic requests and
refresh of Shooter+38's .01-second grace; the installed setting is zero.
Therefore the second click exits target selection immediately and restores
ordinary camera on the following owner update. The separate actor+126c raised
posture lifetime may remain positive for six seconds (or .75 for the heavy
weapon), and an upper action can hold that lifetime. Camera and targeting exit
must not wait for it.

`PlayerTargeting` is the single ordinary-mode owner. It emits selection,
target-active and camera-context observations; `PlayerController` retains the
independent posture lifetime. Stage routes those observations to their consumers.
Explicit preview selections use the targeting owner rather than maintaining a
second live-aim latch. Raised scripted-flight previews retain an explicit
synthetic posture obligation, separate from live targeting and camera selection.
Enchanted extra modes, crosshair/target queries, aiming HUD and hit outcomes are
not claimed by this two-mode camera/presentation implementation.

## Native boundary

The host samples `GCExtendedGamepad.rightThumbstickButton.pressed`, combines
native/keyboard action holds, and derives one press edge before game policy.
`gamepad_actions` isolates that existing sampling/edge algorithm so a native
GameController snapshot can exercise press, hold, release and second press
without creating a window or touching a connected controller. Apple SDK
GameController headers explicitly permit writes to developer-created snapshot
button objects; writes do not affect real devices. This is a deterministic
native-adapter witness, not a physical Xbox interaction claim. Very short
physical taps entirely between host samples remain a polling limitation.

Both design and implementation query the Core and performance MCP corpora.
Read I.1/E.4 require explicit ownership and prevalidated state changes; MEM.9
keeps targeting and edge history fixed with no per-update allocation. The
recovered algorithm is used directly; no timeout shortening or forced pose
reset is introduced to simulate an aiming toggle.


## Current verification

Native GameController snapshot press/hold/release/second-press drives the same
button objects, sampler and edge owner used by StartupHost, then the production
Stage/controller/camera and native Metal renderer. Across all four weapon classes,
mode1 reaches the authored20-degree field of view. The second press immediately
clears selected/target-active; the next owner phase requests ordinary camera,
which reaches44 degrees while raised posture is still active. Native on/off
images were inspected. RT alone does not select zoom mode. Evidence is retained
under `interactive-animation/targeting-controller.*` and its capture directory.
This replaces the previous eventual-expiry acceptance criterion. Scoped adversarial
re-review found no remaining ownership/admission defect in targeting, inactive Stop
or classification. It does not clear unrelated reconstruction debt. Physical Xbox
interaction remains pending.
