# Original rendering artifacts: footage and contemporary reports

Research date: October 25, 2026. This is an external visual/reference search,
not acceptance of the reconstruction or a diagnosis of its rendering defect.

## Result

Original Bullet Witch had documented shadow flicker and pixelation on Xbox 360,
and reviewers and a Steam user reported shadow problems in the PC release.
However, the actual frames inspected here do **not** demonstrate the same dense
bright pinpricks across Alicia and her weapon that appear in the user's current
screenshots. Known original defects therefore remain useful context, not grounds
to dismiss the reconstruction's noise or declare it faithful.

The strongest positive evidence is firsthand written reporting of unstable
shadows. The footage comparison is weaker: it gives real examples of coherent
character shading, but resolution, compression, camera position, lighting and
unknown settings prevent a fine-grained negative finding.

## Local observation and method

Inspected both supplied October 25 screenshots, identified by their filenames:

- `Screenshot 2026-10-25 at 4.54.37 AM.png`: upward outdoor camera, school outfit,
  dense high-frequency dark/light stippling over exposed arms and legs, with
  different-looking noise on cloth and weapon surfaces.
- `Screenshot 2026-10-25 at 4.52.07 AM.png`: default outfit in the mine passage;
  isolated bright pinpricks cover face, chest and weapon against otherwise dark
  character shading. The environment does not show the same conspicuous pattern.

These are static observations. The user's description supplies movement/floating;
the stills alone do not establish temporal behavior, the responsible pass, or
whether the dots are shadow comparisons, specular values or conflicting draws.

Searched original-game reviews, Steam user reports and gameplay references using
combinations of Bullet Witch, PC, Xbox 360, shadows, flickering, shadow acne,
grainy, speckles and One Shot Kill. Reused leads from
[PC brightness footage](../engine/subsystems/character-brightness-pc-footage.md)
and [Xbox brightness footage](../engine/subsystems/character-brightness-xbox-footage.md),
then freshly inspected playback and paused frames in a dedicated background
browser tab. Player timestamps were checked before reporting them. No footage
was downloaded, no account was used, and no production code or game installation
was changed. Descriptions establish provenance; they are not visual evidence.

## Firsthand reports

| Source and version | Reported observation | What it does and does not establish |
| --- | --- | --- |
| [GameSpot, Greg Mueller, February 27, 2007](https://www.gamespot.com/reviews/bullet-witch-review/1900-6166515/) — original-console release | The review explicitly reports “flickering and horribly pixelated shadows.” | Contemporary evidence of original shadow instability. The review predates PC; its current shared game card listing PC and X360 does not turn this into a PC review. No specific costume, mine location, shadow setting or pinprick pattern is identified. The article was read directly in the browser after the web fetch failed. |
| [Save or Quit, May 21, 2018](https://saveorquit.com/2018/05/21/review-bullet-witch/) — PC | The reviewer reports that enabling shadows made the presentation worse because they flickered frequently. The launcher exposes shadow, soft anti-alias and mip options. | A particularly useful firsthand association with the shadow toggle, although not a controlled frame comparison. The review also remarks on strong bloom when leaving a mining tunnel, explicitly speculating about its origin. Do not adopt that speculation as an implementation fact. |
| [Hey Poor Player, April 26, 2018](https://www.heypoorplayer.com/2018/04/26/bullet-witch-review/) — explicitly PC reviewed | The reviewer describes the appearance when passing through shadows as resembling a screen problem. | Supports visibly objectionable shadow-region behavior, but does not specify noise frequency, self-shadow acne or a missing denoiser. The author says they had not played the Xbox release; this is not a direct platform comparison. |
| [Baron O' Beefdip, Steam review, May 3–4, 2018](https://steamcommunity.com/id/baronodeefbip/recommended/696180/) — PC | Specifically reports flickering grass shadows and severe pop-in. | Direct user evidence, retrieved from the original Steam review rather than an aggregator. This concerns grass/world shadows, not demonstrated Alicia self-shadow noise. Hardware and settings are unspecified. |
| [ray_co99, Steam review, September 30, 2024](https://steamcommunity.com/profiles/76561198060279615/recommended/696180) — PC | In the presentation section, the reviewer describes Alicia as hard to see in dark areas, excessively contrasted in bright areas, and becoming dark at a low camera angle. This sentence is an English paraphrase of the Japanese review. | Additional evidence of original character-lighting/view-angle complaints. It does not identify the screenshot's pinpricks or establish their cause. |

[Respawning's April 27, 2018 PC review](https://respawning.co.uk/featured/bullet-witch-pc-review/)
also mentions flickering, pixelated shadows at high settings, but the relevant
paragraph closely repeats the older GameSpot wording. It is not counted as an
additional independent observation. Aggregator summaries and unrelated search
results are likewise not counted. An XboxAchievements search result mentions
pixelated shadows, but its full page returned HTTP 403; it is not needed for the
conclusion above.

The publisher also explicitly states that the PC port was made brighter and
received visual cleanup. Consequently, original Xbox footage is not an absolute
brightness target for the Steam reconstruction. This statement provides no
technical description of denoising or a fix for shadow flicker.
[XSEED, What's new with Bullet Witch?, May 1, 2018](https://www.xseedgames.com/2018/05/01/whats-new-with-bullet-witch-on-pc/).

## Freshly inspected gameplay frames

| Recording and verified timestamp | Platform evidence | Visible result and limits |
| --- | --- | --- |
| [Blaze Rhodon II, Dawn in the Valley, 15:15](https://www.youtube.com/watch?v=x9bF0AxqXa8&t=915s) | Title explicitly identifies PC and Steam. | Default Alicia in the mining tunnel has broad, continuous dark/light surfaces and strong blue highlights. The electric gun effect is active, so this is not a clean illumination control. Inspected at the selected 1080p60 setting; the displayed player was smaller than native resolution. No demonstrated match to the local pinpricks, but finer artifacts may be hidden. Campaign setup differs from One Shot Kill. |
| [Game in Memoria, All Costumes, 00:53](https://www.youtube.com/watch?v=6PljUusoSOQ&t=53s) and [00:58](https://www.youtube.com/watch?v=6PljUusoSOQ&t=58s) | Expanded description says Windows 10 x64, Intel G4560, GTX 1050 Ti; uploaded May 12, 2018. Storefront/build unspecified. | School outfit viewed from the front, then rear/side in the first residential area. Arms and legs have continuous shading at the viewed scale, rather than the local screenshot's conspicuous stippled coverage. Same outfit category, different level and camera; unknown shadow settings. |
| [Same PC costume showcase, 01:43](https://www.youtube.com/watch?v=6PljUusoSOQ&t=103s) and [01:48](https://www.youtube.com/watch?v=6PljUusoSOQ&t=108s) | Same explicit Windows hardware description. | Fairy outfit front and side/rear views retain patterned fabric and broad shaded skin/cloth. No confirmed match to the current pinpricks. Useful character material reference, not a same-location test. |
| [lemmy360, One Shot Kill (Hell), 00:05](https://www.youtube.com/watch?v=RaiIsq1N41k&t=5s) and [00:14](https://www.youtube.com/watch?v=RaiIsq1N41k&t=14s) | Visible upload date November 26, 2008, before the PC port; title/description identify the concept mission. Console attribution is inferred from the original platform and date, not shown hardware. | Default Alicia moves along the industrial entrance passage. At 00:14, an upward camera shows shaded character surfaces against the bright opening, with no obvious local-style dense speckles. Playback reports 480p and the image is heavily compressed/letterboxed: it cannot establish absence of fine noise. The 00:00 frame is a loading card, not gameplay. |

The inspected PC costume description also links an emulator playlist. That is
channel navigation, not evidence that this particular Windows recording uses an
emulator. Conversely, a modern upload date alone would not establish PC, which
is why the explicit hardware and title descriptions are retained above.

## Engineering disposition

- **Confirmed:** shadow flicker/pixelation was reported in the original release
  and PC port. Character darkness/contrast at different angles also has direct
  user commentary.
- **Not demonstrated:** a matching original-game sample of the current dense
  skin/weapon pinpricks, in the same scene with matched camera, costume and
  graphics settings. The search did not find a specific firsthand account
  identifying these as shadow acne, fireflies, or a missing denoising stage.
- **Not inferable from these references:** whether our current defect comes
  from shadow bias, raster precision, material evaluation, coverage/order or
  another path. Nor do these references prove that the original never shows
  similar noise; video compression and downscaling can conceal it.
- Continue the source/device investigation. Keep the original shadow quality
  limitation separate from a demonstrated reconstruction discrepancy. Do not
  add a denoiser, tune brightness or accept noisy output merely because original
  reviews criticize the shadows.

The most discriminating future visual control is the Steam executable in the
same mine position with the same costume, camera, shadow option and output size,
including a short stationary-camera and rotating-camera sequence. The links
above are supporting references, not substitutes for that controlled evidence.
