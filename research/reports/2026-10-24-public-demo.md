# Public demonstration and README media

October 24, 2026. Video work is parked. Current scope: retain the README wording
and capture option, and add one externally hosted title-screen still.

Show the reconstruction that runs on the development Mac, with Cavia visible
at the top of the public repository. Preserve the project's purpose: readable
source, a faithful port to Xbox Series X, then improvements. The current native
Metal build demonstrates startup, title, interactive menus and costume selection,
followed by bounded One Shot Kill traversal. It does not demonstrate a completed
mission or combat simulation.

## Repository description

GitHub description applied and verified October 24, 2026:

> Unofficial engine reconstruction of Cavia's Bullet Witch in C++20, running natively on Apple silicon. Requires purchased game data.

## Release facts

Primary sources checked October 24, 2026:

| Fact | Source and scope |
| --- | --- |
| Cavia developed the original game; Xbox 360 was its original platform | [Original official Bullet Witch site](https://www.marv.jp/products/bulletwitch/) retains the Cavia and Xbox 360 credits. Its Flash presentation is not required to read those credits. |
| Original release year: 2006 | [XSEED's April 11, 2018 announcement](https://marvelous-usa.com/2018/04/11/xseed-games-unveils-a-marvelous-lineup-of-pc-and-console-titles/) expressly identifies the original Xbox 360 release as 2006. Use this first-release year rather than the later regional 2007 date. |
| Steam PC release: April 25, 2018 | [Steam product page](https://store.steampowered.com/app/696180/Bullet_Witch/) lists that release date; the publisher announcement also names Steam and the date. Steam's present developer field names Marvelous, Inc.; it is not the source for the original Cavia attribution. |
| Cavia also developed the original NieR, with the Drakengard team | [Square Enix's February 2, 2010 announcement](https://www.jp.square-enix.com/company/ja/news/pdf/e76b423e2a44ee159e5f99da904fbfae.pdf), page 1, credits Cavia and identifies the Drakengard development staff. This refers to the original NieR, not every later game in either series. |

## Media and the repository rules

Reviewed [NOTICE](../../NOTICE), [LICENSE](../../LICENSE),
[LICENSE-CONTENT](../../LICENSE-CONTENT), [.gitignore](../../.gitignore) and
the [repository content guard](../../tools/check_repository.py).

The project licenses cover its own code, writing and original visuals. NOTICE
explicitly excludes the original game's code, scripts and audiovisual assets.
A gameplay capture contains those audiovisual elements; publishing it does not
make them project-original artwork or license them under CC BY. The text-only
repository guard rejects binary media, and ignored evidence remains local.

The approved arrangement keeps capture files outside Git and links to external
demonstration media. It preserves those repository boundaries and license scope;
it is not a claim that repository policy grants rights to the game's imagery.
No license text, content guard or evidence requirement is changed.

Place one still captured from this build's actual title screen beneath the
README title, above the first paragraph. Host it on R2 at
`media.plainsight-systems.com`, with only its reviewed URL in Git.
[GitHub supports externally hosted Markdown images](https://docs.github.com/en/get-started/writing-on-github/getting-started-with-writing-and-formatting-on-github/basic-writing-and-formatting-syntax#images).
Do not add a video link, GIF or loop in this pass.

The README embeds the actual hosted still described below.
Keep the plain-text project title; no fan-art title treatment is planned.

Beside the media, retain:

> Requires purchased Steam game data. Captured game imagery and audio remain the property of their respective rights holders and are not covered by this repository's licenses.

With the still, record the actual capture date, commit and working-tree
state, output resolution, and **Apple M3 Max · native Metal**. Do not insert a
commit identifier before knowing which build was recorded, or describe a dirty
build as precisely matching a published commit.

## Parked video plan

Retained for a future request. Recording and editing the video are outside the
current still-image pass; the existing capture option remains available.

Use `./run --capture`, exactly as documented in the README. The
[native host notes](../../docs/native-startup-host.md#recording-a-demonstration)
describe preparation and the limits. Capture mode hides developer UI and routes
diagnostics away from the terminal; it preserves the ordinary runtime. It does
not accelerate startup, replace content, change game state or supply missing
menu audio. Keep captured files under ignored `research/evidence/public-demo/`
or outside the checkout.

Build beforehand with `./run --build-only`. Film the actual uninterrupted launch
with the incremental build check still in place. Set up a clean terminal prompt
that does not display private/local paths, and exclude unrelated windows. The
command itself needs no machine-specific path with the standard purchased-data
location. Recording and editing tools are external to this engine option.

The following timing is an editorial target, not runtime behavior. The full
clip must now show an outfit change, movement, evasion, weapon switching and
melee. Leave room for these actions, the measured launch interval and a readable
end card; extend the cut beyond 28 seconds or shorten the logo/cinematic excerpts.
Do not squeeze the actions into the old three-second traversal slot. Captions
sit in the bottom safe area and use six words or fewer.

| Target | Shot | Caption |
| --- | --- | --- |
| 0:00–0:02 | Animated title with its actual audio | Bullet Witch (Cavia, 2006) |
| 0:02–0:05 | Desktop, terminal command, fullscreen in one take | Native on Apple silicon. No emulator. |
| 0:05–0:07 | Boot logos, accelerated only in editing | Original boot sequence · 3× playback |
| 0:07–0:11 | Opening cinematic excerpt | Original intro. Native playback. |
| 0:11–0:14 | Title confirmation, menu input registering | Original Lua. Reconstructed native menus. |
| 0:14–0:21 | Three or four costume selections | Costume select, fully interactive |
| After costume selection | Real loading into One Shot Kill | One Shot Kill: traversal in progress |
| Traversal sequence | Move, evade sideways then forward/back, switch weapon, melee | Movement, evasion, weapons, melee |
| After traversal | Forward-looking caption, no full-mission claim | Working toward a complete mission. |
| End | Readable project identification and data requirement | See end-card text below |

End-card text is a separate information card rather than a six-word caption:

> Bullet Witch, reconstructed in C++
>
> github.com/plainsight-systems/bullet-witch
>
> Bring your own Steam copy. No game assets included.

Let the selected outfit preview settle before entering One Shot Kill. In the
scene, show ordinary movement first, then sideways and forward/backward evades,
followed by the weapon switch animation and melee. Give each action enough time
to read. These are live traversal and action presentation, not evidence of
ammunition, hits, damage, enemies or completed combat simulation.

The front-end outfit previews are original authored 2D images. In-scene Alicia
is rendered geometry. Avoid editing or wording that conflates those two views.
The original Lua orchestrates screen flow; reconstructed native code implements
menu behavior. Title and movie audio work; menu background music and confirmation
cues remain incomplete. Keep those distinctions visible in the demonstration.

## Published still

The title-screen still was captured and visually checked locally at
`research/evidence/public-demo/title-screen-2026-10-24.png`: the unedited native
title window at 3456 × 2168, with the START prompt and version text and no
developer overlays. The lower-right **Alpha · macOS** label identifies the
reconstruction. The capture was made October 24, 2026 on Apple M3 Max with Metal,
from base commit `9e6be4dbb260242238dee9393b80aea99aaedd8b` plus the then-uncommitted
capture option, fullscreen failure diagnostics and title label. It is not a
claim that the image was generated by that base commit alone.

The user authorized publication of this still. Its immutable
[public image URL](https://media.plainsight-systems.com/bullet-witch/title-screen-2026-10-24-0dbf54587d10.png)
serves the exact captured PNG from R2 bucket `plainsight-public-media`, with
`Content-Type: image/png` and `Cache-Control: public, max-age=31536000, immutable`.
Public HTTPS delivery returned 200 and matched the local SHA-256:
`0dbf54587d10472df83c337de28784c0b94af620b03ea7efd7fd989382f79244`.
Capture metadata and verification files stay ignored beside the original; no
image bytes enter Git. Video remains parked.
