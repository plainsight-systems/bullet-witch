# Future asset upgrade toolchain: verified research

Verified October 24, 2026 against primary documentation, repositories and model
cards. The user approved the future upgrade direction on October 24, 2026; the
[brief's asset upgrade phase](../../BRIEF.md#future-asset-upgrade-phase) records
that agreement after the faithful working port. This report preserves supporting
evidence and experiment limits. Exact tool/checkpoint selection and successful
asset processing remain unestablished; approval of the direction is not a claim
that an experiment has run or a replacement is ready.

The user's preferred execution target is the existing Linux machine with an
NVIDIA RTX 3090 and 24 GB VRAM. Prefer open tools and local batch processing;
consider Windows only for a selected tool that requires it, and rented GPU
capacity only after a measured local limitation. Characters should retain their
identity and working rig; freer generation is a later prop experiment.

## Evidence boundary

Published requirements below are vendor/author claims. Pinned source observations
establish behavior in the inspected implementation, not successful execution on
this machine. No models were installed or downloaded, no inference or performance
benchmark was run, and no assets were modified. No Bullet Witch asset round trip,
visual improvement, VRAM fit, throughput, or production-quality ranking is claimed.
Moving `main`/`latest` links were checked on the date above; pin revisions and
checkpoint hashes before an experiment.

## Texture upscaling and surface-map generation

### Real-ESRGAN and OpenModelDB

[Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN) provides Python directory
inference, tiling and Linux NCNN/Vulkan binaries for NVIDIA/AMD/Intel GPUs. The
portable binaries do not require a Python/CUDA environment. The Python `outscale`
option can resize beyond the network's native scale, but that final resize is not
another learned reconstruction. Its [code license is BSD-3-Clause](https://github.com/xinntao/Real-ESRGAN/blob/master/LICENSE).

Native 4x scaling produces 256→1024 and 512→2048. A 256→2048/4096 or 512→4096
target needs a different scale, another pass, or ordinary resampling; it is not
automatically native 4x inference. Compare results at the intended display size.

[OpenModelDB licenses](https://openmodeldb.info/docs/licenses) vary by checkpoint;
the catalog is not a single model family or blanket open-source grant. The
[GameAI 1.0 record](https://openmodeldb.info/models/4x-GameAI-1-0), for example,
describes a 4x ESRGAN model aimed at PS2 compression, lists WTFPL, and names
commercial-game textures as training sources. That is relevant candidate evidence,
not a Bullet Witch quality result or independently established training rights.
Record architecture, actual checkpoint license, author-declared provenance and
hash separately from inference-code licensing. A model from the catalog is not
necessarily compatible with the stock Real-ESRGAN CLI.

### Standalone PBRify on Linux

[PBRify_Remix](https://github.com/Kim2091/PBRify_Remix) documents a chaiNNer chain
that reads and writes texture directories, upscales color, and generates normal,
roughness and optional height maps. Remix ingestion occurs after those files are
saved; the texture-processing path therefore does not require Remix Runtime or
Toolkit. The author declares the models CC0 and training exclusively on ambientCG
CC0 material. These are author statements, not an independent dataset audit.
[Technical details](https://github.com/Kim2091/PBRify_Remix/blob/main/EXTRA_INFO.md)
identify DAT2 for upscaling and SPAN for map generation; this is not simply a
diffusion workflow.

[chaiNNer](https://github.com/chaiNNer-org/chaiNNer) is GPL-3.0 software supporting
Linux, integrated Python/dependency management, directory batches, and NVIDIA
PyTorch/CUDA. Its [CLI](https://github.com/chaiNNer-org/chaiNNer/wiki/05--CLI)
can execute a saved chain with input overrides, but is explicitly experimental.
It needs a pinned-version reliability check before unattended production use.
The documented standalone chain is the smallest initial PBRify route; no Toolkit
project or runtime capture should be added just to generate normal/roughness files.

[ComfyUI](https://github.com/Comfy-Org/ComfyUI#manual-install-windows-linux) is
GPL-3.0 software with a documented Linux manual install and CUDA PyTorch NVIDIA
backend. It is an alternative local workflow/API host. Its framework license does
not license every custom node or model. Use compatible pinned dependencies rather
than assuming an old model package supports the newest Python/PyTorch stack.

### RTX Remix: optional integration, version-specific behavior

The [current AI Tools](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/howto/learning-aitools.html)
integrate ComfyUI workflows, specifying an NVIDIA GPU with at least 8 GB VRAM and
12 GB or more recommended. NVIDIA's [node pack](https://github.com/NVIDIAGameWorks/ComfyUI-RTX-Remix)
contains PBRify and PBR Fusion 4 templates. The integration workflows perform AI
processing in ComfyUI; REST workflows additionally manage Toolkit projects/layers
and require Toolkit services. Its [Python dependencies](https://raw.githubusercontent.com/NVIDIAGameWorks/ComfyUI-RTX-Remix/main/requirements.txt)
do not list Omniverse Kit. Use plain file inputs/outputs for an independent graph;
do not assume an unchanged project-driven template works without adaptation.

[PBR Fusion 4's model card](https://huggingface.co/NightRaven109/PBRFusion4) declares
Apache-2.0 and at least 8 GB VRAM and advertises normal/depth generation. The exact
outputs and dependencies of its complete template should be checked separately.

The [Toolkit requirements](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs/introduction/intro-requirements.html)
list Windows 10/any RTX GPU minimum; Windows 11, RTX 4070/12 GB and 32 GB system
RAM are recommended. These do not make Windows necessary for standalone PBRify,
Real-ESRGAN or ComfyUI. Do not use Toolkit requirements as a universal minimum for
every external workflow.

The [legacy 1.2.4 AI tool](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/1.2.4/docs/howto/learning-aitexturetools.html)
explicitly generated albedo, normal and roughness maps: quality mode used 256-pixel
tiles for 4x output; speed mode resized input to 256 and produced 1K output. Its
recommendation to keep inputs at or below 512 to avoid VRAM overflow belongs to
that version, not every current model.

[Headless Toolkit documentation](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/docs_dev/agentic-remix/headless-remix.html)
now describes a built-checkout launcher serving MCP/REST, including project,
layer, texture-override and ingestion operations. It is inaccurate to describe
Remix as GUI-only. This does not establish a tested headless endpoint for every AI
workflow. [Runtime capture/replacement and offline authoring are separate](https://docs.omniverse.nvidia.com/kit/docs/rtx_remix/latest/).
Runtime game compatibility is irrelevant to processing an already extracted image.

[Toolkit source](https://github.com/NVIDIAGameWorks/toolkit-remix/blob/main/LICENSE)
and the NVIDIA node pack use Apache-2.0; installed Omniverse dependencies and
downloaded models retain separate terms. [NVIDIA's Omniverse overview](https://www.nvidia.com/en-us/omniverse/)
links its governing software/product terms. Do not describe the entire stack as
uniformly MIT/Apache or assume its license covers the original game's assets.

## Geometry generation and existing-mesh texturing

### TRELLIS.2

The [official repository](https://github.com/microsoft/TRELLIS.2) describes image-to-3D
and shape-conditioned texturing; its [license](https://github.com/microsoft/TRELLIS.2/blob/main/LICENSE)
and [Microsoft model card](https://huggingface.co/microsoft/TRELLIS.2-4B) identify MIT
code and model weights. Dependencies have their own terms: the implementation uses
NVIDIA rendering libraries, and pipeline configuration references DINOv3 and
BRIA RMBG-2.0. The top-level MIT label does not cover all downloaded components.

Published requirements are Linux and an NVIDIA GPU with at least 24 GB VRAM;
validated hardware and published timings concern A100/H100, not an RTX 3090.
The 512, 1024 and 1536 settings describe sparse voxel-grid resolution (1536 cubed),
not texture pixels or polygon counts. Surface attributes include base color,
roughness, metallic and opacity. Opacity capability does not guarantee correct
transparency in a default opaque GLB export.

At inspected commit `75fbf0183001ed9876c8dbb35de6b68552ee08bd`, the
[image pipeline](https://github.com/microsoft/TRELLIS.2/blob/75fbf0183001ed9876c8dbb35de6b68552ee08bd/trellis2/pipelines/trellis2_image_to_3d.py)
and [texturing pipeline](https://github.com/microsoft/TRELLIS.2/blob/75fbf0183001ed9876c8dbb35de6b68552ee08bd/trellis2/pipelines/trellis2_texturing.py)
default to low-VRAM operation and move stages between CUDA and CPU. Cascade token
limits can reduce requested resolution. Neither behavior guarantees every 1536
job fits 24 GB. Proposed local evaluation: 512 smoke test, measure 1024, and attempt
1536 only if the asset and memory budget justify it.

The texturing pipeline's `preprocess_mesh` normalizes geometry and rebuilds a
fresh `Trimesh` from vertices/faces, dropping the original visual/UV object before
`run` reaches postprocessing. Its later UV-preservation branch therefore does not
make the stock end-to-end route an original-UV round trip; fallback unwrapping can
also change vertex correspondence. Output is a new static mesh, not a demonstrated
skinned-character round trip. See [preprocessing](https://github.com/microsoft/TRELLIS.2/blob/75fbf0183001ed9876c8dbb35de6b68552ee08bd/trellis2/pipelines/trellis2_texturing.py#L106)
and [run](https://github.com/microsoft/TRELLIS.2/blob/75fbf0183001ed9876c8dbb35de6b68552ee08bd/trellis2/pipelines/trellis2_texturing.py#L397).
A custom adapter could preserve UVs and transform correspondence; preservation is
not fundamentally impossible, but is not established by the stock pipeline.

### Hunyuan3D-2.1

[Hunyuan3D-2.1](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1) publishes shape and
PBR painting code/weights under the custom [Tencent Hunyuan 3D 2.1 Community License](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/main/LICENSE),
not MIT/Apache. Inspect that actual license and component notices when selecting
an exact package. No comparative evidence here establishes it as having the
"best textures."

Published VRAM figures are 10 GB for shape, 21 GB for painting, and 29 GB combined.
Separate fresh processes are a plausible proposed approach on the 24 GB machine;
keeping both stages resident exceeds that published combined budget. This is not
a fit guarantee for every resolution/view count. At commit
`82920d643c0dc2f7bfd7255f45f62d386edfe60c`, the
[Gradio low-VRAM path](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/82920d643c0dc2f7bfd7255f45f62d386edfe60c/gradio_app.py)
uses cache clearing while the texture-worker model CPU-offload call is commented
out. A flag name alone is not evidence that model weights are offloaded.

The inspected [painting pipeline](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/82920d643c0dc2f7bfd7255f45f62d386edfe60c/hy3dpaint/textureGenPipeline.py#L93)
defaults to remeshing. Even with remeshing disabled, it calls
`mesh_uv_wrap` (xatlas) at [line 119](https://github.com/Tencent-Hunyuan/Hunyuan3D-2.1/blob/82920d643c0dc2f7bfd7255f45f62d386edfe60c/hy3dpaint/textureGenPipeline.py#L119).
Do not promise original UVs, vertex order, rig, weights or morphs survive. Its
painting output uses albedo and metallic/roughness; normal images used for
conditioning are not evidence of an exported generated normal-texture channel.

### Single-image versus multiview

The stock TRELLIS.2 and Hunyuan3D-2.1 routes inspected are single-reference-image
paths. Rendering front/side/back views is useful for reference and evaluation, but
does not turn them into calibrated multiview reconstruction APIs. Tencent's
[Hunyuan3D-2mv](https://huggingface.co/tencent/Hunyuan3D-2mv) is a separate checkpoint
with explicit multiple-view inputs; do not transfer that feature to 2.1 by name.
Generated unseen surfaces remain inferred. For a faithful character, a generated
detail donor baked onto the preserved master is a proposed safer starting point
than accepting an entirely regenerated mesh and rig.

## Baking, retopology and interchange

[Blender Cycles baking](https://docs.blender.org/manual/en/5.0/render/cycles/baking.html)
supports high-to-low selected-to-active normal/AO baking, UV targets and projection
cages/ray distances. Its explicit displacement bake compares Multiresolution
levels; do not promise a generic one-click arbitrary-mesh displacement bake.
[Background Python execution](https://developer.blender.org/docs/handbook/testing/python/)
supports scripted processing. [Blender licensing](https://www.blender.org/about/license/)
distinguishes GPL application code from generated artwork; official binary
distribution is GPL-3.0-or-later and much Blender-developed source is GPL-2.0-or-later.

[Marmoset baking](https://marmoset.co/posts/toolbag-baking-tutorial/) supports
normal, AO and height/displacement outputs and has a [Python API](https://www.marmoset.co/python/reference5.html).
It is an optional commercial tool, not required for the open-tool baseline. No
purchase, trial installation, OS compatibility test, or ranking against Blender
has been performed here.

[Instant Meshes](https://github.com/wjakob/instant-meshes) is a local field-aligned
remeshing tool with Linux support and a [BSD-style license](https://github.com/wjakob/instant-meshes/blob/master/LICENSE.txt).
It can assist topology generation; it does not establish correct facial/joint
edge flow, preserved UVs, skin weights or morph targets. Subdivision adds geometry
but does not by itself recover detail or produce a shippable animated asset.
[Quad Remesher](https://exoside.com/) is a commercial optional alternative.
[Tripo's ComfyUI integration](https://github.com/Tripo-Developer/ComfyUI-Tripo)
and [Meshy's Remesh API](https://docs.meshy.ai/en/api/remesh) provide hosted-service
routes; an available client/node does not make the inference service local or
open source. No paid service run is part of this research.

glTF/FBX are interchange boundaries, not proof of a usable Bullet Witch replacement.
glTF's [material schema](https://github.com/KhronosGroup/glTF/blob/main/specification/2.0/schema/material.schema.json)
requires linear tangent normals, with +X right, +Y up and +Z toward the viewer;
occlusion samples the red channel. The [Blender exporter guidance](https://docs.blender.org/manual/id/3.6/addons/import_export/scene_gltf2.html)
explains non-color normal textures, tangent-space baking and channel packing.
Validate skeletal transforms, skin weights, morphs, tangents and animations at the
actual chosen exporter/importer boundary.

## Agreed future workflow and experiment limits

The following constraints support the approved future phase. Concrete experiments,
tool selection and runtime contracts still require implementation and validation:

1. Start with a small local pilot: one character material, one tiling surface and
   one simple prop. Preserve originals and compare under identical camera/light
   conditions, then moving light, multiple distances and real animation. Measure
   peak VRAM, wall time and final runtime texture/memory cost before batching.
2. Keep a faithful character track and a separate generated-prop track. Preserve
   character identity, silhouette, proportions, original UV correspondence,
   skeleton, weights, attachment points and deformation. Generated geometry can
   be a detail donor for baking onto the retained mesh rather than a replacement
   rig. Retopology and weight transfer require explicit deformation validation.
3. Treat normal/roughness/height inference as plausible new material authoring,
   not recovery of canonical lost maps. Existing diffuse may contain baked light,
   highlights or occlusion; assess cleanup before adding dynamic PBR lighting.
   Color-map upscalers should not blindly process packed data or normal maps.
4. Process alpha/masks deliberately and preserve their semantics. Keep normal,
   roughness, height and AO data out of color transforms. Decode/filter/renormalize
   normals; spatial rotation/mirroring also requires correct vector-component
   transformation. Match the target tangent basis and green-channel convention.
   Check UV-island padding, mirrored seams, wrapping, tile boundaries, alpha edges,
   compression and mipmaps; inference tiling alone does not ensure texture tiling.
5. Preserve a manifest keyed by canonical resource identity plus source content
   hash, not basename alone. Record source package/subresource, model/code revision,
   checkpoint hash/license, settings/seed, transforms, map semantics, output hashes
   and review status. Never let a generated file silently replace an unrelated
   resource with the same basename.
6. Keep asset authoring distinct from runtime integration. A higher-resolution
   texture using existing shader semantics is a different change from adding PBR
   materials, normal/roughness channels, displacement, new mesh formats or a new
   import path. These require separate renderer/resource contracts and acceptance;
   successful GLB/FBX export is not evidence those consumers exist in the port.
7. Use the 24 GB Linux machine first, serial stages and measured tile sizes. Published
   hardware requirements are not a 3090 benchmark. Only consider rented capacity
   if an accepted quality target fails locally or measured throughput warrants it.

The approved scope is recorded in the linked brief. This research does not change
the current reconstruction sequence or establish local model performance, asset
quality, importer compatibility or completed renderer support.
