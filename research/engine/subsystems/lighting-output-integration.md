# Lighting and final output ownership

**Slice 2 accepted after native review · October 26, 2026.** The preceding
mission/presentation slice is accepted and committed. This work preserves its
appearance, shader equations, clocks and serial GPU completion.

## Design and exact change manifest

`scene::LightingPublication` owns prepared scene lighting and reflection binding
interpretation. Its receiver publications own prepared draw destinations and
the existing strict-placement cache. Game presentation supplies authored field
selection, model bounds/transforms and AREA/channel values; engine lighting
samples, packs and publishes. A restricted `render::LightingPublisher` permits
lighting updates only, borrowing the stable `RenderScene`. The native renderer
retains the only authoritative draw registry. Receiver metadata is not another
render registry, and actors/controllers gain no scene or light mutation authority.

Each receiver prepares its initial packet and reserves draw updates while loading,
then seals that membership. Frame publication reuses that storage. Appearance
replacement prepares a candidate receiver and draws, performs the existing native
transaction, then adopts the candidate without allocation or throwing. Receiver
publications retire before their lighting owner; that owner and reflection
resources outlive every sample. RenderScene retires before residency release.

Lighting files: new `include/bw/scene/lighting/lighting_publication.hpp` and
`src/scene/lighting/lighting_publication.cpp`; changes to
`include/bw/render/render_scene.hpp`, `src/render/render_scene.cpp`, and
`src/game/stage_presentation.*`; focused coverage in existing resource tests.
Build lists and current architecture/process documentation follow this manifest.
`tests/render_scene_test.cpp` extends the existing resource-test executable;
`tests/macos_output_compositor_test.mm` extends the existing native memory-test
executable, hooked from `macos_texture_allocation_test.mm`. The portable
`platform/startup_host.hpp` contract describes the extracted owners without an
API signature change.

Output files: new portable `include/bw/render/output_compositor.hpp`, native
`movie_renderer.*`, `sprite_renderer.*`, `output_compositor.*`, and changes to
`src/platform/macos/startup_host.mm`. Existing world rendering and postprocessing
remain intact. Movie and sprite owners retain their own resources; the compositor
borrows the sibling producers and owns command submission, final presentation and
synchronous completion. Host retains the window, input, audio and clock observation.
Existing native offscreen tests will cover composition/retirement; no visible
launch or new integration scenario is part of this implementation.

## Source and reference contract

Original recovered `5e4430` transforms the model receiver center, samples the
character field, selects receiver-local lights and publishes before rendering.
`5f6e80` uses the model's unscaled authored radius. Static and sky policies remain
the separate callbacks described in [scene producers](lighting-scene-producer-contract.md).
The concrete recovered exports were reread before designing this boundary. No
new normalization, sampling cadence or shared body/weapon packet is introduced.

Pinned Unreal `f45729c0b6f097038e72b52d366a62baeca3275b` was inspected again:
`FScene::AddPrecomputedLightVolume`/`RemovePrecomputedLightVolume` explicitly
manage scene membership; `FIndirectLightingCache::UpdateCachePrimitive` keeps
receiver eligibility/bounds/cache separate from game logic; `FinalizeCacheUpdates`
completes preparation before publication. This supports receiver-owned prepared
state and publication authority, not importing Unreal's threads, interpolation
or shared attachment lighting policy. The existing original algorithms remain
in `SceneLighting`/`RetainedLightingReceiver` unchanged.

Pinned Doom 3 `a9c49da5afb18201d31e3f0a429a037e56ce2b9a`,
`neo/framework/Session.cpp`, `idSessionLocal::Draw` and `UpdateScreen`, separates
scene/UI/overlays and brackets drawing with presentation. The native extraction
preserves this project's actual world/postprocess → movie → ordered UI/fade
sequence instead of adopting Doom's policy or globals.

Both required C++ corpora were queried for lighting header design; full Core F.7
(`#rf-smart`) and C.32 (`#rc-dtor-ptr`), and performance COPY.4
(`guidelines/copy-move/COPY.4-rule-of-five-rule-of-zero.md`) and CDSA.32
(`guidelines/cpu-dsa/CDSA.32-transform-static-data-into-the-consumer-layout.md`)
were read. Borrowed capabilities stay explicit; prepared receiver storage is
reused without hot-path allocation; stable owners cannot copy/move and candidate
receiver moves cannot throw. No performance improvement is claimed. Implementation separately queried both
corpora and read full Core E.6 (`#re-raii`) and performance MEM.9
(`guidelines/memory/MEM.9-embedded-allocate-at-init.md`): scoped candidate cleanup,
explicit valid optional state, and reserved storage before frame publication.
The output owner's separate consultation is in [its record](output-composition.md).

## Validation

The native build and all six CTest groups pass, including existing material,
lighting and lifetime contracts. New in-process receiver tests check sealed
membership, initial packet reuse, independent model/placed observations,
transform-driven light admission, invalid observation retention, restricted
capabilities, reentry/stale publication, prepared storage and teardown. They
observe a deterministic backend and do not establish rendered appearance.

Actual offscreen Metal checks cover world/movie/UI composition, ordering,
aspect-fit, fade, resource release, replacement failure and synchronous retirement;
see [output composition](output-composition.md). No visible window was launched.

The purchased-data `startup-corpus` passed its original root/menu flow, One Shot
Kill asset admission, all four weapon/action families, pause/costume/reentry and
zero live migrated payload after teardown. Its renderer/audio observations are
fixtures; original image/audio equivalence requires the user walkthrough.

Final `python3 scripts/check.py all` passes: native build, all six CTest groups,
242 fatal clang-tidy compilation-command checks, pinned formatting, five repository
policy tests and the text/content guard. No check suppression was added. Changed
Markdown local links and whitespace also pass. Logs are ignored under
`research/evidence/reconstruction/lighting-output-integration/`.

The user confirmed the native build works and accepted this slice on October 26.
This records overall walkthrough acceptance, not separate confirmation of every
suggested input sequence or original-game visual parity. Existing coarse
shadows/residual speckling remain accepted baseline limitations. The costume-menu
pink block and the newly reported A/B Accept/Back prompts across main-menu
screens are separate backlog items; neither is corrected here. The prompts’
first appearance, cause and source fidelity remain unverified. Acceptance
authorizes the slice commit; no push is authorized.
