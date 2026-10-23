# Authored stage fog

The selected `st04_c` environment supplies separate surface and sky fog blocks,
with a shared RGB color. Both blocks have zero distance density and zero height
opacity. Their enable bytes are set, but the authored contributions are still
zero. The current clear appearance does not establish a missing visible fog
effect, and adding atmospheric haze would change this stage's settings.

## Field mapping

Offsets below are relative to the selected `witch/stage/<name>` customization
payload. The immutable `StageEnvironment` decoder retains finite authored values
and enable bytes. View composition owns disabled fallbacks and shader packing.

| Meaning | Surface offset | Sky offset | Inspected `st04_c` value |
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
area-environment override gate, not the surface distance enable; it is false in
this stage. The decoded fixture distinguishes these adjacent fields. Descending
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
With the inspected zero density and opacity, both contributions remain zero.

Source witnesses are under `research/evidence/engine/pass-44-stage-area-context/`
(`0x460300`, `0x422280`), `pass-06l/` (`0x4276d0`, `0x425160`),
`pass-08-world-environment/` (`0x45fac0`) and `pass-08-world-materials/`
(`0x42ffc0`). The instruction listing confirms constants 2499, 2500, 0.01, 1
and -1 independently of inferred decompiler types.

## Verification boundary

The existing resource test scenario now exercises separate sky/surface fields,
independent enable bytes, reversed distance endpoints, shared RGB and rejection
of a nonfinite fog scalar. Runtime coefficient packing and native visual
verification belong to the integrating slice's checks. This decoder does not
claim area interpolation, fog-mode 2, or active nonzero fog visual parity.

Design and implementation each queried both C++ MCP corpora and read `R.2`
(`#rr-use-ptr`) and `MEM.6`
(`guidelines/memory/MEM.6-inject-allocators-behind-one-interface.md`): checked
borrowed records return owned scalar values, with hierarchy metadata using the
existing injected domain.
