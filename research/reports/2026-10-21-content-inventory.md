# Costume and bonus mission inventory

The downloaded Steam build contains **five explicitly labeled Special Costume
packages and 18 named mission packages** under `data/concept`. This inventory
identifies all 23 directories and their 119 files. It records bundled content;
it does not establish that every package is selectable in-game or reconcile the
complete historical Xbox DLC catalog.

Research date: October 21, 2026. Titles come from the packages' own text resources.
Their UTF-8 text uses many full-width Latin characters, normalized here with
Unicode NFKC for readability. Original strings, file hashes, resource names, and
script members are preserved in the [inventory evidence](../evidence/content/concept-packages.json).

## Costumes

Each `data/concept/PW1` through `PW5` package contains a model mesh, textures,
collision-related and control-point data, character setup records, text, and a
costume preview image. These are substantive character packages, not just menu
thumbnails. The main `PW0` package remains under `data/data/stable/graphics/chara`.

| Package | Bundled English label | Description of its bundled preview | Files |
| --- | --- | --- | ---: |
| `PW1` | Special Costume 1 | Silver or white fitted suit and tall boots | 11 |
| `PW2` | Special Costume 2 | School uniform with plaid skirt and blue hair | 9 |
| `PW3` | Special Costume 3 | Cropped blouse, short dark skirt, glasses, and green hair | 9 |
| `PW4` | Special Costume 4 | Bandaged outfit with orange top and dark hair | 9 |
| `PW5` | Special Costume 5 | Blue and gold fairy-like outfit with pink hair | 9 |

The descriptions are visual observations, not official costume names. All five
existing preview PNGs were copied without conversion and visually inspected. Their
bytes match the indexed CPR members. This confirms what the previews depict; the
3D meshes have not been rendered to verify their appearance.

Preview sources are `extend/cosspr.cpr`, containing `costume_change01.png` through
`costume_change05.png`. The text is in `extend/costxt.cpr`. Local preview copies
are in `/Users/andyhunter/repositories/tech-demos/bullet-witch/research/extracted/costume-previews`.

## Named challenge missions

Each mission directory contains four files: its RAT index, a CPR wrapping a DPK
with scripts and level resources, `cmspr.cpr` with a preview PNG, and `cmtxt.cpr`
with localized text.

| Package under `data/concept` | Bundled English title | Stage prefix found in DPK member names |
| --- | --- | --- |
| `cm00_00` | Geist Search and Destroy | `st05` |
| `cm01_00` | One Shot Kill | `st04` |
| `cm02_00` | Caterpillar City | `st02` |
| `cm03_00` | 7 Minutes | `st03` |
| `cm04_00` | Dynamite Beauty | `st01` |
| `cm05_00` | Endless | `st02` |

Stage prefixes are observed asset references, not proof of a mission's complete
runtime loading behavior. The challenge titles do not establish their mechanics;
those await script evaluation or gameplay.

## Campaign variants

| Base title from bundled text | Extra Edition package | Great spells unlocked package | Stage prefix |
| --- | --- | --- | --- |
| Clad in Streaming Black | `cm16_00` | `cm32_00` | `st01` |
| City of Screams | `cm17_00` | `cm33_00` | `st02` |
| 10,000 ft of Flames | `cm18_00` | `cm34_00` | `st03` |
| Dawn in the Valley | `cm19_00` | `cm35_00` | `st04` |
| The Bound Soul | `cm20_00` | `cm36_00` | `st05` |
| The End of Chaos | `cm21_00` | `cm37_00` | `st06` |

The actual labels append “Extra Edition” or “Great spells unlocked” on a second
line. These 12 packages plus the six challenges account for all 18 `cm*`
directories. Each has a corresponding `cmXX_00.lub` member in its DPK. Some also
contain other stage Lua members; the full member lists are in the evidence.

## Model container coverage

The complete downloaded inventory has **857 paths ending in `model/mesh.cpr`**:
five in costume packages, 65 under `graphics/chara`, 178 under `graphics/bg`, 555
under `graphics/bgobj`, and 54 under `effect` (21 `boss1`, 33 `common`). These are
container counts, not a count of distinct characters or rendered models. One
container may hold multiple buffers, and other resource paths may also contain
geometry. See [the path list](../evidence/content/mesh-container-counts.json).

## Evidence and follow-up

- [Package table](../evidence/content/concept-packages.tsv): compact IDs, titles,
  stage references, file counts, and sizes.
- [Detailed package inventory](../evidence/content/concept-packages.json): all 119
  files with hashes, CPR members, DPK entries, and original label strings.
- [Visual identifications](../evidence/content/costume-identifications.json):
  preview paths and hashes with the observed costume descriptions.
- [Inventory script](../tools/content_inventory.py): repeatable, read-only inspection.

This closes the short inventory pass. The next planned research is startup-script
evaluation. Detailed model previews, mission mechanics, unlock behavior, and a
historical DLC completeness comparison remain later work.

Reproduce the package inventory from the project root:

```sh
python3 research/tools/content_inventory.py /Users/andyhunter/Games/BulletWitch-Steam research/evidence/content
```
