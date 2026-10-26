# Frontend profile loading

The native desktop character-load task is reconstructed from the purchased PC
image's `004a1240`, its load core `004a5cc0`, and save core `004a6380`.
Local evidence is organized under the ignored engine passes `06w`, `06h` and
`270-frontend-profile-dialogs`; the bounded save-core instruction trace and
purchased-text readout are under `native-pause/profile-load` evidence.
These are behavioral descriptions, not redistributed decompiler output.

## Original desktop protocol

The character-load owner waits for its resources, reveals over 0.1 seconds,
then checks the selected desktop save location. The PC provider always reports
a selected device; console device-selection screens are not manufactured.

* Missing System enters content creation directly.
* Existing content is loaded into a candidate. Missing Record or SaveData is
  a failed load, with `mem.0043` acknowledgment before the parent decision.
* Other rejected loads reach `mem.0056`, the original overwrite question,
  defaulting to Yes. Yes admits content creation; No returns to the desktop
  lookup/load route. No does **not** continue with saving disabled.
* Loading uses `mem.0021` with input disabled; successful loading holds its
  progress notice for at least one second, then closes it. Load success emits
  the original logical sound 14 (common bank cue 11) without a separate
  `mem.0035` acknowledgment in the traced normal path.
* Creation uses `mem.0020`, writes System, SaveData, then Record, and holds the
  progress notice for three seconds on success. `mem.0042` acknowledges actual
  creation success and emits the same success cue. Failure fast-closes and
  returns to the desktop lookup; it cannot publish a success notice.
* Progress notices use the common native dialog's actual reveal/conceal state.
  The storage task explicitly closes it, matching `004a76d0`; disabled user
  input is not implemented by fabricating a confirmation press.
* If autosave is disabled after creation, `mem.0022` asks whether to enable it.
  The original completion does not open another overwrite question if the
  subsequent System write fails. The reconstruction retains that failed result
  and dirty System state. Recovered fresh System flags are 3; autosave uses
  bit 2 (mask 4), so ordinary fresh creation asks for this opt-in after its
  creation acknowledgment. Yes persists the System change; No leaves it off.
* Completion conceals with the original zero-duration request and waits for
  that owner to finish before publishing the character-load result.

The original startup initializer `004931c0` zeros all `0x4b000` bytes of the
checkpoint buffer. `00493fe0` and `00494000` supply that buffer and size to the
save core. Fresh content therefore contains a **307200-byte all-zero sentinel**,
not an invented playable checkpoint. The profile decoder accepts only that
exact-sized, entirely zero encoding as no checkpoint; nonzero malformed or
wrong-sized data still fails validation. Gameplay checkpoint serialization is
not implemented by this operation.

## Reconstruction save compatibility

Earlier accepted port builds wrote valid System and Record files without a
SaveData file. Treating those existing saves as corrupt would unnecessarily
invite loss of the user's progression. A deliberately documented compatibility
migration applies only after both System and Record fully validate and SaveData
is missing: write the original empty sentinel and preserve every existing
System/Record byte. Failed writes leave the active profile unchanged and enter
the original failure route. The migration is one-time and never repairs a
nonzero malformed checkpoint or a missing/invalid Record. This is a
reconstruction compatibility policy, not a claim about original game behavior.

`Profile::load_startup` owns strict admission and this migration.
`Profile::create_defaults` is the explicit replacement operation admitted by
missing System or the affirmative original overwrite decision. It publishes
fresh in-memory state only after all three writes succeed. Disk writes remain
independent replacements: a later failure may leave earlier files replaced,
which the next lookup observes. No filesystem multi-file atomicity is claimed.
The ordinary `Profile::load` compatibility API remains available to existing
consumers and also understands the original zero sentinel.

## Ownership and verification

`ProfileLoadFlow` borrows the stable profile, storage and fade owners. It owns
only bounded policy/dialog state and outputs typed original dialog identity,
copyable presentation and a bounded menu-sound batch. The frontend owns
localization/art preparation and audio submission. `MenuDialog::close` is the
shared native programmatic-close capability, independent of storage policy.
No generic Retry/Continue labels or fabricated loading prose remain in this
flow. Original English storage punctuation U+2019 uses the existing bitmap
font's apostrophe glyph through the shared prepared-text boundary.

In-memory tests exercise fresh write order and acknowledgment, exact empty
sentinel decoding, nonzero/wrong-sized rejection, missing-record acknowledgment,
default-Yes overwrite, No preserving active state and retrying, explicit Yes
replacement, partial creation failure, legacy two-file migration preservation,
migration write failure and one-time completion. Resource/fade gates remain
independent of successful storage I/O. Full frontend rendering and integration
verification belong to the parent slice's final gate.

Focused verification for this change: the shared `profile-tests` target builds
and passes, and an isolated runner of `profile_load_flow_contracts` passes. The
normal profile malformed-header fixture now uses a nonzero invalid buffer,
preserving its intended rejection test while allowing the original zero sentinel.
Original-text inspection reads purchased namespace keys through the production
`TextTable` parser; it does not rely on substituted English strings. The parent
slice owns the full repository gate and connected frontend presentation checks.
