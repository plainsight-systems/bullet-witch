# Native pause input prompts

The shared PC prompt receiver (`472270`) selects keyboard artwork when the
input-manager keyboard-mode bit is set, otherwise the active pad lookup.
Fixed UI mode 2 uses default bindings even when gameplay mappings have changed
(`472930`, `472dd0`). Confirm resolves to Xbox icon 120 or keyboard icons 94
and 54 (Space); Back resolves to Xbox icon 121 or keyboard icons 95 and 39
(Shift). These are original image indices in the admitted input-icon pack.

The keyboard run advances a scale pivot by 64 source units, not the icon rectangle.
`472270` sets the rectangle once through `564630`, then moves the pivot through
`5648e0` for each icon. `563110` passes that pivot as the scaling center to
`D3DXMatrixTransformation`. At one-third scale the final canvas pitch is
`(1 - 1/3) *64 = 128/3`. Its returned text advance is
`count * (128/3) + (32/15)`; the pad returns 44.8. `MenuPrompt` publishes the
final canvas displacement so surfaces do not reapply the original transform.
Resource-owning views prepare the extra original
icons once and consume copied runs; they never read physical devices or Profile.
Ordinary root pause uses authored row artwork, not a newly invented input footer.
Options, Help and controller settings use the common confirm/back footer runs;
brightness and save acknowledgments use the corresponding single action run.

Device discovery clears the keyboard-mode bit (`41c541`, `41cb2d`), failed
polling eventually sets it (`41c64f`, `41cfbe`), and mapped pad activity clears
it (`41d1a1`). Inspection of direct global references finds no keyboard-event
writer switching this bit. That original discovery policy does not establish last-used keyboard switching.
The earlier reconstruction used copied `controller_connected` for presentation.
The user rejected that behavior in the frontend repair: a plugged-in controller
must not hide the bindings for keyboard/mouse input currently being used.

`ControllerInput` now publishes a copied active family from new keyboard/mouse
activity, pad button edges or accepted stick navigation edges. Idle connection
and stale holds do not override it; disconnect restores keyboard/mouse. The
shared fixed UI binding descriptor drives both accepted actions and glyph
resolution, while Help mode zero continues to resolve saved gameplay bindings.
This activity policy is an explicit user-requested adaptation guided by pinned
Unreal CommonInput, not a claim about an untraced original keyboard-mode writer.
See [frontend repair](../frontend-menu-repairs.md) for source/reference details
and native switching verification. No additional clock or device owner exists.

Core Guidelines I.1 and performance MEM.9 were read in both design and
implementation. Explicit copied snapshots separate input policy from resource
presentation; fixed runs require no allocations per frame. Gameplay remapping,
inline Help action bindings and other front-end views remain separate consumers.

## Verification and scale-pivot correction

Focused prompt contracts and fatal clang-tidy pass for the resolver, its tests,
Options surface and Help surface. The existing offscreen native Options probe
renders keyboard Options, calibration and all four save/discard states using
original image pixels. It observes 90 live textures with root and gameplay HUD
also resident, below the unchanged 128-slot budget; teardown retains none.
This does not substitute for the integrated gate or a visible gameplay check.

The initial reconstruction treated the 64-unit pivot step as a canvas translation,
which overlapped keyboard icons and labels. Independent review traced the scale
pivot through the native transform and corrected the shared effective pitch to
42.6667. Two icons occupy 85.3333 pixels; the 87.4667 text advance leaves 2.1333
pixels of clearance. This was a reconstruction error, not an original layout
limitation. The corrected controller keyboard footer was rendered offscreen with
original artwork and the production sprite shader. Captures and logs remain in
ignored evidence.

Options' one-row reset shortcut tests input bit `0x8000`. The fixed pad producer
maps it to Xbox north/Y. The complete native UI keyboard augmentation in
`4adef0..4ae407` adds navigation and confirm/back, but no reset bit. No keyboard
reset alias is invented; keyboard access retains the original Defaults row in
controller settings and ordinary setting adjustment.

## Fixed UI wheel input

The complete native UI augmentation in `4adef0`, exported in private engine
pass257, checks source mouse pseudo-codes `0x134` (wheel up) and `0x135`
(wheel down) alongside the corresponding arrow keys. It ORs `0x01010001`
and `0x02020002`, respectively, into held, pressed and repeated UI observations.
The reconstructed game input adapter supplies those same masks to its copied
pressed/repeated UI stream for each wheel impulse. Gameplay weapon cycling
remains a separate translation through saved keyboard bindings; menu routing
consumes the UI observation. No persistent wheel hold or new repeat timer is
introduced. The analog field is a separate fixed UI observation: `474570` returns the owner
plus8, and Help reading in `4ddea0` consumes receiver plus0x28 (owner plus0x30),
multiplying it by16 to obtain paragraph displacement. `4adef0` first copies
the pad sample, then adds +2 for wheel up or -2 for wheel down to that field.
`41cda0` establishes its baseline as positional left-stick Y; no W/S, arrow or
remapped gameplay-key branch contributes to this analog field.

The adapter publishes `menu_vertical_axis` from the platform's normalized
positional stick Y plus the recovered wheel impulse. It is not clamped to the
stick range because source wheel and stick contributions can sum beyond it.
The composition layer copies this observation into `MenuInput.vertical_axis`;
Help owns the source16-pixel conversion and its existing clamp/damped response.
Saved movement bindings therefore cannot accidentally scroll Help. The original
XInput adapter's deadzone remains source provenance; this repair does not change
the native platform's existing axis normalization.

Focused contracts verify both directions, unchanged weapon-cycle direction,
combined stick/wheel magnitude, independence from gameplay keyboard movement and
an empty sample after the impulse. The source export contains the complete
1,306-byte function and its shared physical-key query, with reproducible command
and status logs; proprietary exports remain ignored.
