# Remaining work

- Next: compatible Lua bridge, task progression and root startup driver toward
  SRC-02, using the implemented resource lifecycle and DPK access.
- Close camera/view, scene/visibility, lighting/pass, all-mode schedule and
  auxiliary-worker contracts at their owning boundaries as source reconstruction
  reaches them. Structural ownership is established; behavior remains explicit.
- Reconcile the ten-scenario integration inventory with the five-scenario target
  through meaningful in-process contract extraction where appropriate. Preserve
  required content-policy and CLI coverage; add no integration scenarios meanwhile.
- Revisit the ASan startup limitation when the host/toolchain changes. Keep
  native/UBSan results separate from unavailable ASan execution.
- Establish platform-specific verification when those backends are implemented;
  do not infer Xbox or Windows support from a passing Mac build.
