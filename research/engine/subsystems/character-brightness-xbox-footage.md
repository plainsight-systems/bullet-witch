# Character brightness in original Xbox footage

This October 24 comparison asks whether the bright Special Costume 5 appearance
is visible in the original Xbox 360 release. The inspected footage does not
establish the reported white-looking body as an original bug. It supplies two
useful, separate controls: the same fairy costume in another stage, and the
same One Shot Kill starting area with the default costume. Neither is an exact
original-runtime reproduction of the current PW5 frame.

## Method and local comparison

Videos were opened in a dedicated browser tab, paused, sought with the visible
player controls, and inspected as actual gameplay frames. Titles, upload dates,
and descriptions were read on the watch pages. No conclusion below comes from
a thumbnail or transcript. No remote video was downloaded and no production
code or original game files were changed.

The local comparison was the settled native PW5 capture
`research/evidence/reconstruction/character-light/exposure-sequence/pw5/frame-239.png`.
It shows the closed industrial gate in the starting cave, a very bright
blue/white/gold fairy outfit, bright pink hair and bright skin. Rock and gate
textures remain clearly illuminated. This image follows 240 presentations;
the existing [material brightness audit](character-material-brightness-audit.md)
records the exposure experiment and its limits. The screenshot itself does not
establish that the original live scene supplied the same inputs.

## Same costume, original Xbox release

[Yuki Ren, Fairy costume—Bullet Witch, Xbox 360](https://www.youtube.com/watch?v=MNQd3MZzPnQ)
is explicitly labeled Xbox 360 and was uploaded August 24, 2008. The date also
predates the PC port. It shows the pink-haired fairy outfit in the first city
area, not One Shot Kill.

| Inspected timestamp | Visible observation |
| --- | --- |
| [02:08](https://www.youtube.com/watch?v=MNQd3MZzPnQ&t=128s) | Exposed back/shoulder skin has warm shading; pale costume ornament remains conspicuous, but the body is not uniformly white. A red screen-edge effect is present, so this is not a clean color reference. |
| [05:20](https://www.youtube.com/watch?v=MNQd3MZzPnQ&t=320s) | A side-facing movement pose shows darker blue fabric and legs, shaded skin, pink hair, and brighter trim. The costume can appear substantially darker than the settled native PW5 cave image. |

This rejects the idea that an always-white fairy body is demonstrated by this
recording. It does not rule out a stage-specific original lighting defect. The
old, compressed, letterboxed capture has unknown capture settings and game
brightness, and the scene has different illumination.

## Same mission area, original-console-era recording

[lemmy360, Bullet Witch—One Shot Kill (Hell), part 1](https://www.youtube.com/watch?v=RaiIsq1N41k)
was uploaded November 26, 2008. Its title and Japanese description identify the
One Shot Kill concept mission at Hell difficulty. The upload predates the 2018
PC release; Xbox 360 attribution follows that chronology and the original
release platform, rather than a claim that the recording shows console hardware.
The player uses the default dark outfit.

| Inspected timestamp | Visible observation |
| --- | --- |
| [00:01](https://www.youtube.com/watch?v=RaiIsq1N41k&t=1s) | Alicia is inside the industrial gate/cave entrance. Both her body and the surrounding entrance walls are dark; the ground beyond is brighter. The gate is open and she is already moving, unlike the local closed-gate pose. |
| [00:06](https://www.youtube.com/watch?v=RaiIsq1N41k&t=6s) | In the adjacent passage by large diagonal supports, Alicia and the nearby wall remain shaded while the exterior is bright. This later frame helps avoid interpreting the first frame alone as a startup fade. |
| [00:11](https://www.youtube.com/watch?v=RaiIsq1N41k&t=11s) and [00:16](https://www.youtube.com/watch?v=RaiIsq1N41k&t=16s) | The shaded character and passage contrast strongly with the bright opening into the exterior. |
| [00:31](https://www.youtube.com/watch?v=RaiIsq1N41k&t=31s) and [00:36](https://www.youtube.com/watch?v=RaiIsq1N41k&t=36s) | In the exterior, her skin and environment are much brighter while the dark outfit retains tonal separation. Both inspected frames include firing/muzzle flash, so they cannot isolate sunlight, indirect light or exposure response. |

Compared qualitatively with local `frame-239.png`, the original starting-area
recording is markedly darker in the character **and environment**. This is a
reason to keep the investigation wider than a costume-specific multiplier. It
does not identify whether a producer, render pass, output transform, version
difference, camera position, gate state or recording setting causes that
difference. These are comparison limits, not proposed fixes.

The video is low resolution and compressed. Original display brightness,
capture gamma/range, output conversion and game revision are not disclosed.
The local and recorded cameras, poses, costume and gate state differ. Therefore
no numerical exposure, gamma, SH or material factor can be derived from these
frames, and the evidence does not authorize a brightness adjustment.

## Disposition

Original Xbox footage provides no demonstrated match for the severe settled
PW5 appearance. The same costume can be shaded, and the same mission's cave
can shade Alicia and its environment together. This supports continuing the
reconstruction investigation; it does not prove the original game never had a
lighting bug or that the PC port matches the Xbox result.

The missing discriminating sample remains PW5 in the same One Shot Kill cave,
preferably in the Steam build with known game brightness and an original live
graphics-state capture. The [earlier footage report](aim-exit-and-costume-footage.md)
contains other costume observations, with their platform and scene limits.
