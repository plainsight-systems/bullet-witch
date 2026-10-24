# Authored stage fog

The selected `st04_c` environment supplies separate surface and sky fog blocks,
with a shared RGB color. Its raw customization record has zero density and
opacity, but these are not the live values. Original stage activation enables
AREA updates, which select camera-dependent environment channels before fog
publication. The inspected cave target has surface density `0.0003` and height
opacity `0.0004`; the exterior target has `0.0007` and `0.004`. The corresponding
sky terms also change. Fog therefore participates in the live reconstructed
scene; the raw record alone cannot establish its visible contribution.

## Field mapping

Offsets below are relative to the selected `witch/stage/<name>` customization
payload. The immutable `StageEnvironment` decoder retains finite authored values
and enable bytes. View composition owns disabled fallbacks and shader packing.

| Meaning | Surface offset | Sky offset | Raw `st04_c` value |
|---|---:|---:|---|
| Distance enabled | `0x25` | `0x50` | true |
| Distance start | `0x28` | `0x54` | 1000 |
| Distance end | `0x2c` | `0x58` | 0 |
| Exponential density | `0x30` | `0x5c` | 0 |
| Distance height gate enabled | `0x34` | `0x60` | true |
| Gate start / end | `0x38 / 0x3c` | `0x64 / 0x68` | 2499 / 2500 |
| Additional height fog enabled | `0x40` | `0x6c` | true |
| Height start / end | `0x44 / 0x48` | `0x70 / 0x74` | 2499 / 2500 |
| Height opacity | `0x4c` | `0x78` | 0 |

Shared color at `0x7c / 0x80 / 0x84` is black. Byte `0x24` is the separate
area-environment override gate, not the surface distance enable. It is zero in
the raw record, then original initializer `5f9c00` sets it to one. The decoded fixture distinguishes these adjacent fields. Descending
or reversed distance ranges are retained, rather than rejected as if they were
near/far projection planes. No extra allocation is introduced by fog decoding.

The independent local extraction is retained as
`research/evidence/reconstruction/stage/slice4-fog-fields.json`; the decoded
payload offset is 34416 in the retained `customizer.bin`. Game files remain
read-only and neither these bytes nor the extraction is tracked.

## Original composition

`0x460300` reads the fields above and queues mode-1 fog commands through
`0x422280`. Disabled distance changes its start to 2499 and density to zero.
Disabled distance-height gating substitutes 2499–2500. Disabled additional
height fog substitutes the same range and zero opacity. The surface commands
retain world units. The sky command multiplies distance start/end by `0.01`;
its height values are `(camera_y + authored_height) * 0.01`. Density and opacity
are not multiplied by that scale. This concerns fog commands, not sky geometry.

Executor `0x4276d0` uploads color to pixel constant 39 and delegates the numeric
coefficients to `0x425160`. Let `d0,d1` be command distance endpoints, `g0,g1`
the command height-gate endpoints, `h0,h1` the additional height endpoints,
`rho` density, `a` height opacity and `v` the render context's eye height:

```text
PS40 = (1 / (d1-d0), -d0 / (d1-d0),
        -1 / (g1-g0), (g1-v) / (g1-g0))
PS41 = (0, a, -1 / (h1-h0), (h1-v) / (h1-h0))
PS42 = (d0, rho, 0, 0)
```

Context writer `0x45fac0` places the eye at `+0x90/+0x94/+0x98`.
Material uploader `0x42ffc0` subtracts that same eye from world translation;
fog therefore consumes camera-relative positions with world axes, not rotated
view-space height. The translated mode-1 shader combines exponential distance
fog with the descending height gate and the independent height contribution.
Its exponential path uses density, not the first two linear-distance entries.
The live AREA values replace the raw zero density and opacity. The camera-owned
selection and channel transitions are documented in
[scene lighting producers](lighting-scene-producer-contract.md).

Source witnesses are under `research/evidence/engine/pass-44-stage-area-context/`
(`0x460300`, `0x422280`), `pass-06l/` (`0x4276d0`, `0x425160`),
`pass-08-world-environment/` (`0x45fac0`) and `pass-08-world-materials/`
(`0x42ffc0`). The instruction listing confirms constants 2499, 2500, 0.01, 1
and -1 independently of inferred decompiler types.

## Verification boundary

The existing resource test scenario now exercises separate sky/surface fields,
independent enable bytes, reversed distance endpoints, shared RGB and rejection
of a nonfinite fog scalar. Runtime coefficient packing and native visual
verification belong to the integrating slice's checks. The decoder alone does not
claim area interpolation, fog-mode 2, or active nonzero fog visual parity. The
scene environment owner now supplies AREA interpolation separately.

Design and implementation each queried both C++ MCP corpora and read `R.2`
(`#rr-use-ptr`) and `MEM.6`
(`guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md`): checked
borrowed records return owned scalar values, with hierarchy metadata using the
existing injected domain.


## Live AREA activation correction

The raw zero-density observation above is not the live environment. Stage
activation enables AREA, which updates mode-1 fog fields from the spatial
channel target. Actual cave targets have equal surface distance endpoints zero.
Original `425160` consequently writes infinity/NaN into the unused linear
PS40.xy lanes; mode-1 programs read only PS40.zw and PS42.xy. Production now
sets those unused linear lanes to zero rather than rejecting a valid exponential
fog state. Active height-gate, height-opacity and exponential-density equations
are unchanged. This is a typed representation of the admitted mode-1 consumer,
not an epsilon or a claim that linear mode-2 fog is implemented.

### Source execution and admitted consumers

Literal original `425160` execution with the cave and exterior AREA snapshots
confirms `PS40.xy = (+Infinity, NaN)` when both distance endpoints are zero.
Only the D3D constant-upload call was intercepted; the coefficient arithmetic
was executed unchanged. `PS40.zw`, `PS41`, and `PS42` remain finite at those
points. These unused linear coefficients are nonfinite results of the original
arithmetic, not an omitted clamp or an epsilon-based transition.

The current admitted material selectors were matched against the original
registry: 130 variants use 61 distinct color programs. Every inspected `c40`
read uses only `.z/.w`; none consumes the linear `.x/.y` lanes. The native
`world_color.metal` has the same exponential-distance and height consumers.
The adapter therefore publishes canonical zero in its unused linear lanes and
validates the active coefficients. It does not reject a valid exponential fog
state because a linear-distance interval is zero. Future linear-fog admission
requires its own producer/consumer contract.

Ignored local evidence: `research/evidence/reconstruction/character-light/`
`output-contract/fog-area-consumer.json`, `fog_area_consumer.py`, and
`fog-admitted-consumers.json`. This evidence verifies coefficient publication
and admitted shader consumption, not complete native visual parity.
