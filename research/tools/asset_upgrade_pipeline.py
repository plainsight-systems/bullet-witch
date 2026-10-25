#!/usr/bin/env python3
"""Hash-bound, offline color-texture experiment; never writes to source resources.

Validate explicit color/alpha/tiling contracts and model identities first. Produce
baselines, execute one float32 CUDA model at a time through pinned ComfyUI tiling,
then save per-asset comparisons and raw material predictions. New run directories
are exclusive; caught failures preserve provenance and already completed outputs.
See research/reports/2026-10-25-asset-upgrade-pipeline.md for source and limits.
"""

import argparse
import gc
import hashlib
import importlib.metadata
import json
import platform
import re
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

COMFY_REVISION = "63be2f9b82ad0dac88a7ceaa28a9cf3154125a18"
COMFY_UTILS_SHA256 = "9320cf5c624e96acc1d9ec92ff120ed6387a2142707f3194cb21dd0d11ac2500"
COLOR_MODELS = ("realesrgan_x4plus", "pbrify_upscaler_v4")
MATERIAL_MODELS = ("pbrify_normal_v3", "pbrify_roughness_v2")


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def check_hash(path, expected):
    if not isinstance(expected, str) or not re.fullmatch(r"[0-9a-f]{64}", expected):
        raise ValueError(f"Invalid SHA256 for {path}")
    actual = sha256(path)
    if actual != expected:
        raise ValueError(f"SHA256 mismatch for {path}: expected {expected}, got {actual}")
    return actual


def contained_path(root, relative):
    if not isinstance(relative, str) or Path(relative).is_absolute():
        raise ValueError("Input paths must be relative strings")
    result = (root / relative).resolve(strict=True)
    if not result.is_relative_to(root) or not result.is_file():
        raise ValueError(f"Input path escapes root or is not a file: {relative}")
    return result


def write_status(root, state):
    temporary = root / "run.json.tmp"
    temporary.write_text(json.dumps(state, indent=2) + "\n")
    temporary.replace(root / "run.json")


def validate_inputs(args):
    from PIL import Image

    manifest = json.loads(args.manifest.read_text())
    if manifest.get("schema_version") != 1 or not manifest.get("assets"):
        raise ValueError("Expected schema_version 1 and a nonempty assets list")
    seen = set()
    for asset in manifest["assets"]:
        identity = asset.get("resource_id")
        if not isinstance(identity, str) or not identity.strip() or identity in seen:
            raise ValueError(f"Missing or duplicate resource_id: {identity}")
        seen.add(identity)
        if asset.get("semantic") != "color_srgb":
            raise ValueError(f"Only explicit color_srgb textures are accepted: {identity}")
        if asset.get("alpha_mode") not in ("opaque", "straight"):
            raise ValueError(f"Unspecified/unsupported alpha semantics: {identity}")
        tiling = asset.get("tiling")
        if not isinstance(tiling, dict) or any(tiling.get(axis) is not False for axis in ("u", "v")):
            raise ValueError(f"Only explicitly non-wrapping textures are supported: {identity}")
        path = contained_path(args.input_root, asset["source_png"])
        check_hash(path, asset["source_sha256"])
        with path.open("rb") as stream:
            header = stream.read(26)
        if len(header) < 26 or header[:8] != b"\x89PNG\r\n\x1a\n" or header[24] != 8 or header[25] not in (2, 6):
            raise ValueError(f"Expected 8-bit PNG: {identity}")
        with Image.open(path) as source:
            if source.format != "PNG" or source.mode not in ("RGB", "RGBA") or getattr(source, "n_frames", 1) != 1:
                raise ValueError(f"Expected RGB/RGBA PNG: {identity}")
            source.load()
            crop = asset.get("comparison_crop")
            if crop is not None and (not isinstance(crop, list) or len(crop) != 4 or
                    any(type(value) is not int for value in crop) or min(crop[:2]) < 0 or
                    min(crop[2:]) <= 0 or crop[0] + crop[2] > source.width or
                    crop[1] + crop[3] > source.height):
                raise ValueError(f"Invalid source-space comparison_crop: {identity}")
            if asset["alpha_mode"] == "opaque" and source.convert("RGBA").getchannel("A").getextrema() != (255, 255):
                raise ValueError(f"Nonopaque source contradicts alpha_mode: {identity}")
        if "original_dds" in asset:
            dds = asset["original_dds"]
            check_hash(contained_path(args.input_root, dds["path"]), dds["sha256"])
    return manifest


def selected_models(args):
    keys = list(COLOR_MODELS)
    if args.materials:
        keys.extend(MATERIAL_MODELS)
    if args.height:
        keys.append("pbrify_height")
    catalog = json.loads(args.catalog.read_text())["models"]
    selected = {}
    for key in keys:
        entry = dict(catalog[key])
        path = (args.catalog.parent / entry["path"]).resolve(strict=True)
        check_hash(path, entry["sha256"])
        if path.suffix not in (".pth", ".safetensors"):
            raise ValueError(f"Unsupported checkpoint format: {path}")
        entry["path"] = str(path)
        selected[key] = entry
    return selected


def register_file(path, run_root, semantic):
    return {"path": str(path.relative_to(run_root)), "sha256": sha256(path),
            "bytes": path.stat().st_size, "semantic": semantic}


def save_image(image, path, state_asset, run_root, semantic):
    with path.open("xb") as stream:
        image.save(stream, format="PNG")
    record = register_file(path, run_root, semantic)
    record.update(size=list(image.size), mode=image.mode)
    state_asset["outputs"].append(record)


def runtime(args, state):
    # Import the externally installed implementation rather than vendoring it.
    root = args.comfy_root.resolve(strict=True)
    revision = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if revision != COMFY_REVISION:
        raise ValueError(f"ComfyUI revision must be {COMFY_REVISION}, found {revision}")
    check_hash(root / "comfy/utils.py", COMFY_UTILS_SHA256)
    sys.path.insert(0, str(root))
    import torch
    import comfy.utils
    import numpy as np
    from spandrel import ImageModelDescriptor, ModelLoader
    from safetensors.torch import load_file

    if not torch.cuda.is_available():
        raise RuntimeError("CUDA is required for this experiment")
    torch.backends.cuda.matmul.allow_tf32 = False
    torch.backends.cudnn.allow_tf32 = False
    torch.backends.cudnn.benchmark = False
    state["environment"] = {
        "python": platform.python_version(), "platform": platform.platform(),
        "packages": {key: importlib.metadata.version(key) for key in
                     ("torch", "spandrel", "safetensors", "numpy", "Pillow")},
        "cuda": torch.version.cuda, "gpu": torch.cuda.get_device_name(0),
        "comfy_revision": revision, "comfy_utils_sha256": COMFY_UTILS_SHA256,
        "nvidia_smi": subprocess.check_output([
            "nvidia-smi", "--query-gpu=name,driver_version,power.limit", "--format=csv,noheader"], text=True).strip(),
    }
    return torch, comfy.utils, np, ImageModelDescriptor, ModelLoader, load_file


def infer(image, model, args, libs):
    torch, utils, np, *_ = libs
    pixels = np.array(image.convert("RGB"), dtype=np.float32) / 255.0
    tensor = torch.from_numpy(pixels).permute(2, 0, 1).unsqueeze(0)
    torch.cuda.synchronize()
    torch.cuda.reset_peak_memory_stats()
    started = time.perf_counter()
    with torch.inference_mode():
        # Keep the complete image on CPU; only the current tile resides on CUDA.
        output = utils.tiled_scale(
            tensor, lambda tile: model(tile.to("cuda", dtype=torch.float32)),
            tile_x=args.tile, tile_y=args.tile, overlap=args.overlap,
            upscale_amount=model.scale, out_channels=3, output_device="cpu")
    torch.cuda.synchronize()
    metrics = {
        "seconds": time.perf_counter() - started,
        "timing_scope": "tile transfer, inference, CPU assembly; excludes load and encoding",
        "peak_allocated_bytes": torch.cuda.max_memory_allocated(),
        "peak_reserved_bytes": torch.cuda.max_memory_reserved(),
        "input_size": list(image.size), "output_size": list(output.shape[-2:][::-1]),
    }
    if not torch.isfinite(output).all():
        raise ValueError("Model returned nonfinite values")
    array = output[0].permute(1, 2, 0).numpy()
    metrics["raw_min"] = float(array.min())
    metrics["raw_max"] = float(array.max())
    metrics["clipped_values"] = int(((array < 0) | (array > 1)).sum())
    return array, metrics


def preview(array, alpha, libs):
    from PIL import Image

    np = libs[2]
    result = Image.fromarray(np.rint(np.clip(array, 0, 1) * 255).astype(np.uint8))
    if alpha is not None:
        result.putalpha(alpha.resize(result.size, Image.Resampling.BILINEAR))
    return result


def display_rgb(image):
    from PIL import Image

    background = Image.new("RGB", image.size, (80, 80, 80))
    if image.mode == "RGBA":
        background.paste(image, mask=image.getchannel("A"))
    else:
        background.paste(image)
    return background


def comparisons(asset_dir, state_asset, args):
    from PIL import Image, ImageDraw

    filenames = ["source.png", "bicubic_4x.png"] + [key + ".png" for key in COLOR_MODELS]
    pictures = []
    for name in filenames:
        with Image.open(asset_dir / name) as image:
            pictures.append(image.copy())
    size = pictures[1].size
    pictures[0] = pictures[0].resize(size, Image.Resampling.NEAREST)
    labels = ["Original (nearest 4x)", "Bicubic 4x", "Real-ESRGAN 4x", "PBRify DAT 4x"]
    width = 512
    overview = Image.new("RGB", (width * 4, width + 40), (24, 24, 24))
    draw = ImageDraw.Draw(overview)
    for column, (label, picture) in enumerate(zip(labels, pictures)):
        thumb = display_rgb(picture)
        thumb.thumbnail((width, width), Image.Resampling.LANCZOS)
        overview.paste(thumb, (column * width, 40))
        draw.text((column * width + 8, 10), label, fill="white")
    save_image(overview, asset_dir / "comparison_overview.png", state_asset, args.output_dir, "review_only")
    crop_width, crop_height = min(width, size[0]), min(width, size[1])
    for name, x, y in [("center", .5, .5), ("top_left", 0, 0), ("top_right", 1, 0),
                       ("bottom_left", 0, 1), ("bottom_right", 1, 1)]:
        left = round((size[0] - crop_width) * x)
        top = round((size[1] - crop_height) * y)
        box = (left, top, left + crop_width, top + crop_height)
        sheet = Image.new("RGB", (width * 4, crop_height + 64), (24, 24, 24))
        draw = ImageDraw.Draw(sheet)
        for column, (label, picture) in enumerate(zip(labels, pictures)):
            draw.text((column * width + 8, 8), label, fill="white")
            draw.text((column * width + 8, 28), f"Output pixels {box}", fill="white")
            sheet.paste(display_rgb(picture.crop(box)), (column * width, 64))
        save_image(sheet, asset_dir / f"comparison_crop_{name}.png", state_asset, args.output_dir, "review_only_actual_output_pixels")
    selected = state_asset["source"].get("comparison_crop")
    if selected is not None:
        x, y, w, h = (value * 4 for value in selected)
        box = (x, y, x + w, y + h)
        sheet = Image.new("RGB", (w * 4, h + 64), (24, 24, 24))
        draw = ImageDraw.Draw(sheet)
        for column, (label, picture) in enumerate(zip(labels, pictures)):
            draw.text((column * w + 8, 8), label, fill="white")
            draw.text((column * w + 8, 28), f"Source crop {selected}", fill="white")
            sheet.paste(display_rgb(picture.crop(box)), (column * w, 64))
        save_image(sheet, asset_dir / "comparison_crop_selected.png", state_asset, args.output_dir, "review_only_actual_output_pixels")


def execute(args, state, manifest, catalog):
    from PIL import Image

    libs = runtime(args, state)
    torch, _, np, descriptor_type, loader_type, safe_load = libs
    for index, asset in enumerate(manifest["assets"]):
        directory = args.output_dir / f"asset_{index:03d}"
        directory.mkdir()
        record = {"source": asset, "directory": directory.name, "outputs": [], "inference": {}}
        state["assets"].append(record)
        source = contained_path(args.input_root, asset["source_png"])
        with (directory / "source.png").open("xb") as destination, source.open("rb") as stream:
            shutil.copyfileobj(stream, destination)
        check_hash(directory / "source.png", asset["source_sha256"])
        record["outputs"].append(register_file(directory / "source.png", args.output_dir, "original_copy"))
        with Image.open(source) as image:
            rgb = image.convert("RGB")
            result = rgb.resize((image.width * 4, image.height * 4), Image.Resampling.BICUBIC)
            if asset["alpha_mode"] == "straight":
                result.putalpha(image.getchannel("A").resize(result.size, Image.Resampling.BILINEAR)
                                if image.mode == "RGBA" else Image.new("L", result.size, 255))
            save_image(result, directory / "bicubic_4x.png", record, args.output_dir, "encoded_color_working_assumption")
    write_status(args.output_dir, state)

    # Loop by model: one checkpoint and model instance serves every selected asset.
    for key, entry in catalog.items():
        checkpoint = Path(entry["path"])
        check_hash(checkpoint, entry["sha256"])
        weights = safe_load(str(checkpoint), device="cpu") if checkpoint.suffix == ".safetensors" else torch.load(checkpoint, map_location="cpu", weights_only=True)
        model = loader_type().load_from_state_dict(weights).eval()
        del weights
        if not isinstance(model, descriptor_type) or model.input_channels != 3 or model.output_channels != 3:
            raise ValueError(f"Expected RGB image model: {key}")
        expected_scale = 4 if key in COLOR_MODELS else 1
        if model.scale != expected_scale:
            raise ValueError(f"Unexpected scale for {key}: {model.scale}")
        model.to(device="cuda", dtype=torch.float32)
        try:
            for record in state["assets"]:
                asset = record["source"]
                directory = args.output_dir / record["directory"]
                source_path = directory / ("source.png" if key in COLOR_MODELS else "pbrify_upscaler_v4.png")
                with Image.open(source_path) as source:
                    image = source.convert("RGB")
                with Image.open(directory / "source.png") as original:
                    alpha = original.getchannel("A") if original.mode == "RGBA" and asset["alpha_mode"] == "straight" else None
                if key not in COLOR_MODELS:
                    image.thumbnail((args.material_max_size, args.material_max_size), Image.Resampling.LANCZOS)
                print(f"{key}: {asset['resource_id']} {image.size}", flush=True)
                array, metrics = infer(image, model, args, libs)
                metrics.update(model_sha256=entry["sha256"], architecture=model.architecture.id)
                if key not in COLOR_MODELS:
                    raw_path = directory / f"{key}_raw.npy"
                    with raw_path.open("xb") as stream:
                        np.save(stream, array, allow_pickle=False)
                    record["outputs"].append(register_file(raw_path, args.output_dir, "uncalibrated_raw_rgb_data_no_gamma_transform"))
                    metrics["tangent_convention"] = "unknown_unvalidated" if key == "pbrify_normal_v3" else "not_applicable"
                    if key == MATERIAL_MODELS[0] and alpha is not None:
                        save_image(alpha.resize(image.size, Image.Resampling.BILINEAR), directory / "material_source_alpha.png", record, args.output_dir, "bilinear_source_alpha")
                result = preview(array, alpha if key in COLOR_MODELS else None, libs)
                save_image(result, directory / f"{key}.png", record, args.output_dir,
                           "encoded_color_working_assumption" if key in COLOR_MODELS else "uncalibrated_raw_rgb_data_preview_no_gamma_transform")
                record["inference"][key] = metrics
                del array, result
                write_status(args.output_dir, state)
        finally:
            del model
            gc.collect()
            torch.cuda.empty_cache()
    for record in state["assets"]:
        comparisons(args.output_dir / record["directory"], record, args)
    # Detect source changes during the run; every output remains tied to original hashes.
    validate_inputs(args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("manifest", "input-root", "catalog", "comfy-root", "output-dir"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--tile", type=int, default=256)
    parser.add_argument("--overlap", type=int, default=32)
    parser.add_argument("--materials", action="store_true")
    parser.add_argument("--height", action="store_true")
    parser.add_argument("--material-max-size", type=int, default=1024)
    parser.add_argument("--validate-only", action="store_true")
    args = parser.parse_args()
    args.input_root = args.input_root.resolve(strict=True)
    args.output_dir = args.output_dir.resolve()
    if args.output_dir.is_relative_to(args.input_root):
        parser.error("Output directory must be outside the input root")
    if args.tile < 64 or not 0 <= args.overlap < args.tile or args.material_max_size < 64:
        parser.error("Require tile >=64, 0 <= overlap < tile, material-max-size >=64")
    if args.height and not args.materials:
        parser.error("--height requires --materials")
    args.output_dir.mkdir(parents=True, exist_ok=False)
    state = {"schema_version": 1, "status": "running", "started_utc": datetime.now(timezone.utc).isoformat(),
             "script_sha256": sha256(Path(__file__)), "settings": {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
             "color_policy": "Preserve encoded RGB bytes; color_srgb is a model working assumption, not original sampler evidence",
             "precision": "float32 CUDA, TF32 off", "assets": []}
    write_status(args.output_dir, state)
    try:
        state["manifest_sha256"] = sha256(args.manifest)
        state["catalog_sha256"] = sha256(args.catalog)
        manifest = validate_inputs(args)
        state["input_manifest"] = manifest
        catalog = selected_models(args)
        state["models"] = catalog
        if not args.validate_only:
            execute(args, state, manifest, catalog)
        state["status"] = "validated" if args.validate_only else "complete"
        return 0
    except BaseException as error:
        state["status"] = "failed"
        state["error"] = {"type": type(error).__name__, "message": str(error)}
        print(f"Experiment failed: {error}", file=sys.stderr)
        return 1
    finally:
        state["finished_utc"] = datetime.now(timezone.utc).isoformat()
        write_status(args.output_dir, state)


if __name__ == "__main__":
    raise SystemExit(main())
