# Weapon selection and prepared residency

Normal weapon selection must not borrow the paused appearance menu's lifetime.
The original `603900` retires the old selected model through `616440`, changes
its selected weapon index, then calls `615900`. That constructor creates the new
model instance from retained weapon fields `+b4/+b8`, installs the attachment
transform and selects rest1 or rest2 according to raised posture. It does not
pause the scene/audio or restart the host clock. Current switch animation and
body-owner preservation remain valid; repeated archive/decode/upload and global
pause were a distinct ownership defect.

The mission resource owner will prepare all four admitted weapon classes during
loading. Each immutable model/support/device owner stays at a stable address
until mission cancellation. A selection candidate borrows an already prepared
entry; publish replacement draws before committing the selected index. Rollback
only drops candidate selection, never destroys retained class assets. The
selected model's independent playback/attachment state is recreated, as in the
source; body/event/audio/controller/camera state remains alive. Preparation cost
moves to visible mission loading, rather than being removed from game elapsed
time on a button press. Ordinary selection does not set Stage pause, consume a
UI-only frame or rebase host deadlines. Actual menu selection remains paused by
its UI owner.

Concrete reference: Epic Unreal Engine revision
`f45729c0b6f097038e72b52d366a62baeca3275b`,
`Engine/Source/Runtime/Engine/Private/Components/SkeletalMeshComponent.cpp`,
`USkeletalMeshComponent::SetSkeletalMesh` and
`SetSkeletalMeshWithoutResettingAnimation`. These accept an existing asset,
synchronize that component's pending evaluation and rebuild its component-local
render/animation state. They do not pause unrelated world/audio owners. We retain
our original selected-model recreation rather than importing Unreal animation
reset policy. The resource/component distinction and local synchronization are
the applicable reference contracts.

Both header and implementation consultations use Core R.20/I.11 for retained
resource ownership and borrowed candidate lifetime, and performance CDSA.32/MEM.9
for preparing immutable data once during admission. This is not a claim that a
source-selected model instance can never allocate: its reconstructed pose and
playback instance may allocate at activation. Measure that bounded path; do not
hide it with a global pause or clock reset. Required evidence is repeated live
selection with no file reads/texture/model uploads, preserved unrelated clocks
and audio state, failed renderer-publication rollback and balanced teardown.

## Verification

The actual-root corpus verifies all four weapon classes twice against one retained
mission admission: no further file reads, model uploads or texture uploads;
current/candidate pointers remain stable; rejected renderer publication preserves
current body/weapon/action state; cancellation returns charged live memory to0.
Initial admission still tests partial-upload failure and retry. The normal Stage
selection runs unpaused and does not call audio pause.

The offscreen native Metal/FACT sequence selects all four classes twice without
pausing. Handles61–64 recur unchanged, file-read delta and device-residency delta
remain0, and each selection preserves the active body owner while the original
upper switch progresses. Independent camera motion and132 audio updates continue
after each selection, with no additional pause call. The first render is warmed
before the device-residency comparison so lazy shadow targets are not misreported
as weapon assets. Mid-switch native frames were inspected. This is offscreen
production-owner evidence, not control of the user's game window.

Debug instance activation takes6.38–7.43ms across those eight visits on this run.
This includes the selected pose/playback/palette candidate and retained-draw
replacement. It is not a zero-allocation claim or whole-frame performance result.
The cost remains ordinary elapsed time, rather than being hidden by a global
pause/deadline reset. Logs/captures and reproducible commands remain ignored under
`interactive-animation/resident-switch-*`. Full integrated checks follow the
other concurrent ownership corrections; spawn admission remains explicitly open.
