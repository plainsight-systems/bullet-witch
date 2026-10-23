# Title images and original bitmap font

October 22, 2026. Native title presentation uses original assets through portable
image/font decoding and target-owned sprite rendering. No system font or generated
replacement artwork is required. The archive record tag `DDS ` is a handler name,
not a reliable payload encoding label.

## Evidence and process

Read-only `cpr_layout.inspect` inspection of `sprite/title/title.cpr` found USER
`title_info.bin`, six bytes containing little-endian sprite IDs 423, 424 and 452.
Three sibling `DDS ` records contain PNG signatures and IHDR dimensions:
`title_1.png` and `title_2.png` are 580 by 100; `title_3.png` is 460 by 64.
Only the USER record publishes `sprite.title`; sibling texture records have no
catalog publication name. The first texture selects an inline replacement image
at archive pointer base 0x28 plus raw field 2; readers must honor selected payload.
The other textures use their second-section payloads. No asset is copied into Git.

The native DDS handler 0x41a6f0 calls D3DXLoadSurfaceFromFileInMemory, supporting
this interpretation. The recovered title definitions map 424 to position (350,505),
423 to (350,505), and 452 to (412,632), in logical 1280 by 720 coordinates.
Version text is `Version 1.0.5`, size 16 at (10,700). Screen policy and animation
remain game code; image decode does not know these constants.

Generated metadata, extracted local inspection samples and reference archives live
under ignored `research/evidence/reconstruction/startup-title/`. Native exports
`pass-07a-font`, `pass-07b-font-layout`, and `pass-07c-font-layout` were produced with
`research/tools/engine_slice.py`, the existing hash-bound Ghidra project and
`ExportEngineSlice.java`. Function 0x41a180 was not an entry and produced no
function; 0x41a2b0 supplied the actual format initialization evidence.

## Production PNG reference and dependency

Read libpng 1.6.59 `example.c`, the simplified read API declarations in `png.h`,
and its CMake build, with zlib 1.3.2. The example initializes `png_image`, requests
`PNG_FORMAT_RGBA`, allocates the exact output size, finishes the read and releases
library state. Its RGBA contract is sRGB with non-associated (straight) alpha.
The engine follows that contract using a memory span, checks dimensions/byte budgets
before invoking the library, and keeps cleanup in RAII. It does not implement
compression, PNG filters, palette expansion or color conversion itself.

- [libpng 1.6.59 example](https://github.com/pnggroup/libpng/blob/v1.6.59/example.c)
- [libpng API](https://github.com/pnggroup/libpng/blob/v1.6.59/png.h)
- [zlib 1.3.2](https://github.com/madler/zlib/tree/v1.3.2)

`cmake/PngDecoder.cmake` fetches source archives into the ignored build, validates
SHA-256 and builds static dependencies. libpng archive hash is
`2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5`;
zlib archive hash is
`b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1`.
No host package discovery selects a different zlib. Library SIMD remains selected
by its established platform build; owned RGBA is the portable renderer boundary.

## FONT interpretation

`font/font.cpr` publishes USER `witchfont.ufx` as `sprite.font` and has sibling
`DefaultFontTexture`, a 1024 by 1024 RGBA PNG. The FONT payload is 78,296 bytes:
32-byte header, two 16-byte face records, 256 12-byte page records, 3,758 16-byte
glyph records and 3,758 four-byte Unicode mappings. Layout comes directly from
0x41a2b0; 0x41a250 selects a page by codepoint high byte and binary-searches its
sorted mapping records. Each mapping stores u16 codepoint and u16 glyph-relative
index, added to the page's glyph base.

Header offsets 4/8/12 store mapping/face/glyph counts. Offset 24 is atlas width;
offset 20 is twice atlas height, shifted right by the original initializer.
The first face stores pixel size 33 and baseline 28. Glyph records hold four u16
atlas rectangle coordinates, four signed byte bearing coordinates, flags at byte
12 and signed advance at byte 13. Remaining bytes are retained in original input
but not used by this title draw path.

Function 0x413130 selects glyph coverage from packed atlas channels: flag 1 blue,
2 green, 4 red, 8 alpha; flag 0x40 is blank but advances. Other glyph modes are
explicitly unsupported by this bounded reader; none occur in the inspected font.
Version text uses blue and green glyphs. Rendering samples the selected channel as
coverage with white RGB, then applies text tint/opacity. This is essential:
drawing the packed RGBA image normally would mix unrelated glyph shapes.

At size 16, glyph scale is 16/33. The baseline position is y + 28*scale; the signed
bearing rectangle offsets that position. Advance is the signed per-glyph value
scaled by 16/33. No guessed fixed-width spacing or platform font metrics enter.
The legacy D3D9 half-pixel vertex adjustment belongs to that raster API and is not
applied to Metal's logical-pixel quad positions.

## Boundaries

`bw::formats::decode_png` returns independently owned RGBA8. `BitmapFont::parse`
returns validated owned glyph/Unicode metadata; lookup is logarithmic and returns
an independent glyph value. Neither decoder performs I/O, publishes resources,
creates native GPU objects or owns game policy. Resource leases retain archive
bytes; decoded images and uploaded textures have separate readiness/lifetimes.
Current standard-container allocation remains covered by the deferred memory
subsystem milestone. This slice does not claim full text shaping/localization.

## Verification

The in-process `image-font-tests` target passed synthetic straight-alpha pixel,
truncation, output-budget, corruption, signed bearing, missing-glyph, section/map
bounds and unsupported-flag checks. Two contract groups are exercised; individual
assertions are not reported as separate tests.

A read-only original-asset probe decoded all 14 images (three title overlays,
one font atlas and all ten selectable loading images). Every RGBA byte matched
Pillow's independent PNG decoding. The native FONT reader accepted all 3,758
records; every nonblank record's bitmap dimensions matched its signed bearing
rectangle dimensions. The version string resolved all original glyphs. Generated
probe source/output and comparison hashes remain under the ignored evidence
folder; no new process-launching test scenario was added. These checks establish
asset interpretation and ownership, not native visual equivalence.


## Native verification correction: packed channel identity

The user observed corrupted version characters in the first native title run.
The initial interpretation incorrectly read native mask integers as ARGB color
names: flag 1 became red and flag 4 blue. Those values identify the opposite
channels in the PNG atlas. Direct inspection of the original atlas cropped by
recovered glyph coordinates resolves the ambiguity: `V`, `0`, `1`, `5` and `.`
(flag 1) are legible in blue, while red contains unrelated overlapping glyphs;
`e` (flag 2) is in green; `¾` (flag 4) is in red; `ñ` (flag 8) is in alpha.
The recovered masks 0x00ff0000, 0x0000ff00, 0x000000ff and 0xff000000 have their
nonzero bytes at indices 2, 1, 0 and 3. The decoder now converts flags to canonical
PNG channels B/G/R/A once during metadata parsing. Renderer sampling stays RGBA.

The earlier pixel byte comparison was valid but insufficient: it proved PNG
conversion preserved input, not which packed channel belongs to each character.
An added in-process format contract explicitly verifies all four flag mappings.
Ignored `glyph-channel-inspection.txt` records independent original-glyph crops
as thresholded pixel grids, including wrong-channel comparisons. Native replay
and a readable version label remain the acceptance check for this correction.
