# Remaining work

- Next: reconstruct concrete startup services/screens and their task completion,
  global/save side effects and status contracts. Replace deterministic verification
  fixtures with production implementations incrementally; start with company-logo
  and memory-card flow, then advertise/title/menu and required sprite/media/input.
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
