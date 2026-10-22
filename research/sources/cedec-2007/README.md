# Cavia's Bullet Witch presentation — CEDEC 2007

Reviewed October 22, 2026. [Source PDF](r35.pdf) ·
[CEDiL catalog](https://cedil.cesa.or.jp/cedil_sessions/view/105?event=23) ·
[Engine provenance](../engine-provenance.md)

## Provenance and inspection

User supplied `r35.pdf` in Downloads after registering with CEDiL. Copied without
modification into this source folder. The six-page Japanese slide deck identifies
Cavia, its Second Production Team, producer 高橋 徹, and September 28, 2007 on
page 1. The title concerns original titles and next-generation development,
from the Bullet Witch development floor.

The source hash, byte count, commands and tool diagnostics are in the
[inspection manifest](../../evidence/cedec-2007/inspection.json).
[Inspection script](../../tools/inspect_cedec_2007.py) runs Poppler `pdfinfo`,
`pdftotext -layout`, and `pdftoppm -scale-to 1400 -png`.
All six rendered pages were visually inspected against the extracted text.
Poppler emitted a default-configuration/dictionary warning, but all three commands
returned success, all six pages rendered, and the relevant Japanese text is legible.
The original PDF was not repaired or rewritten.

## Engine finding — page 2

Under 「オリジナルタイトルの意義とは？」 (approximately “What is the significance
of an original title?”), one bullet reads:

> 自社開発エンジンへのモチベーション

Working translation: **“Motivation for an in-house-developed engine.”**

This is primary-source discussion of Cavia's in-house engine. The subsequent
[Play interview finding](../cavia-engine-history.md) explicitly confirms that the
team built it from scratch. The engine's origin is established; these particular
slides supply no branded name or technical architecture.

Visual witness: [page 2](../../evidence/cedec-2007/page-2.png).

## Page guide — paraphrased

| PDF page | Subject | Research relevance |
|---|---|---|
| 1 | Title, Cavia team, producer and date | Confirms original developer provenance. |
| 2 | Original titles, hardware-launch opportunity, costs, risk, in-house engine motivation and staffing | Direct engine-development lead, without technical specifications. |
| 3 | HD graphics appeal, SD-to-HD transition and production burden | Production context; no renderer architecture or API description. |
| 4 | Mid-sized studio strengths, audience proximity, flagship titles and agile development | Studio context. |
| 5 | Free DLC, longer content life and new opportunities | Original DLC strategy; no asset/package manifest. |
| 6 | Core audiences and production lessons around ambition, schedules and milestones | Project context; no engine implementation details. |

The six pages contain no engine architecture diagram, technical subsystem list,
Lua/PhysX version information or 2018 port details. Do not infer those from the
presentation. Our existing native/asset evidence remains the basis for mapping.

## Mapping consequence

In-house engine development is established by this deck together with the direct
team interview. The presentation-access item is resolved. Continue native mapping
and technical history research from this foundation.
