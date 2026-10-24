# Original loading artwork and tip layout

This note records the narrow source reconstruction supporting the loading surface.
Production presentation is described in [loading-presentation.md](loading-presentation.md).
The draw owner is `0x4a6c00`; repeated sprites are emitted by `0x4a69a0`.
The ignored `pass-mission-load-first` export includes both pseudocode and exact
instructions. Stack arguments in the latter resolve ambiguities in the decompiler.

## Animated groups

For each group let `d = (sin(theta), cos(theta))`. Sprite index `i`, beginning at
zero, has unrotated top-left `base + d * (slide + i * spacing)`.
Rotation is independent of spacing direction. All coordinates use the original
1280 by 720 canvas.

| Group | Sprite ID | Base | Direction | Count | Spacing | Sprite rotation | Slide descriptor |
| --- | --- | --- | --- | --- | --- | --- | --- |
| A | 448 | (0, 90) | 90 degrees | 11 | 120 | 0 | `0x95d954` |
| B | 448 | (531, 604) | 90 degrees | 7 | 120 | 0 | `0x95d964` |
| C | 448 | (246, 384) | 250 degrees | 5 | 120 | 20 degrees | `0x95d974` |
| D | 448 | (1200, 190) | 250 degrees | 8 | 120 | 20 degrees | `0x95d984` |
| E | 447 | (172, 370) | 0 degrees | 7 | -80 | 0 | `0x95d994` |
| F | 447 | (409, 370) | 0 degrees | 7 | -80 | 0 | `0x95d9a4` |

A through D interpolate -1280, +1280, +1280, -1280 respectively to zero over
0 to 0.8 seconds, through `0x571de0`. E and F interpolate -1280 to zero through
`0x571c90`, with times (0.05, 0.87) and (0.1, 0.92). Constants are preserved in
ignored `research/evidence/reconstruction/loading/loading-draw-constants.json`.

`0x564630` marks position mode with bit 2. `0x563110` obtains sprite width and
height and builds the rectangle from position to position plus dimensions. The
center-position flag (bit 4) is absent. Rotation defaults to the rectangle
center; explicit rotation pivot flag 0x20 is also absent. `0x564870` writes its
angle into the Z rotation component. Therefore a group position is the unrotated
top-left, while each sprite rotates around its own center.

## Tip resource and formatting

`0x412110` loads the three filenames at `0x86f2bc`: `data/menu/9950`, `9951`, and
`9952`, into the category table at `0xa0e1c0`. `0x412000(category, textID)` reads
language column `0xa0b6d4`. The loading draw uses category 1, hence `9951`.
Raw files begin with uint32 row count and then six uint32 file-relative string
offsets per row. The lookup is `4 + 4 * (textID * 6 + language)`; zero means absent.
Language 1 is English. Strings are UTF-8, converted by the original text owner.

The loading item selects a uint16 text ID from `0x95d910`. Current item 5 selects
row 5. The shipped 9951 file has 18 rows, valid 0 through 17. The executable's
larger item map contains IDs 18 and 19, but those are not valid rows in this file:
reading them would treat the following UTF-8 string bytes as pointers. They must
remain an explicit unsupported/missing-resource case.

Text starts at (540, 152), font size 28. `0x4bb350(4)` sets line spacing at owner
+0x24; it does **not** set alignment. Default alignment flags are 3 (left/top),
from `0x4ba1f0`. `0x4ba620` advances size plus spacing, hence 32 pixels. It skips
consecutive CR/LF after each line, so empty lines do not independently increase Y.
Text fades from zero to one between 0.7 and 1.0 seconds through `0x571db0`.

The rich-text parser is `0x4ba9a0`, already exported in `pass-07a-font`.
Default ARGB is `0xffe6e4d5`. Color `end` restores that base color. Named colors
multiply current channels through `0x4ba020`: red `0xffff8080`, yellow
`0xffffff80`, blue `0xff8080ff`, sky `0xff4de5e0`. Text fading controls alpha.

Item 5 includes two inline control icons. `kico mgc` resolves through `0x4b9c90`
to control-icon ID 13. `ico key_b` resolves through its 20-byte table record at
`0x95faa0 + 3*20` to ID 134. Both have authored offset (0, -7). Scale is
font size / 32 = 0.875. The parser corrects icon Y by subtracting
`(1-scale) * 0.5 * authoredOffsetY`, giving -6.5625 relative to text Y. Icons use
white RGB and inherited alpha. Advance is the icon measurement from `0x472720`
multiplied by the font scale. `0x4ba3e0` forwards these to `0x472650`, which splits
ID into low-seven-bit control and high-bit variant before `0x472270`.

The PC control artwork is a separate pack, `data/menu/9900`, loaded by
`0x472870`. Its uint32 header is image count 149 and maximum payload size 9171,
followed by 150 file-relative offsets at byte 8. Consecutive offsets bound each
PNG payload. Images occupy the original handle array at `0x12f4938`; index is
`(handleAddress - 0x12f4938) / 4`. These are not system-sprite IDs.

For the original XInput default configuration, `0x41ca70` selects byte table
`0x827eb0` and device mapping row zero at `0x82d9c0`. The magic control reads
configuration slots 8/9 (physical codes 8/9), selecting image indices 124 and
125. The plus sign is index 148. The B control reads slot 11 (code 13), selecting
image 121. The image payloads are all 128 by 128; image 121 was visually checked
against the extracted original B-button artwork. Other input schemes/remapped
controls need the corresponding original mapping, not these fixed defaults.

Inline image scale is `s = fontScale * 0.933333993 * 0.333333343`, or about
0.2722224 for 28-pixel text. Image size is therefore about 34.84447 pixels.
The original composition is unusual: `0x472270` sets the unscaled rectangle
position P only once. `0x5648e0` changes the **scaling pivot**, not position.
For the three magic components the pivot is `P + (i*q, 0)`, where
`q = fontScale * 0.933333993 * 64 * 0.8` (about 41.81336 pixels). Scaling the
unchanged rectangle around this pivot places its final top-left at
`P + (i*q*(1-s), 0)`, about 30.43083 pixels apart. Preserve this source matrix
rather than interpreting the pivot as the final placement. The single B image
uses i=0. Text cursor advance remains separate: about 104.53334 for magic and
39.2 for B. The final icon alpha is inherited from text, RGB is white.

## Tooling and evidence

The investigation reused existing source exports, read constants from the
unpacked original executable with `research/tools/engine_image.py`, and examined
bounded instructions with `llvm-objdump`. Additional bounded exports
`pass-86-loading-icons` and `pass-87-loading-icon-sprites` retain the exact
command, seeds, pseudocode and call metadata. The game directory was read only.
No copied game artwork, strings or executable bytes are added to tracked files.
