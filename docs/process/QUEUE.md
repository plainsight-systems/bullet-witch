# Remaining work

- Next: integrate the implemented company-logo/startup-check update policies with
  task completion and activation side effects; provision the exact logo playlist
  and media/fade/input capabilities. Keep unresolved movie-open failure explicit.
  Continue advertise, title initialization, character-load save/dialog behavior
  and title-menu status contracts, following
  `research/engine/subsystems/startup-next-contracts.md`. Replace root fixtures
  only as real capabilities become available. Graphics/media backend selection
  remains open for discussion with the user.
- Extend root/mission native bindings as their owning game systems are implemented;
  missing bindings and unsupported resource/CALL handlers remain explicit failures.
- Close camera/view, scene/visibility, lighting/pass, all-mode schedule and
  auxiliary-worker contracts at their owning boundaries as source reconstruction
  reaches them. Structural ownership is established; behavior remains explicit.
- Reconcile the eleven-scenario integration inventory with the five-scenario target
  through meaningful in-process contract extraction where appropriate. Preserve
  required content-policy and CLI coverage; add no integration scenarios meanwhile.
- Revisit the ASan startup limitation when the host/toolchain changes. Keep
  native/UBSan results separate from unavailable ASan execution.
- Establish platform-specific verification when those backends are implemented;
  do not infer Xbox or Windows support from a passing Mac build.
