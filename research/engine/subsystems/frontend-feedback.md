# Original front-end feedback

The front end originally delegated menu feedback to native tasks. The recovered
root Lua dispatches and waits for title/menu tasks; it does not emit their cursor,
confirmation or cancellation cues. Rebuilding the native pause menu exposed the
older front end's missing cue consumer and static main-menu presentation.

## Main-menu presentation and ownership

`MenuFlow` owns navigation, supplied presentation time, pending child acceptance,
selector responses and copied feedback. `StartupMenuSurface` owns prepared text,
resident images and bounded commands. Neither reads a clock or acquires audio
backend authority. The session supplies seconds and drains discrete sound events.
The existing renderer remains responsible for ordered sprite rendering.

Original evidence is the unpacked executable's title-menu initializer `0x49a690`,
update `0x49b320`, draw `0x49a990`, selector functions `0x4c6170`/`0x4c6230`/
`0x4c6240`/`0x4c6280`/`0x4c6430`, and sprite interpolation `0x572120`. The existing
organized pass-06r export contains these functions. The original title policy
`0x499f10` already supplied the title prompt pulse/fade reconstruction; its
missing Start cue is a separate audio output.

The main shell uses original background334, four ornaments349–352, six row
images354/353/357/356/358/355 and additional authored decoration400–403. Entry
transitions come from the layout endpoints at `0x95a748` and descriptors at
`0x95af28`, `0x95b108` and `0x95b1a8`. They retain authored staggered times,
quintic easing and the descriptor's tangent overshoot where selected. The original
1.8-second entry admission remains separate from the concurrent half-second
screen reveal. No synthetic arrow identifies the main selection.

Selected rows use white RGB; unselected rows use 0.4 RGB. Disabled rows apply the
original additional −0.11 RGB and slash image359 with the row's authored offset
and rotation. This replaces the previous opacity-based imitation. The selected
row's periodic vertical displacement uses the original amplitude and period;
confirmation applies the original 1.1 scale impulse with a 0.14-second damped
response. A separate selected-row echo follows `0x49a5c0` and the linear
response at receiver `+0x5a0`: it starts at zero and advances at one per second.
Descriptor `0x95b478` uses layouts 40/41, widening X from 1 to 3 while alpha
falls from 96/255 to zero over 0.3 seconds. This is an additional ordered draw,
not a new blend mode. These calculations reuse the shared math functions.

Descriptions come from original text namespace 2: `data/menu/9952`, six language
columns, rows 96–101. The native draw resolves `0x412000(2, selected+0x60)`, with
32-pixel text at (315,575), the original text tint and a damped opacity response.
The English column is prepared once into bounded storage; no authored description
text is checked into code or this report. Changing selection resets opacity to
0.5, then the original 0.17-second response restores it.

Main-owned child transitions preserve the selected main row while other rows
shrink/fade using the original 0.9-second descriptor. Campaign selection shifts
the ornaments to its authored arrangement. Stage/concept selection removes the
selected row after the other rows; return reverses the corresponding tracks.
Options retains the main shell through its 0.85-second exit, then conceals for
0.25 seconds before transferring presentation. Return preserves the originating
main selection rather than resetting to New Game. The child presentation reconstruction below replaces the earlier generic
list layouts; shared Options owns its own native child tasks.

Core Guidelines I.1 was retrieved for design and E.4 for implementation;
performance MEM.9 was retrieved in both phases. Time/event ownership is explicit,
invalid supplied time is rejected before mutation, and text/image preparation
stays out of frame updates. Fixed arrays bound response and draw work.

## Final departure ownership

The flow publishes the recovered conceal duration; the session owns screen
concealment and completion. Main Back/Quit uses 0.5 seconds and starts the
one-second music stop immediately (`0x49b320`). Difficulty confirmation emits
logical sound 13 (common cue 10), then its task `0x4d2110` conceals for one
second. New Game's parent observes that confirmation and starts its three-second
music stop immediately. Stage/Concept wait for their nested difficulty task to
finish before issuing the same stop. Continue task `0x4d2a10` conceals for 0.25
seconds before returning successful status; its three-second music stop also
belongs to post-conceal completion. No additional generic half-second conceal
is inserted for those mission routes. Organized passes 263 and 264 retain this
connected child-task evidence without checking recovered code into Git.

## Verification scope

Public flow tests cover entry admission, one-shot cues, changed-description
opacity, delayed Options transfer, restored main selection and rejection of
invalid time. The original-content corpus passes the purchased 9952 text into the
same surface used by the native application. The host runtime target built and
all runtime contract cases passed. Scoped fatal clang-tidy passed the flow,
surface and menu tests; the pinned formatter was applied to those files. The
parent integration owns the final repository-wide gate and original-content
corpus. A native walkthrough remains necessary: source inspection and synthetic
checks do not establish visual parity.

### Resource namespace admission

Concept titles belong to each installed concept catalog. Frontend setup mounts
only the corresponding `sprite.text.cmtxtNN` and `sprite.cmNN` menu resources
through `CatalogSet::mount_resources`; sibling scene aliases remain invisible
to the frontend namespace. The mount retains the immutable catalog and two
validated record indices, without copying payloads or loading scene content. Source identity
and archive ownership remain available to the normal ResourceService. This
bounded admission follows the recovered per-concept text and thumbnail requests without
making eighteen full scene catalogs compete in global resolution. Public
contract tests cover normalized identity, hidden sibling aliases, priority and
rejection without partial publication. Core I.1 and Performance MEM.9 were
retrieved for both design and implementation.

## Visible text audit and purchased localization

The first front-end implementation embedded English labels in `MenuFlow`,
including invented edition suffixes for concept missions. These were live
render inputs, not diagnostic names. The correction removes that authored prose
from policy and removes `StartupMenuSurface`'s generic `row.label`, `title`, and
`message` rendering fallback. Policy publishes compatibility identities and
numeric settings; the surface prepares original text and artwork once.

| Visible content | Original lookup and evidence |
| --- | --- |
| Campaign stage names | `0x4c3ec0` requests `sprite.text.sts`; namespace `sts`, keys `0001`–`0006`. |
| Concept mission names | `0x4c3b90` requests `sprite.text.cmtxtNN`, then resolves entry zero. Each installed concept supplies namespace `cmtxtNN`, key `0000`. Generic `sts` concept entries are placeholders, not the downloaded titles. |
| Difficulty choices | Draw `0x4d1b50` uses descriptors at `0x961708` and layouts at `0x9610a0`: original baked images 337/341/339/347/345, retaining their authored positions and selected tint. |
| Options labels | Shared native Options owner, full ten-row frontend context; original PC namespace-two table `data/menu/9952`, controller/calibration/localized children. |
| Costume names | `0x4e2600` resolves downloaded `costxtNN` entry zero; `0x4e2cc0` resolves namespace `onm`, key 30 plus costume ID when no downloaded title exists. Default costume therefore uses `onm.0030`, replacing the invented default label. |
| Headings | Baked original images 322 (Stage), 323 (Concept), 224 (Options), 60 (Costume); no invented Difficulty heading. |
| Continue without data | Native task `0x4d2a10` requests `mem.0060`; the acknowledgment label comes from `sys.0027`. |

Installed concept identities are 0–5, 16–21 and 32–37. Their actual text repeats
campaign names in the later groups; it does not contain our previously appended
edition labels. Front-end composition exposes only each concept's text and thumbnail aliases
through restricted catalog mounts, retaining scene resource resolution. The
surface traverses validated namespace/key/language records (English identifier
200), normalizes supported full-width characters into the existing font emitter,
and rejects missing keys instead of substituting prose. The costume parser now
uses that exact tree traversal rather than searching for a plausible English
string in the pool. Source exports remain ignored in pass 265.

The same original-content audit found main decoration 400 was absent from the
surface's residency list. It belongs to `sprite.system`, not `sprite.titlemenu`.
The surface now requests that group and uploads only the needed footer image;
there is no increase to the renderer's global texture limit. Missing image
errors include the requested identity. Final integrated build, fatal lint and
purchased-content corpus results are owned by the parent verification run; the
text repair is not claimed verified by the earlier visual-lane runtime pass.

## Earlier text/audio integration verification

Before the subsequent full child-menu reconstruction, the repository gate passed: pinned formatting, native build, all six
CTest suites, all 228 fatal clang-tidy commands, five repository-policy cases
and the full proposed text-only content check. The original-root startup corpus
also passes with purchased menu/text/audio inputs, native profile storage,
menu/costume/mission handoff, and zero live resource-domain allocations after
teardown. Its renderer/audio observations remain controlled fixtures. Separate
real FACT/FAudio disk-output evidence is described in the audio report. The
user's native visual/audio walkthrough remains the acceptance boundary; no
commit or launch is implied by these checks. Logs remain ignored under
`research/evidence/reconstruction/frontend-audio/`.

## Native child menu reconstruction

The source task boundaries now replace the generic list presentation. Policy
owns supplied-time animation, selection, dialogs and save requests; rendering
owns original art and prepared localized text. The parent session owns screen
conceal/reveal, actual persistence and Options child composition. A published
mission selection closes input but retains the child presentation so the
original selector animation continues during session-owned concealment.

- Stage/Concept: `0x4c42b0` and `0x4c5210` draw and update the original
  carousel, description panel, thumbnails, rank and decimal score. Layouts at
  `0x95fcb0` and descriptors at `0x960250` supply thirteen tracks. The carousel
  uses 298-pixel row pitch and the original 0.08-second damped scroll response;
  navigation is bounded, including three-row page steps. Read-only Profile
  best-record values come from the original record field mapping. Stage art
  uses `ssnor`, concept fallback uses `sscon`, and installed missions use their
  own `cmNN` thumbnails and `cmtxtNN` titles. Score cells retain the original
  0.8 scale and sprite center pivot; rank art retains its centered 0.6 scale.
- Difficulty: `0x4d1910`, `0x4d1b50` and `0x4d2110` retain campaign unlock
  admission, baked row images, 1.8-second entry gate, five authored strip/name
  tracks, localized `sys.0120`–`0124` descriptions and original footer. Selected
  marker 343 uses separate 0.068-second X/Y responses and 0.15-second scale/alpha
  responses, including the 1.2 entry scale impulse. Cancel shifts its target
  left by 80 and uses the source 0.25/0.3-second exit responses. Marker 344
  supplies the original confirmation echo while the session conceals the page;
  selecting a row does not teleport a renderer-owned cursor. Nested Stage and
  Concept retain the originating backdrop and selection when returning.
- Continue: `0x4d2a10` uses purchased `mem.0060` acknowledgment when no checkpoint
  exists and `sys.0010` yes/no confirmation when one exists. It retains the
  campaign-positioned main artwork. Confirmation still cannot fabricate an
  executable world checkpoint: that gameplay boundary remains independent.
- Costume: `0x4e2600`, `0x4e2970`, `0x4e2cc0`, `0x4e36f0` and `0x4e4aa0`
  supply original custom art, nine entry tracks, 172-pixel scroll rows,
  two-row page steps, current-costume marker, authored 2D preview, localized
  names and scrollbar geometry. Selection starts at the original first row;
  the current costume is a separate observation. Change confirmation uses
  `onm.0008`, completion uses `mem.0005`. Changed settings use the original
  autosave branch (`0x470d60`, System bit 2), otherwise `mem.0047` asks to save.
  Manual/automatic success use `mem.0040`/`0048`; failure uses `mem.0008` and
  returns to the save decision. The flow publishes a save request rather than
  writing storage. No-change exit reverses art for 0.4 seconds before the
  original 0.25-second screen conceal.

All common dialogs reuse the source-backed MenuDialog policy and geometry.
Fixed UI prompts consume copied input-mode observations and the shared original
icon atlas. No gameplay action mapping or mutable Profile capability enters the
surface. The prepared surface stays within the existing texture limit by
packing input icons once and removing unused Options assets from this owner.

The child source and layout evidence is retained under organized ignored
passes; no proprietary text, image payload or raw decompiler output is added to
Git. Public tests now wait for the original child input gates and confirmations,
and exercise cursor target/time ownership and continued departure animation.
The earlier gate above is not verification of this later child expansion. The
parent owns the final integrated gate and purchased-content corpus for this
snapshot; actual native visual acceptance remains distinct from either test.
