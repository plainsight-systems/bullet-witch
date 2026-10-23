# First DLC level selection

One Shot Kill is the smallest first candidate among all six named challenge
missions by Lua instruction count and distinct native API calls. This comparison
supports selecting it for the first level-loading slice; it does not establish
that its geometry, actors, AI or native implementation cost is smallest.

## Challenge comparison

Counts come from the [script inventory](../../evidence/script-corpus/inventory.json)
and [per-script maps](../../evidence/script-corpus/INDEX.md), checked against the
[verified reconstructed Lua](../../extracted/scripts-recovered/). Titles are from
the [content inventory](../../reports/2026-10-21-content-inventory.md).
Prototypes include the top-level chunk: One Shot Kill has seven named functions
plus that chunk. Native APIs are distinct underscored functions called by each
mission script, not a count of all native dependencies.

| Mission | Script | Prototypes | Instructions | Native APIs | Stage resources loaded |
| --- | --- | ---: | ---: | ---: | --- |
| One Shot Kill | `cm01_00` | 8 | 278 | 43 | `st04_c` |
| Dynamite Beauty | `cm04_00` | 16 | 1,448 | 59 | `st01` |
| 7 Minutes | `cm03_00` | 15 | 1,580 | 77 | `st03_a`, `st03_b`, `st03_cd` |
| Caterpillar City | `cm02_00` | 20 | 1,738 | 59 | `st02` |
| Geist Search and Destroy | `cm00_00` | 72 | 1,921 | 48 | `st05_a` |
| Endless | `cm05_00` | 86 | 5,358 | 69 | `st02` |

All six use ordinary stage, environment, path and object loading. Their additional
script demands distinguish the candidates:

- **One Shot Kill:** player placement, checkpoints, three trap registrations, one
  timer registration for a music change, goal and mission-clear handling. No
  scripted camera choreography, shield setup, explicit enemy spawn calls or enemy
  callback registrations. Actors and AI can still arrive through placement data;
  this is not an enemy-free level.
- **Geist Search and Destroy:** a broad trap/enemy callback network and kill-count,
  mission-flag and timer bookkeeping. Its narrower API surface makes it a plausible
  later candidate despite many repeated functions.
- **Dynamite Beauty:** enemy spawning/death callbacks, mission flags, scripted
  cameras and zone shields.
- **Caterpillar City:** enemy spawning/death callbacks, AI switching, scripted
  cameras, zone shields and checkpoint progression.
- **7 Minutes:** three stage sections, explicit unloading/disposal/reloading,
  timer/game-over behavior, cameras, shields and effect-work allocation. Its small
  function count understates the breadth of required systems.
- **Endless:** extensive repeated enemy/wave/timer callback logic, character
  targeting, cameras and shields.

None of these six scripts calls movie APIs. Absence of movies therefore does not
distinguish One Shot Kill from the other challenges.

The [container inventory](../../evidence/script-corpus/containers.json) records
7 immediate package members for One Shot Kill, versus 10 for Geist, 10 for
Caterpillar, 27 for 7 Minutes, 11 for Dynamite and 11 for Endless. These counts do
not include the transitive stage, geometry, texture, actor or AI dependencies.

## Other DLC variants

The twelve campaign variants are also larger by script instruction count. The
same inventory records 73–155 prototypes and 2,742–5,485 instructions across
`cm16_00`–`cm21_00` and `cm32_00`–`cm37_00`. The smallest by instructions is
`cm36_00`, with 79 prototypes and 2,742 instructions. Its inventory records 86
underscored native globals; all twelve variants reference `_PlayMovie` and
`_WaitMovie`. The content inventory identifies these as Extra Edition and Great
spells unlocked variants of the regular campaign stages.

## Decision and boundary

Use One Shot Kill as the first level-loading candidate. Its script is over five
times smaller than the next challenge by instruction count and uses the fewest
distinct native APIs. Follow its actual load sequence and placement resources to
establish the required engine work. Do not equate this script comparison with a
measured ranking of world complexity or full gameplay reconstruction effort.
