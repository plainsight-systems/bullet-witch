# Character brightness in PC footage

The inspected Windows footage does **not** support treating Special Costume 5's
near-white appearance as an unavoidable original costume effect. It shows the
fairy outfit retaining shaded skin and patterned cloth. It does not yet settle
whether One Shot Kill has a location-specific original defect: no verified PC
capture of this costume at the reconstruction's exact cave-door spawn was found.

## Method and platform evidence

On October 24, 2026, searched the web and YouTube, then inspected actual playback
and paused frames in one dedicated browser tab. No video was downloaded. These
are visual references, not calibrated pixel comparisons; encoding, recording
range, game brightness settings and executable revision are unknown.

| Reference | Platform evidence | Inspected observation |
|---|---|---|
| [Game in Memoria, 01:42](https://www.youtube.com/watch?v=6PljUusoSOQ&t=102s), published May 12, 2018 | Expanded uploader description lists Windows 10 x64, Intel G4560 and GTX 1050 Ti. This supports a PC recording; the storefront and executable revision are unspecified. | Pink hair, blue/cream patterned bodysuit and trailing panels identify the fairy outfit. In the first residential city area, face, torso and pale fabric retain shading and detail. |
| [Same capture, 01:45–01:49](https://www.youtube.com/watch?v=6PljUusoSOQ&t=105s) | Same recording | The camera rotates around the live character. Side and rear fabric become markedly darker blue; pale edging remains distinct. The body does not remain uniformly luminous as the viewing direction changes. This is rotation at the same location, not a demonstrated walk between lighting volumes. |
| [XSEED PC livestream, 01:14:08](https://www.youtube.com/watch?v=XAkp-wx6zgY&t=4448s) | Publisher description explicitly says Windows PC; Steam/Humble release banner is visible. | Menu labels the same fairy appearance Special Costume 5. This is an illustration and supplies identity evidence only. |

The Windows showcase's unrelated emulator-playlist link is channel navigation,
not a claim that this game is emulated. Its explicit capture hardware description
is stronger evidence than upload date alone. Nevertheless, it does not disclose
the game's installation/storefront.

## Steam valley cave comparison

[Blaze Rhodon II, Dawn in the Valley, 14:59–15:18](https://www.youtube.com/watch?v=x9bF0AxqXa8&t=899s)
is titled **Bullet Witch (PC, Steam) Walkthrough Part 4 Dawn in the Valley**.
Keyboard/mouse prompts are visible in the loading sequence. It shows the default
costume in the campaign's final mine/base section, with rock walls, wooden tunnel
supports, rail tracks and overhead lamps. Alicia remains dark against the lit
ground. An active electric weapon effect produces blue highlights, so this is
not an unenchanted lighting control. At
[15:33](https://www.youtube.com/watch?v=x9bF0AxqXa8&t=933s), she is similarly dark
at the base entrance while firing over bright ground.

This inspection did not establish the exact hazard-striped spawn doorway shown
in the reconstruction. Campaign mission configuration, character position,
camera and weapon effects differ from One Shot Kill. The footage is useful
evidence that the PC renderer supports dark character shading in the valley's
mine/base setting; it is not proof of identical stage lighting inputs.

## Comparison and disposition

The local settled diagnostic `character-light/exposure-sequence/pw5/frame-239.png`
shows broad near-white areas on the fairy's pale panels and legs, alongside
strong blue patterns and bright pink hair. The PC city showcase has substantially
more visible cloth and skin shading. Different stage illumination prevents
converting that contrast into a correction factor or concluding which upstream
input differs.

No inspected footage identifies the reported glow as a documented original PC
bug. The demonstrated source shader agreement remains agreement on supplied
inputs; these external frames do not close whole-scene parity. Preserve the
brightness finding as unresolved, with the direct PC fairy reference available
for review. The separate Xbox investigation supplies the stronger exact-spawn
geometry comparison. No production changes or new acceptance claim result from
this footage pass.
