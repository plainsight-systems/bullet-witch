# Engine provenance and external sources

Checked October 22, 2026. Return to [architecture](../engine/ARCHITECTURE.md).

## Current conclusion

The Windows release dates to **April 25, 2018**. Steam credits **Marvelous, Inc.**
as developer and **XSEED Games, Marvelous USA, Inc., and Marvelous Europe** as
publishers. Storefront credits do not identify every person or contractor who
performed the port. [Steam product page](https://store.steampowered.com/app/696180/Bullet_Witch/)

The publisher describes the port as based on the Japanese console build, with
changes to framerate, brightness, enemy behavior/balance, weapons and movement,
and inclusion of the original DLC. These differences matter when comparing PC
behavior with console references.
[Publisher development post, May 1, 2018](https://www.xseedgames.com/2018/05/01/whats-new-with-bullet-witch-on-pc/)

**Bullet Witch uses Cavia's in-house engine, built from scratch for the game.**
This is directly confirmed by the development team's Play interview, now located
and reviewed. The original Japanese release was July 27, 2006. See
[engine history and direct sources](cavia-engine-history.md).

The remaining provenance questions concern a separate historical engine name,
reuse in other titles and the exact changes in the 2018 PC port. In-house engine
development itself is established. The original developer blog also identifies
NovodeX physics and an AI scripting engine.

## Primary source catalog

| Source | What it establishes | Limit |
|---|---|---|
| [Steam product page](https://store.steampowered.com/app/696180/Bullet_Witch/) | PC date, developer/publisher credits and DirectX 9.0c requirement | No engine name in the inspected listing. |
| [Publisher launch announcement, April 25, 2018](https://marvelous-usa.com/2018/04/25/remaster-of-bullet-witch-casts-and-blasts-onto-pcs-today/) | XSEED's worldwide PC launch, graphics options and included DLC | Announcement, not a technical port specification. |
| [Publisher development post, May 1, 2018](https://www.xseedgames.com/2018/05/01/whats-new-with-bullet-witch-on-pc/) | Japanese-build lineage and intentional PC changes | Does not name the engine or describe a renderer rewrite. |
| [CEDEC 2007 session catalog](https://cedil.cesa.or.jp/cedil_sessions/view/105?event=23) | Bullet Witch development session by 高橋徹, Cavia producer, September 28, 2007, 13:00–14:20 | User supplied `r35.pdf`; all six slides reviewed. Page 2 discusses in-house engine motivation. [Local review](cedec-2007/README.md). |
| [Original official site](https://www.marv.jp/products/bulletwitch/) | Original AQ Interactive / Cavia presentation | Legacy Flash content limits inspection. |
| [Official staff blog, June 2006 archive](https://web.archive.org/web/20070528115040/http://www.aqi.co.jp/product/bulletwitch/blog/2006/06/index.html) | Original developer commentary and research leads | Archive access intermittent; no engine identity claim is based on it here. |
| [Official staff blog, July 2006 archive](https://web.archive.org/web/20070528131506/http://www.aqi.co.jp/product/bulletwitch/blog/2006/07/index.html) | Further development commentary | Technical claims need a specific entry and verification. |
| [Official staff blog, August 2006 archive](https://web.archive.org/web/20070528115438/http://www.aqi.co.jp/product/bulletwitch/blog/2006/08/index.html) | Further development commentary | August 4 programmer entry now reviewed: explicitly identifies NovodeX and AI scripting work. See the engine history note. |

The CEDEC title is 「オリジナルタイトルの意味と次世代機開発について
～『バレットウィッチ』開発現場から～」, approximately “The meaning of an
original title and next-generation development—from Bullet Witch's development
floor.” The six slides cover production experience, including in-house engine motivation.
They provide no technical engine architecture. User-supplied access resolved the
earlier download limitation; no authentication boundary was bypassed.

Secondary databases and encyclopedia references helped discover primary links.
Their engine labels are not treated as confirmed without original material or
binary evidence.

## Evidence in the inspected PC executable

Input: `experiments/steamstub/witch.original.exe.unpacked.exe`, SHA-256
`bdaff1746d3092215699b09eab11315c6adf8fb71eaa8f46c280b11b3cd448fd`.
Offsets below are **file offsets**, not preferred virtual addresses.

| Observed material | Offset / evidence | Interpretation |
|---|---|---|
| `%scavia_%s.h` | `0x427998` | Cavia-related identifier; not an engine product name. |
| `witch::ScriptEnvironment::…` strings | From `0x448c28` | Game namespace and AI/script names survive in diagnostics. |
| `H:\bw\dev\project\witch\Release\witch.pdb` | `0x457d40` | Original build path; does not mean the PDB is available. |
| Paths under `src\script`, `src\menu`, `src\ai`, `src\base` | JSON below | Original subsystem/file naming anchors. One raw string has printable bytes before the path. |
| Lua 5.0.2 identification | `0x440f48` | Embedded version identifier. Independent validation uses Lua 5.0.3, a distinct tool version. |
| PhysXLoader and NxCooking imports | Import report below | Middleware dependencies; loaded SDK version and scene behavior still need mapping. |
| Legacy PhysX installer path containing `9.13.0604` | `0x2c0d58` | Redistributable package version, **not sufficient evidence of SDK version**. |
| D3D9/D3DX9, DInput8, XInput and CRT imports | Import report below | Windows platform boundaries; not an overall engine brand. |

See [raw string evidence](../evidence/engine/binary-provenance-strings.json),
[extractor](../tools/engine_provenance.py), and
[import inspection](../reports/2026-10-21-steam-inspection.md).
Keyword hits such as `DebugEngine` or a fire-engine asset name must not be
mistaken for product identification. Absence of a familiar commercial engine
name in this limited scan does not prove absence of that technology.

## Follow-up

CEDiL access checked October 22, 2026: it is the CEDEC Digital Library, and its
[official terms](https://cedil.cesa.or.jp/registrations/agreement) state the
service is free. The [registration form](https://cedil.cesa.or.jp/registrations/regist)
requires email, password and surname/given name; company fields are optional.
Free registration is open to anyone according to
[CEDEC's own description](https://cedec.cesa.or.jp/2024/call_for_speakers/).
Registration/login is needed for the download. The user has now supplied it, and
the [local copy](cedec-2007/r35.pdf) has been inspected.

1. Map original namespace/source-path strings to native references.
2. In-house engine attribution is resolved. Continue with shared technology,
   technical pipeline details and PC port-team attribution.
3. CEDEC slide inspection is complete. Seek more detailed original technical
   material; this six-page outline contains no middleware or architecture details.
4. Identify SDK creation/version calls independently of redistributable labels.
