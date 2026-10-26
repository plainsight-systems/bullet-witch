# Developer compilation boundary

The reconstruction's animation preview is a development extension. Original
Alicia action admission, movement, weapon resources and pause remain game-owned.
The earlier implementation constructed preview/menu state in ordinary builds even
though its developer menu had no admitted input route. This redesign removes that
shipping dependency without adding a new inspector, overlay or key binding.

## Reference and contract

Inspected Unreal revision `f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/PlayerController.cpp::AddCheats` and
`EnableCheats`. `UE_WITH_CHEAT_MANAGER` encloses creation and initialization;
shipping callers do not instantiate the manager. Adopt compile-selected owner
creation, not Unreal's object system or cheat command set. Our stricter requested
boundary also excludes the concrete preview translation units and state members.

`BW_DEVELOPER_VIEWS` selects the developer implementation. The game world retains
ordinary simulation order and a restricted preview protocol only in enabled
builds. The developer adapter borrows the world through bounded commands and const
observations; it receives no writable actor/controller state or clock authority.
Its allocation uses the existing mission memory domain. The world destroys this
borrower before presentation and simulation. No global registration or listener
is introduced.

Ordinary action/resource feedback belongs to simulation, independently of preview
status. Normal pause interactions no longer construct or consult a developer menu.
The existing developer flow remains available to developer tests; its future
user-facing admission is still a separate agreed design.

## Verification requirements

Build and exercise ordinary contracts with the switch both off and on. Explicit
preview tests run only in the enabled build; original gameplay checks remain in
both. Scan the disabled final executable before stripping for concrete
`PlayerAnimationReview` and `DeveloperReviewFlow` symbols and preview strings;
verify positive presence in the enabled test artifact. The artifact check uses an unstripped developer-disabled Debug build, so
exclusion is not an optimizer side effect. Release configurations reject an
attempt to enable developer views. A source-level guard alone does not prove
exclusion. Native visual acceptance remains separate from fixtures.

Both design and implementation retrieved Core Guidelines I.1 (`ri-explicit`) and
R.3 (`rr-ptr`): explicit capability operations and documented nonowning borrows.
Performance TLM.1 (`guidelines/telemetry/TLM.1-compile-telemetry-out-by-default.md`)
and TLM.8 (`guidelines/telemetry/TLM.8-validate-clean-builds-by-artifact-scan.md`)
require compile-time omission and linked-artifact verification. Enabled preview
setup allocates once; admitted frames retain bounded state and allocate nothing
new for this boundary.

## Current verification

Both configurations pass the full repository gate and purchased-content traversal.
The disabled unstripped executable and game archive exclude concrete preview,
flow, capability/factory symbols and checked preview labels; its compilation
inventory excludes every developer implementation. Enabled artifacts provide
positive symbol evidence, and the corpus exercises existing preview transitions.
Production hooks return rejected/empty observations without mutation. Compile-time
checks keep capability construction private, observations const and world identity
noncopyable/nonmovable. Release developer activation is rejected. The final
working build is disabled. See the [integrated record](architecture-layout-implementation.md)
for verification scope; a user walkthrough is still pending.
