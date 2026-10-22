# Cavia engine research — existing reverse engineering

Researched October 22, 2026. [Architecture](../engine/ARCHITECTURE.md) ·
[Local comparison report](../reports/2026-10-22-cavia-community-research.md)

## Directly useful: Bullet Witch and Zegapain XOR

Rich Whitehouse published a Bullet Witch model import demonstration on **March 29,
2012**, describing its Python implementation. The distributed Noesis source is a
substantial format specification: archives, vertex/index bindings, draw groups,
material texture references, skeletons and skinning palettes.
[Author's demonstration](https://www.richwhitehouse.com/index.php?postid=66)

The [official changelog](https://www.richwhitehouse.com/noesis/nms/index.php?content=readme)
records Bullet Witch import in v3.46, material/loading fixes in v3.47 and
**Zegapain XOR support in the Bullet Witch Python script in v3.72**. Version 3.73
added a procedural animation example; that is not a decoder for game animations.
The distributed reader itself mentions Zegapain-specific chunk nesting.
This establishes shared asset-reader coverage across the two titles, not an
identity claim about their entire engines.

Downloaded the author's [Noesis 4.474 distribution](https://www.richwhitehouse.com/filemirror/noesisv4474.zip),
and preserved these reference files under `vendor/noesis-reference/`:

- [fmt_bulletwitch_cpr.py](../vendor/noesis-reference/plugins/python/fmt_bulletwitch_cpr.py)
- [inc_noesis.py](../vendor/noesis-reference/plugins/python/inc_noesis.py)
- [API readme](../vendor/noesis-reference/plugins/python/__NPReadMe.txt)

See the [download/hash manifest](../evidence/community-research/noesis-source.json).
No Noesis executable or downloaded Python code was executed. The archive is
retained locally; source availability is not a blanket redistribution license.

Important source anchors: `parseModelHeader`, `parseDGRP`, `parseCM1RBoneMap`,
`parseKNS2`, `parseSkeleton`, `CprVDCL.createBuffers`, `cprLoadTexture`.
Console decoding uses big-endian data and Xbox-specific vertex/texture formats.
Our PC comparison demonstrates which metadata layouts survive and identifies
Direct3D 9 declarations in place of the console declaration encoding.

Luigi Auriemma's [Bullet Witch CPR extractor](https://mirror.aluigi.org/bms/bullet_witch_cpr.bms)
was already used in our initial archive research. It remains the archive reference;
the Noesis reader adds interpretation of the model contents.

## Original NieR: concrete, narrower tools

[Banz99/Nier-Texture-Manager](https://github.com/Banz99/Nier-Texture-Manager)
contains C# extraction/repacking logic for original-generation NieR textures.
The implementation handles PS3 byte order and Xbox textures, with LZO support.
It maps paired descriptor/data files (`2DV/2DP`, `MDV/MDP`, `VIR/PHY`, `EFV/EFP`),
32-byte `HEAP` entries, `TX2D` textures and optional `KPKy` containers.
Those are different from Bullet Witch's observed CPR representation.

Preserved README and two source files at commit
`d68dd2c6e1be2f3d48c61f4a9ce3552b63c634a0`:
[Form1.cs](../vendor/nier-texture-manager-reference/Decompressor/Form1.cs),
[README](../vendor/nier-texture-manager-reference/README.md), and
[provenance](../evidence/community-research/nier-source.json).
Source inspected only; no NieR assets were available for a compatibility comparison.
This is useful comparative research, with no demonstrated direct importer reuse.

[Replicant2Blender](https://github.com/WoefulWolf/Replicant2Blender) and
[replicant_templates](https://github.com/WoefulWolf/replicant_templates) explicitly
target **ver.1.224…**, the later remaster. Keep those generation labels attached
when following links; their existence does not establish original-engine coverage.

## Community leads and remaining searches

[2018 Steam modding discussion](https://steamcommunity.com/app/696180/discussions/0/3288067088086193485?ctp=3&l=english)
contains firsthand reports of the old importer working with console assets while
PC files differ. Pages 1–3 were checked; no verified PC-ready importer emerged.
Our local comparison now makes some of those differences explicit.

No complete Bullet Witch engine decompilation or documented full-engine replacement
was located in this search. Existing work is already substantial at the archive,
model and texture layers. The strongest immediate path is the Bullet Witch reader
and its Zegapain coverage, with NieR tools as comparative references.

Searches covered English/Japanese title, Cavia, engine, Noesis, XSI and original
NieR/PS3/Xbox 360 terms; author pages, GitHub code and contemporary developer
interviews were inspected. Search absence is not proof that no additional tools exist.
