# Aiming exit and costume footage

## Scope and method

The October 24 user review reopened targeting exit, overlapping weapon-lower
and melee animation, and excessive character brightness, especially Special
Costume 5. This is a bounded visual-reference investigation, not a source-code
finding or a pixel-matched acceptance test. Viewed original-game uploads in a
dedicated browser, using paused frames and player frame stepping. The publisher
stream's exported transcript located relevant discussion; no media was downloaded.

## Publisher PC reference

[XSEED's PC gameplay livestream](https://www.youtube.com/watch?v=XAkp-wx6zgY)
identifies itself as staff playing Windows PC, with a pre-release April 25
availability banner. The exact executable revision is not disclosed.

- [09:58–10:21](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=598s): the
  transcript describes a PC change retaining the raised gun for roughly three
  to five seconds, whereas the older version lowered it immediately. This
  corroborates an intentional retained posture, not the precise binary timer
  value or the timing of a targeting-mode button edge. The observed frame at
  10:17 has the gun carried behind Alicia.
- [16:44–16:49](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=1004s): discussion
  mentions melee, but inspected frames show firing followed by reload; ammunition
  changes from 3 to 80. This is **not** evidence of melee interrupting lowering.
- [01:14:05](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=4445s): the menu
  explicitly labels the pink-haired blue-and-white fairy outfit **Special
  Costume 5**. This is a preview illustration, not a scene-lighting sample.

No inspected interval establishes the second right-stick click, its precise
camera response, or a melee request during an active lowering animation.
Do not infer those input/state transitions from a zoomed camera or weapon pose.
The original input/action source must establish those contracts.

## In-level costume comparisons

[Gameplay World JK, final costume, 05:03](https://www.youtube.com/watch?v=4haTz7S4uOc&t=303s)
shows the same pink-haired fairy outfit in the final ruined-city area. Blue
fabric and pale trim are conspicuous, while exposed skin/back remain shaded
and visibly darker. This is not a uniformly white or equally illuminated body.
The upload is dated April 5, 2019, but its title and inspected description do
not establish the platform. A recent upload date alone does not prove PC.

[Yuki Ren, fairy costume, 02:04](https://www.youtube.com/watch?v=MNQd3MZzPnQ&t=124s)
is explicitly labeled Xbox 360. It likewise shows bright trim alongside shaded
skin in the first city area. Its old compressed capture and different stage
prevent numerical comparison with the Steam reconstruction.

These references establish appearance and material contrast only. Neither is
the current One Shot Kill location, nor a controlled exposure/camera comparison.
They cannot justify reducing a global brightness multiplier, adding a costume
exception, or declaring the current lighting correct. Trace the original
material selection, lighting parameters and shader evaluation for the reported
costume, then use corresponding native captures to verify that correction.

## Unresolved visual reference

A matching PC capture of target exit followed immediately by melee, with
observable input, was not found in this bounded pass. Nor was a platform-verified
Special Costume 5 sample at the current One Shot Kill location. These limits
remain explicit; the publisher's posture discussion does not close either gap.
