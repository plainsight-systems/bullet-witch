# Front-end presentation reconstruction

The menu surface is a game adapter over the existing ordered sprite renderer.
It owns menu/font resource interests and device textures; menu selection and
profile decisions remain in `MenuFlow`. Dialogs reuse the same bounded value
presentation, with borrowed text valid only during `show`.

## Reference and evidence

The existing Doom 3 reference was inspected again at revision
`a9c49da5afb18201d31e3f0a429a037e56ce2b9a`,
[`neo/renderer/GuiModel.cpp`](https://github.com/id-Software/DOOM-3/blob/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/renderer/GuiModel.cpp),
particularly `Clear`, `SetColor`, `DrawStretchPic`, `EmitSurface` and
`EmitFullScreen`. The adapter keeps ordered virtual-coordinate commands and
resident image handles; it does not introduce another renderer. No reference
code is incorporated. The current renderer synchronously retires GPU work.

Original title-menu initialization `0x49a5f0` requests `sprite.titlemenu` and
`sprite.stageselect`. The static table at `0x95a748` holds the 1280x720 background
334, ornaments 349–352, then the six main entries at `0x95a838` with 48-byte
stride. IDs 354/353/357/356/358/355 are New Game, Continue, Stage Select, Options,
Concept Mission and Exit. The last image retains the filename `mt_title_online`
but its actual Steam pixels say EXIT. The adapter uses those original pixels.
The font is the already validated channel-packed original atlas.

Original main coordinates and image sizes are retained. The complete animated
child-screen layouts and scrolling embellishments are not yet reconstructed.
The costume preview is an original 2D image, reconstructed below. Submenu rows use original font/background resources with a
bounded readable list layout; this is an explicit presentation approximation,
not an original-game pixel-parity claim. Core selection semantics belong to the
separately reconstructed menu policy. Native verification must show each page.

Input keeps pressed actions separate from repeat actions. Original `0x41cfd0`
was inspected: each held bit accumulates elapsed time, emits after 0.5 seconds
and subtracts 0.125 seconds once per update; release clears the accumulator.
The native adapter maps arrows and controller directional inputs into original
navigation bits 24–27 and reproduces that arithmetic. It ignores OS key-repeat
messages, and clears all held state when focus is lost. Confirm/cancel remain
edge-triggered in consumers. This uses the recovered game algorithm rather than
an unrelated OS repeat cadence.

Design consultations: Core Guidelines E.6 (`re-raii`) for partial asset cleanup;
performance MEM.9 (`guidelines/memory/MEM.9-embedded-allocate-at-init.md`) for
fixed command/texture bookkeeping. Implementation: SL.con.3 (`rsl-bounds`) for
archive, metadata and command bounds; GPU.1
(`guidelines/gpu/GPU.1-keep-data-on-device.md`) for texture uploads only during
activation, with no per-frame image transfer. These were retrieved from both
required MCP corpora in their respective phases.

## Costume preview: original 2D assets

The costume menu uses original authored preview images rather than drawing the
character mesh. `0x4e2600` requests `sprite.cos%02d` and
`sprite.text.costxt%02d`, stores the resolved texture in row `+0x1e0`, and stores
localized text in `+0x1e4`. `0x4e2cc0` reads the selected row texture, binds it
through `0x5649e0`, and submits a sprite quad through `0x564630`. The default
costume uses sprite 445; sprite 446 is the original unavailable-image fallback.
The model reload applied after confirming a costume is a separate operation.

The adapter mounts through the existing framework resource catalog set and
loads five `PW1..PW5` previews/text plus `sprite.custom`. The original panel is at
(598,28), and the preview inset (36,36) places the image at (634,64). Assets are
498x534 or 499x535. The constructor does no file access; activation prepares all
93 menu/font/preview textures once (53,686,296 RGBA bytes). Selection submits a different texture handle,
without image decoding or file I/O. Leases and textures retire on menu end.

The title resource uses full-width Latin UTF-8 strings in an STMANAGE text pool.
A bounded costume-only reader resolves the unique English `Special Costume N`
label and normalizes the full-width Latin range for the current English menu.
This is not the general localized dialog/language-table reader. These packages
contain the costume title, not an additional prose description. The original
images and title are displayed; no invented costume description is supplied.

## Steam inline replacement PNG sizes

The real menu probe exposed a format issue hidden by the earlier title subset:
inline DDS records retain the old section-size field after a PNG replacement.
For example, `mt_title_continue.png` records 25,347 bytes but its selected inline
PNG requires 26,733 bytes through IEND. A truncated range fails libpng validation.
The archive reader now follows PNG chunk lengths from the explicit inline
pointer, bounded by the archive and a 64 MiB encoded budget. It requires the
signature, first IHDR, IDAT and zero-length IEND. It does not scan for another
PNG or guess from the next resource pointer. Original raw/section metadata stays
unchanged. libpng continues to validate CRC, compression and image decoding.

`research/tools/menu_asset_inventory.py` independently checks chunk CRCs and
uses Pillow to decode all 93 menu/preview PNGs. C++ output matches each Pillow
RGBA image byte-for-byte. The corpus verifier retains its original raw-field
comparisons and now uses this independent PNG extent calculation for inline
selected ranges; prior corpus evidence remains preserved. In-process resource
checks cover replacement extent, truncated terminal chunk, oversized chunk and
missing terminal chunk. This changes the format reader, not any game asset.

The complete resource corpus rerun also passes: 6,625 CPR archives, 25 RAT
catalogs, 29,996 records, and all 45 inline PNG extents. Generated acceptance is
under `research/evidence/reconstruction/frontend-menu/resource-corpus/`.
Focused strict compilation and fatal clang-tidy pass for the adapter, native
input, archive parser and resource tests. This is resource/policy verification;
native visual acceptance belongs to the integrated front-end run.
