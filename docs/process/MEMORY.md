# Current decisions

- This normal branch descends from the initial-combat commit. Its working tree
  deliberately removes the prior implementation and begins with new architecture
  documents and preserved legal prose. No implementation, tooling, executable,
  or accepted runtime is present; shared ancestry permits a normal later merge.
- No existing Bullet Witch project code may be reused, called, wrapped, linked,
  or imported, including utilities, decoders, tests, shaders, and generated caches.
- Original purchased data/Lua may be consumed by newly implemented input paths.
- Unreal source at `f45729c0b6f097038e72b52d366a62baeca3275b` is the design
  reference. Architecture documents distinguish actual Unreal behavior from
  proposed adaptations. No Unreal source is bundled.
- The first proposed build slice is monotonic time/waiting, typed frame time,
  and a paced EngineLoop with a configurable 60 fps default. Pacing does not
  replace elapsed game or animation time with a constant 1/60 second.
- Custom memory and math are independent foundation work, not prerequisites
  for the initial clock/pacing slice. New third-party choices are explicit.
- This step authorizes worktree and documentation setup only. Production
  implementation has not started. No commit or push has been authorized here.
