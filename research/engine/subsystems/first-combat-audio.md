# First combat audio ownership and impact resolution

This covers the ordinary player gun `wa0` and soldier gun `wg1`. It is recovered
behavior from the hash-bound PC executable and installed customization data,
not a replacement firing sound policy. Audible native acceptance remains open.

## Retained requests versus independent one-shots

`615fc0` requests contextual action 20 and refreshes an eighth-second firing
tail. `618410` preserves a matching request only when `6178e0` succeeds. That
predicate checks a valid original sound-object handle and its `+e4` bit zero;
it does **not** ask whether the backend is still playing audio.

Constructors `6153b0` and `6154d0` establish that bit through `44c2d0` and
`44c290`. These resolve the original cue token and classify a negative token
other than `-1` as retained. Contextual action 20 resolves to `0x000001b0` for
`wa0`, but `0x80000116` for `wg1`. The high bit therefore carries ownership
metadata independent of authored bank looping. Action 21 is absent for both;
action 22 resolves to nonretained recharge cues `0x139` and `0x13c`.

The distinction affects replacement and destruction. Destructor `6157d0`
stops the backend only for retained wrappers; it always unregisters the
wrapper. Repeated player shots therefore overlap and finish naturally. A new
shot detaches the old positional wrapper without stopping its audio. Soldier
shots retain their matching request; the tail path `617a30` replaces only a
retained request with action 21 when the tail expires. An absent release still
stops that retained firing cue.

`WeaponSoundCue` preserves this metadata beside the resolved request.
`WeaponAudio` owns retained cancellation and the current emitter observation.
The session `CueOutput` owns all native voices, including detached one-shots,
reaps completed voices in `update()`, and stops the world domain at mission
teardown. No per-shot collection or second clock is introduced.

## Contact sounds

`61b8e0` first chooses the surface effect and then makes a separate contextual
sound request. Weapon spec `+124` selects an action from table `855490`:
`[14, 15, 16, 12]`. Both admitted guns contain class 3 here and therefore use
action 12. `44bbd0` passes the contact surface and weapon context to `46e710`;
weapon context resolves `wa0` to class 0 and `wg1` to class 5. These are not the
struck actor's sound classes.

The default cue is `0x1e4`. Both guns have these surface overrides:

| Source surface | Cue |
| --- | --- |
| 2, 6, 7, 13 | `0xc2` |
| 3 | `0xc7` |
| 4, 34 | `0xc6` |
| 5 | `0xc3` |
| 8 | `0xf6` |
| 11, 14, 33 | `0xc5` |
| 16 | `0x90` |
| 30 | `0xc8` |
| 31 | `0xca` |
| 32 | `0xcb` |
| 35 | `0xc9` |

Player `wa0` additionally maps surface 18 to `0x90`; soldier `wg1` uses its
default for surface 18. All resolved impact tokens are nonretained. They play
independently at the contact position through bank zero. Unknown weapon/action
classes require their own original resolution, not this table as a global
fallback.

## Evidence and verification

Relevant exports are `pass-334-physics-receiver-kind` (`6178e0`, `618410`,
`617a30`), `pass-05e` (`6157d0`), `pass-320-combat-physics-effects` (`6153b0`),
`pass-05b` (`6154d0`), `pass-130-animation-audio-mapping` (`44c290`, `46e710`),
`pass-323-combat-hit-effects` (`44c2d0`), and
`pass-327-first-combat-closure` (`61b8e0`, `44bbd0`). Ignored evidence
`first-combat-effects/audio-tables.json` records the original contextual tables.
The original settings inventory independently supplies both guns' sound-class
fields. These files remain research evidence, not shipped game data.

In-process output observations verify retained deduplication, emitter updates,
pause, tail release, natural completion, destruction, and overlapping
nonretained shots surviving replacement/tail/owner retirement. They do not
claim audible playback parity. The parent integration owns cue preparation,
actual encounter dispatch, native verification and the full repository gate.
