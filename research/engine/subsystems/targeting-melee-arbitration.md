# Targeting exit and melee arbitration

The October 24 physical review reproduced a real integration omission: after
leaving targeting, starting melee during weapon lowering left the upper-body
lowering owner active over the new melee body. This is separate from the
intentional retained raised-weapon lifetime. No timer, blend duration or mask
has been tuned to hide the overlap.

## Original owner contract

The recovered PC source separates selected targeting, camera context, retained
weapon posture and motion-bank ownership:

- `62ed80` consumes the mode edge. `62ebe0` restores ordinary camera context
  independently from actor `+126c` posture expiry. The installed automatic
  camera flag is zero. The existing two-mode camera transition remains unchanged.
- `568960` notifies the outgoing state with message `957404` before replacing
  current actor state `+100` with pending state `+104`. Standing/crouch Shooter
  registrations `62daf0`/`62cdf0` dispatch that exit through `62a510`, virtual
  `+4c`, to `62e400`. This handler inspects the pending state, migrating an upper
  stance for ordinary standing/crouching and retaining three explicit special
  state families. `StateGrapple` has identity `96a6c0` (`66be80`), distinct from
  all those exceptions, so the handler clears the upper ring via `5e4130`.
- Melee entry `66bf30` sends message `95f774` with duration zero and force byte
  one, requests camera preset 2, then selects body motion 706/707 at frame zero
  with a 0.3-second blend. `62bbf0` registers that message for StateGrapple;
  `62a410` dispatches virtual `+14` to `62ea40`. The receiver writes zero posture
  through `604540`, clears the selected target-mode nibble and sets the source
  0.01-second camera grace. The separate entry camera request is immediate.
- The StateGrapple Shooter registry does not bind the ordinary hardware targeting
  receiver `62a590`/`62ed80`. Thus a mode click during melee cannot refresh or
  restore ordinary targeting. Its ordinary timed update remains registered.
- These operations clear the upper-body owner, not the independent weapon-model
  animation or additive owner. No generalized all-layer reset follows from them.

Existing pass 117/126 evidence and bounded passes 140–143 retain instructions,
registration tables and pseudocode. The instructions and actual vtable values
are authoritative where the decompiler's signatures lose the receiver argument.

## Implemented correction

The action owner applies the outgoing ordinary-state upper retirement before
publishing an accepted exclusive body entry. It borrows the containing
controller's actual phase and rejects non-grounded admissions before mutation;
an empty exclusive owner is not silently treated as an airborne standing state.
Within-action continuations and melee followups do not repeat the exit. The leaf
melee motion policy does not own a special upper-clear workaround.

Accepted entry effects are synchronous. The action owner borrows the controller's
Shooter message sink, which immediately updates the retained posture and existing
targeting owner without advancing clocks or publishing a pose. Ordinary upper
retirement precedes the forced message and body selection for melee, ordinary
damage and death. DodgeStart performs upper retirement, body selection, then its
forced message and ordinary-camera request. Thunder and Tornado perform their
forced posture/target reset after body/scope selection; they do not substitute
ordinary camera2 for their externally owned camera10 scope. Same-frame accepted
entries are supported; no deferred notification or new admission restriction
can discard an earlier source effect. Zero-time pose publication therefore sees
the updated target/posture state.

Hardware input, timed updates and posture-message publication have independent
source registrations. Ordinary stance and generic jump start/flight admit
ordinary Shooter input. Generic jump ending and exclusive action states do not;
Dodge admits none throughout. Only ordinary stance and generic flight publish
upper/weapon raise/lower clips when posture crosses zero. Exclusive and generic
jump start/end receivers update posture without publishing those clips. Ancient
Wall, Will Power and Sacrifice replace the entire timed Shooter callback with a
no-op, preserving posture and camera-grace state while active. Other admitted
states retain the timed callback. Generic common-magic admission requires an
explicit update registration; arbitrary motion identity cannot infer it.

The complete constructor/RTTI, message-registration and entry-order matrix is
recorded in [player Shooter state dispatch](player-shooter-state-dispatch.md).
It includes the source generic-jump upper-retention exceptions. Unsupported
non-grounded damage/magic/death admission remains explicit rather than pretending
an empty exclusive owner is ordinary standing. Independent weapon/additive
owners and within-action followups remain intact.

This is an explicit source entry contract, not a special case for the name of
the lower animation. It covers entering melee during raised, lowering or neutral
posture. Both C++ MCP corpora were queried for design and implementation; full
I.1 and MEM.9 rules apply to the explicit entry obligation and fixed storage.

## Online reference and evidence limits

The [publisher PC stream at 09:58–10:21](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=598s)
explains intentionally retaining the raised gun for several seconds in the PC
version. That corroborates posture retention, not the exact source timer or
right-stick camera-exit ordering. The bounded inspected footage does not show
identifiable RS-on/off input followed by melee during lowering. The melee
comment around 16:44 accompanies firing/reloading, not a demonstrated melee
cancellation. See the [footage record](aim-exit-and-costume-footage.md).

The native production-path witness uses a `GCController` snapshot, the real
button-edge sampler, Stage/controller, original assets and Metal renderer. Before
correction, class0 enters upper636 at frame1, then native A selects body706 while
upper636 remains; the probe fails at that exact boundary. The before log and
captures remain under ignored `interactive-animation/lower-melee-before*`.
After correction, the same native sequence passes all four weapon classes:
body706 begins with no upper owner, targeting and raised posture are off, and
mid-melee captures show the body action without the lower-arm override. Class0
entry/before and class0/class3 middle captures were inspected; the first blended
entry frame alone is not enough to establish the visible result. The actual-root
corpus also covers all four natural lowering interruptions, direct raised entry,
and ignored targeting clicks during melee, with zero live domain memory after
teardown. The final rebuilt PW5 run repeats the production input sequence for all four
classes and passes; class0/class3 middle frames were inspected. This pose replay
does not establish settled exposure or resolve the separate brightness complaint.
Logs and captures are retained under `interactive-animation/lower-melee-*`.

An additional actual-root witness reaches natural lowering before accepted
ordinary damage, common Wall magic, Thunder and death. Every ordinary-state exit
retires upper, while damage/death deliver their proved Shooter reset. A damage
request during evade is rejected without replacing body generation. Runtime and
actual-root runs pass with zero live domain memory after teardown. The
scoped source-state re-review now covers the complete admitted matrix and
synchronous entry delivery; this is not whole-game or physical-controller acceptance.

The combined required gate passes on the frozen source-state and material-family
corrections: pinned formatting, all six CTest suites, all 158 fatal clang-tidy
translation units and repository guards. The final coherent original-root
corpus passes with zero live domain memory. Physical user confirmation of the
corrected combined sequence remains pending; finite pose output alone is not
visual acceptance.
