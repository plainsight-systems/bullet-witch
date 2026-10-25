# Offline texture and material experiment

## Contract and source comparison

The experimental Python CLI owns copies of explicitly selected color textures and
new, isolated output runs. It does not import anything into the game, alter its
resources, start a service, or install a geometry runtime. Each manifest binds a
canonical resource ID to an exact PNG hash and original-resource provenance.
Only explicitly declared `color_srgb` inputs are admitted. This is the model's
working interpretation of encoded RGB bytes, not evidence that the original game
used an sRGB sampler. No gamma conversion is applied. Existing native material,
UV, normal, roughness, mask and packed-data semantics remain separate.

Source study before implementation:

- [ComfyUI `nodes_upscale_model.py`](https://github.com/Comfy-Org/ComfyUI/blob/63be2f9b82ad0dac88a7ceaa28a9cf3154125a18/comfy_extras/nodes_upscale_model.py),
  `ImageUpscaleWithModel.execute`: float32 RGB model invocation, overlapping
  tiles, clamping output, and separate bilinear alpha resampling. The CLI uses
  the installed library's `comfy.utils.tiled_scale` directly, without copying
  its GPL source. It requires that pinned checkout and utility-file hash.
  [`tiled_scale_multidim`](https://github.com/Comfy-Org/ComfyUI/blob/63be2f9b82ad0dac88a7ceaa28a9cf3154125a18/comfy/utils.py)
  feathers overlap into a weighted output accumulator. CPU accumulation bounds
  GPU output storage. The CLI fails on OOM rather than silently changing tile
  settings, making each run's algorithm reproducible.
- [PBRify source](https://github.com/Kim2091/PBRify_Remix/tree/190db5378909749bdbad0f951b5724ba066ea32d),
  `README.md` and `EXTRA_INFO.md`, plus release `v1.7.2`'s `PBR Upscale Chain.chn`
  (SHA256 `91657aff24147ffa65bff7a074bac92b5a62c3084ec137c8a927e100130f842d`).
  The chain splits transparency, upscales RGB, then supplies that upscaled RGB
  to each material predictor. Normal prediction passes through normalization
  and Gaussian blur (sigma 0.8), and its save suffix says `normal_dx`. That label
  suggests DirectX intent but does not prove the raw checkpoint's convention or
  agreement with this game's tangent basis. Height is discouraged because baked
  lighting corrupts inference. The CLI follows the RGB-before-material order,
  keeps height opt-in, and deliberately saves **raw** material predictions before
  normalization/blur/channel conversion. Tangent convention remains unvalidated.
  It preserves alpha by bilinear resampling rather than the chain's learned
  alpha upscaling, to avoid inventing opacity detail.

## Invocation

Use a Python 3.11+ environment with torch, spandrel, safetensors, numpy and Pillow and
an existing pinned ComfyUI checkout. Paths below are environment variables chosen
by the operator, not repository configuration. All generated assets and run data
belong outside tracked source, for example in ignored `research/evidence/`.

Use [the bounded extractor](../tools/prepare_asset_upgrade_inputs.py) to create
hash-bound copies of Alicia's original body and face from the read-only game root:

```sh
python3 research/tools/prepare_asset_upgrade_inputs.py \
  --game-root "$GAME_ROOT" --preset alicia-original --output-dir "$INPUT_ROOT"
```

The input directory must be new and outside the game installation. Then run:

```sh
"$PYTHON" research/tools/asset_upgrade_pipeline.py \
  --manifest "$INPUT_ROOT/manifest.json" --input-root "$INPUT_ROOT" \
  --catalog "$MODEL_CATALOG" --comfy-root "$COMFY_ROOT" \
  --output-dir "$NEW_RUN_DIR" --tile 256 --overlap 32 \
  --materials --material-max-size 1024
```

`--validate-only` checks all input and selected model hashes without importing
torch or running inference. `--height` also requires `--materials`. Material
prediction consumes the PBRify color result, reduced to the declared maximum
size using Lanczos if needed. Recorded material resolution is its actual
resolution, not the full 4x color size. All outputs retain their source aspect
ratio. Tiling declarations are mandatory; wrapping inputs are rejected until
periodic-boundary inference is implemented. Original UV atlases can still have
island seams: this experiment does not know or repair those boundaries.

## Input schema

```json
{
  "schema_version": 1,
  "assets": [{
    "resource_id": "canonical-resource-identifier",
    "source_png": "copies/body.png",
    "source_sha256": "SHA256_OF_PNG_BYTES",
    "semantic": "color_srgb",
    "alpha_mode": "opaque",
    "tiling": {"u": false, "v": false},
    "original_dds": {"path": "copies/body.dds", "sha256": "SHA256_OF_DDS_BYTES"},
    "provenance": {"material_role": "base-color", "color_space_evidence": "unverified"}
  }]
}
```

Paths must be relative to the input root and resolve inside it. `original_dds`
is optional, but if supplied its file and hash are verified. `alpha_mode` is
`opaque` (every source alpha byte must be 255) or `straight`; premultiplied
and ambiguous alpha are rejected. PNGs must be 8-bit RGB or RGBA. Duplicate
resource IDs, missing semantic declarations and hash mismatches fail before
inference. An optional `comparison_crop: [x, y, width, height]` selects an additional
source-pixel crop, shown at actual 4x output resolution. It must lie inside the
source image. Extra provenance is retained verbatim. Inputs are never written.

The catalog is `{"models": {MODEL_KEY: {"path": "...", "sha256": "..."}}}`.
Model paths may be absolute or relative to the catalog. Required keys are
`realesrgan_x4plus` and `pbrify_upscaler_v4`; `--materials` also requires
`pbrify_normal_v3` and `pbrify_roughness_v2`; `--height` adds `pbrify_height`.
The selected model hash must match before loading. PyTorch checkpoints load
with `weights_only=True`; safetensors use their tensor-only loader. Spandrel must
recognize a three-channel image model with the expected scale (4x color, 1x maps).

## Output and review boundary

A run directory must not already exist and must be outside the input root.
It contains `run.json`, recording status even after a caught failure, script and
input/catalog hashes, source resource identities, software versions, CUDA device,
model hashes, settings, timing scope, peak allocated/reserved GPU memory, image
sizes and hashes. Inference uses CUDA float32 with TF32 disabled and one model
resident at a time. Memory numbers include the model and transient work for that
inference; timings exclude model loading and PNG encoding and include tile
prediction and CPU assembly. Deterministic byte equality between environments
is not promised.

Per-asset directories contain the source PNG, a bicubic 4x baseline, both 4x
learned color variants, a reduced overview, and five comparison sheets of 512px
output-space crops. Original pixels in comparisons use nearest-neighbor 4x
magnification; other crop columns show the actual full-resolution output pixels.
Material outputs are uncalibrated inferred RGB data: unmodified float32 arrays
(`.npy`) plus clipped/quantized RGB PNG previews. They receive no gamma transform,
normal renormalization, blur or green-channel flip. They have no generated alpha;
a separate bilinear source-alpha image accompanies materials. Roughness channels
are retained instead of assuming a validated single-channel mapping. Height
values are not distances and must never drive displacement automatically.

Acceptance requires inspection of facial identity, authored costume details,
lettering, compression damage, haloing, UV seams and alpha edges. Compare both
models with bicubic; reject invented detail or excessive smoothing. Generated
normal orientation, roughness response, seam behavior and actual rendered
appearance need a later material/mesh validation experiment before any runtime
adoption. This tooling does not change the accepted faithful-port baseline.

## Tool verification

Before GPU deployment, `py_compile` passed. A manual temporary-file exercise
admitted a valid straight-alpha input and rejected source-hash mismatch,
non-color semantics, an opaque declaration on transparent pixels, wrapping
textures, out-of-range comparison crops, paths outside the input root and
duplicate IDs. Seven comparison sheets were generated and hashed from a small
synthetic RGBA image. These checks establish CLI/data handling only; they do not
establish model quality, throughput or in-game material compatibility. No new
automated integration scenario was added.

## First actual-content experiment

The user authorized offline pipeline experiments on October 25 and selected
Alicia first. The extractor verified the original PW0 body and face BTX0 color
bindings against the resource catalog, runtime texture table and model material
references. PNG bytes and original archive hashes remained unchanged. The
repeatable extractor produced byte-identical copies in a separate directory;
existing destinations and destinations inside the game were rejected.

The first CUDA run completed on an RTX 3090 Ti with 24 GB VRAM, using the pinned
ComfyUI checkout, PyTorch 2.14.1+cu130 and Spandrel 0.4.2. It used float32, TF32
disabled, 256-pixel tiles and 32-pixel overlap. Both color variants ran at native
4x; normal and roughness inference consumed the PBRify output capped at 2048.
Height was not requested.

| Input | Color output | Real-ESRGAN | PBRify DAT | Normal / roughness output |
|---|---|---:|---:|---|
| Body, 1024 x 1024 | 4096 x 4096 | 3.108 s | 18.455 s | 2048 x 2048 |
| Face, 512 x 512 | 2048 x 2048 | 0.798 s | 4.817 s | 2048 x 2048 |

Each material-map inference took 0.96–1.09 seconds. These are single-run timings
for tile transfer, inference and CPU assembly, excluding loading and encoding;
they are not warmed repeated benchmarks. Peak PyTorch allocation was 1.155 GiB
and reservation 1.850 GiB. One-second NVIDIA telemetry sampled a maximum of
2240 MiB device memory, 56 C and 417.56 W.

All 30 actual-content output hashes passed verification, and the inputs remained
unchanged. A separate 73 x 91 RGBA control completed with 292 x 364 color outputs;
all three color variants and the material-alpha output matched direct bilinear
source-alpha resizing exactly. Its 15 output hashes also passed. Runtime engine
code was not changed, the game was not launched, and ComfyUI was not started.

The reviewed eye/eyebrow and lace/skin crops show a useful distinction:
Real-ESRGAN removes much more source grain and smooths skin; PBRify preserves
more surface texture but adds etched-looking detail. The latter's raw normal
preview carries that texture forward, and its roughness prediction follows
some painted shading. These observations do not establish recovered detail or
correct physical materials. Neither variant is accepted as an in-game upgrade.
Next review should compare selected candidates on the preserved mesh under
controlled lighting, including UV seams and gameplay viewing distances, before
batching or integrating replacements.

Generated evidence is under ignored `research/evidence/asset-upgrade-pilot/`:
`inputs/manifest.json`, `run-001/run.json`, the two asset directories, telemetry,
environment inventory and `pipeline-verification.json`. Source and generated
images remain outside Git. The 4x color candidates carry 16 times as many texels
as their originals; compression, mipmaps and runtime residency remain unmeasured.

Both authored Python tools compile. The repository policy checks passed,
including five policy tests and a scan of 701 text-only file versions with no
ignored artifacts admitted. The downloaded local copies of all 30 actual-content
outputs match the remote run hashes. No C++ build or gameplay test was required
or claimed for this Python-only experiment. After verification the GPU returned
to 17 MiB / 0% utilization; ComfyUI remained inactive and disabled.
