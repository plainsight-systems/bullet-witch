# Cavia engine history — direct developer sources

Researched October 22, 2026. [Architecture](../engine/ARCHITECTURE.md) ·
[Provenance catalog](engine-provenance.md)

## Finding

**Bullet Witch uses Cavia's in-house engine, developed from scratch for the game.**
The direct source is Dave Halverson's interview with the Bullet Witch development
team, dated April 28, 2008 on the archived Play page. In the first answer:

> we developed the engine totally from scratch.

[Play: Team Cavia interview](https://web.archive.org/web/20121214062634/http://mag.gomorning.com/index.php?contentid=505&fuseaction=SiteMain.Content)

The team describes large uninterrupted areas and quick retries after death as
important goals. It also confirms that the Ghost in the Shell: Stand Alone Complex
team worked on Bullet Witch. That establishes team continuity; it does not identify
a shared engine. The interview supplies no separate engine product name.

This resolves the earlier overcautious classification of in-house development
as merely an inference. Use **Cavia's Bullet Witch engine** as a descriptive name.

## Original release anchor

The official Tokyo Game Show 2006 exhibitor list records Bullet Witch under
AQ Interactive, Xbox 360, with the date **July 27, 2006** (page 1).
[Official TGS 2006 list](https://expo.nikkeibp.co.jp/tgs/2006/blog/0802a.pdf)

The PC release is April 25, 2018, and the publisher identifies the Japanese build
as the port's starting point; see the existing provenance catalog. Search original
engine material around 2004–2007, and PC-specific changes around 2017–2018.

## Physics and AI — original developer blog

In the official blog's **August 4, 2006** entry, programmer **安藤 (Ando)** identifies
his responsibility as the AI scripting engine and explicitly states that Bullet
Witch uses **NovodeX** for physics. He describes choosing performance over accuracy
to support many moving objects, and says helicopters are physically simulated.
[Official developer blog, August 2006](https://web.archive.org/web/20070528115438/http://www.aqi.co.jp/product/bulletwitch/blog/2006/08/index.html)

This gives original console-era provenance for physics middleware and the AI
scripting subsystem. The inspected PC executable independently imports PhysX
components and contains recovered AI/Lua interfaces. SDK version and port changes
remain mapping tasks.

The same archive's **August 9, 2006** post by cinematic staff member 安井章 points
to a CG World issue released at the end of the preceding month containing a
Bullet Witch graphics introduction/explanation. This is a specific lead for a
more detailed art/graphics pipeline source; the issue itself has not been read.

## Other original-developer leads

- [CEDEC 2007 slide review](cedec-2007/README.md): the now-inspected deck supports
  in-house development, but its outline is less explicit than the Play interview.
- [Toylogic founder interview](https://www.toylogic.co.jp/recruit/interview/president/):
  岳洋一 describes directing Bullet Witch while at Cavia. This identifies a
  useful developer-history lead. Later Toylogic technology must not be assigned
  to Bullet Witch merely because of shared personnel.
- [Original CEDEC session page](https://cedec.cesa.or.jp/2007/contents/r35.html):
  producer/team history and the original session description.

## Consequences for mapping

- Engine ownership/origin is settled; remove it as an open feasibility question.
- Keep the resource job and retry/reload paths prominent: they relate directly to
  the developer's stated design priorities.
- Map AI scripting and physics as distinct subsystems of Cavia's engine, using
  the original blog alongside recovered code.
- Track a historical engine name, reuse in other titles and exact PC changes only
  as additional provenance, not prerequisites for the architecture map.

## Research method

Searched Japanese and English title/developer/engine terms, followed encyclopedia
references to original interviews and official developer posts, and read the
relevant first-person passages. The Japanese release date is supported by the
contemporary official event list. Stored summaries and a short quotation, not
full copies of archived articles. No new files were written to the game directory.

## Follow-up sources recovered

The [Softimage customer interview](softimage-bullet-witch.md) now provides direct
production-pipeline detail from Cavia graphics staff. The
[community research catalog](community-reverse-engineering.md) identifies existing
Bullet Witch model decoding and Zegapain shared importer coverage. See the
[PC comparison report](../reports/2026-10-22-cavia-community-research.md) for the
concrete results of applying that knowledge to the Steam files.
