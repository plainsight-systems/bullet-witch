# Steam performance baseline

The user adopted **stable 60 fps (about 16.67 ms per frame)** as a faithful-port
performance acceptance target, grounded in the original PC release. This is an
acceptance target,
not a claim that the current Mac build has passed sustained performance
acceptance. The assets’ 60 Hz animation sampling does not establish rendering
performance.

| Evidence | What it establishes | What it does not establish |
|---|---|---|
| [Marvelous/XSEED PC release article, May 1, 2018](https://marvelous-usa.com/2018/05/01/whats-new-with-bullet-witch-on-pc/) | The stated aim is consistent 60 fps; selectable limits are 30, 60, 120 and unlimited. | A reproducible benchmark across scenes/hardware or a guarantee at every resolution. |
| [Steam system requirements](https://store.steampowered.com/app/696180/Bullet_Witch/) | Minimum explicitly targets 720p at 30 fps: Core i5-6300HQ 2.30 GHz, Intel HD 530, 4 GB RAM. Recommended lists Core i5-6400 2.70 GHz, GTX 950 and 6 GB RAM. | The recommended configuration has no stated resolution or frame-rate target. Do not label it a documented 1080p60 configuration. |
| [WorthPlaying PC review, October 31, 2018](https://worthplaying.com/article/2018/10/31/reviews/111536-pc-review-bullet-witch/) | Contemporary qualitative observation of solid 60 fps and support for 4K output. | No reproducible hardware, scene or frame-time dataset accompanies those claims. They do not establish a measured 4K60 benchmark. |

Use stable 60 fps as the adopted faithful-port acceptance target while preserving the
[renderer reconstruction policy](../../../docs/architecture/world-renderer-reconstruction.md#baseline-work-placement-and-later-performance-design).
The eventual acceptance run must record output resolution, target Mac/hardware,
scene, build and timing scope; those details are not yet selected. The incomplete
scene does not establish achieved acceptance. This faithful-port target is
separate from advanced architecture/graphics upgrade work. Existing local timing probes remain scoped to their recorded
runs; neither official requirements nor one successful probe proves complete
port performance. This record adds no profiling prerequisite, runtime cap or
new verification gate.
