#!/usr/bin/env python3
"""Copy verified color PNGs from a read-only game into a new pilot directory.

Usage (from the repository root, with Pillow installed):
  python3 research/tools/prepare_asset_upgrade_inputs.py \
    --game-root /path/to/BulletWitch-Steam --preset alicia-original \
    --output-dir research/evidence/asset-upgrade-pilot/inputs

The output directory must not exist and must remain outside the game. Original
PNG bytes are preserved; DDS member suffixes do not imply DDS encoding. The
bounded preset proves catalog -> archive/member and material BTX0 -> runtime
table relationships from the supplied game bytes, not filename convention.
It does not launch the game, produce replacements, or alter original assets.
"""

import argparse
import hashlib
import io
import json
from pathlib import Path
import struct

from PIL import Image

from compare_noesis_layout import compare
from cpr_layout import inspect
from layout_probe import rat
from menu_asset_inventory import png_extent


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unique(items, description):
    require(len(items) == 1, f'Expected one {description}; found {len(items)}')
    return items[0]


def prepare(game):
    """Validate every input before publishing any output; return original bytes."""
    catalog_rel = 'data/data/stable/stable.rat'
    base = 'data/data/stable/graphics/chara/PW0/'
    archive_rel = base + 'texture/texture.cpr'
    table_rel = base + 'texture/default.cpr'
    model_rel = base + 'model/mesh.cpr'
    paths = [catalog_rel, archive_rel, table_rel, model_rel]
    before = {rel: sha256((game / rel).read_bytes()) for rel in paths}
    catalog = rat(game / catalog_rel)
    archive = inspect(game / archive_rel)
    model = compare(game / model_rel)
    table_archive = inspect(game / table_rel)
    archive_bytes = (game / archive_rel).read_bytes()
    table_bytes = (game / table_rel).read_bytes()
    record = unique([r for r in table_archive['records']
                     if r['name'] == 'table._tm' and r['type'] == 'USER'],
                    'runtime texture table')
    offset = record['payload_offset']
    table = table_bytes[offset:offset + record['payload_bytes']]
    require(len(table) >= 4, 'Truncated runtime texture table')
    count, = struct.unpack_from('<I', table)
    require(len(table) == 4 + count * 40, 'Unexpected runtime texture table size')
    names = []
    for slot in range(count):
        prefix, raw_name, recorded_slot = struct.unpack_from('<I32sI', table, 4 + slot * 40)
        require(recorded_slot == slot, 'Runtime slot index differs from table order')
        require(b'\0' in raw_name, 'Unterminated runtime texture name')
        names.append((raw_name.split(b'\0')[0].decode('ascii'), prefix))

    assets = []
    copies = {}
    for key, output_name, crop in [
        ('pw0_body_m1_game_psd', 'alicia_body.png', [660, 20, 128, 128]),
        ('pw0_face_m1_game_psd', 'alicia_face.png', [120, 170, 128, 128]),
    ]:
        resource_index, resource = unique(
            [(i, r) for i, r in enumerate(catalog['resources'])
             if r['name'].lower() == key], f'catalog resource {key}')
        file_record = catalog['files'][resource['file_index']]
        mapped_archive = (Path(catalog_rel).parent / file_record['relative_path']).as_posix()
        require(mapped_archive == archive_rel, f'{key}: unexpected catalog archive')
        member = unique([r for r in archive['records']
                         if r['name'].lower() == key + '.dds' and r['type'] == 'DDS '],
                        f'PNG-bearing DDS record {key}')
        slot, (runtime_name, table_prefix) = unique(
            [(i, name) for i, name in enumerate(names) if name[0].lower() == key],
            f'runtime slot for {key}')
        references = [r for r in model['texture_references'] if r['indices'][0] == slot]
        require(references and all(r['type'] == 'BTX0' for r in references),
                f'{key}: not exclusively bound as BTX0 base color')
        offset = member['payload_offset']
        raw = archive_bytes[offset:offset + member['payload_bytes']]
        require(png_extent(raw, 0) == len(raw), f'{key}: PNG extent mismatch')
        with Image.open(io.BytesIO(raw)) as image:
            image.load()
            require(image.format == 'PNG', f'{key}: expected PNG encoding')
            dimensions = list(image.size)
            mode = image.mode
            extrema = image.convert('RGBA').getextrema()
            info = {k: str(v) for k, v in image.info.items()}
        # The bounded preset has opaque source maps. Do not silently treat new
        # alpha-bearing material data as straight-alpha color artwork.
        require(extrema[3] == (255, 255), f'{key}: expected opaque color map')
        require(crop[0] + crop[2] <= dimensions[0] and
                crop[1] + crop[3] <= dimensions[1], f'{key}: review crop exceeds image')
        copies[output_name] = raw
        assets.append({
            'resource_id': f"stable:{resource['name']}",
            'source_png': output_name,
            'source_sha256': sha256(raw),
            'semantic': 'color_srgb',
            'alpha_mode': 'opaque',
            'tiling': {'u': False, 'v': False},
            'comparison_crop': crop,
            'provenance': {
                'preset': 'alicia-original',
                'source_archive': archive_rel,
                'archive_sha256': before[archive_rel],
                'source_member': member['name'],
                'record_index': member['index'],
                'payload_offset': offset,
                'payload_bytes': len(raw),
                'dimensions': dimensions,
                'png_mode': mode,
                'rgba_extrema': extrema,
                'png_info': info,
                'png_crc_valid': True,
                'encoded_bytes_copied_without_conversion': True,
                'catalog': catalog_rel,
                'catalog_sha256': before[catalog_rel],
                'catalog_resource_index': resource_index,
                'catalog_resource_name': resource['name'],
                'catalog_file_index': resource['file_index'],
                'runtime_table_archive': table_rel,
                'runtime_table_archive_sha256': before[table_rel],
                'runtime_table_member': 'table._tm',
                'runtime_slot': slot,
                'runtime_texture_name': runtime_name,
                'runtime_table_prefix': table_prefix,
                'model_archive': model_rel,
                'model_archive_sha256': before[model_rel],
                'material_semantics': references,
                'role_evidence': (
                    'BTX0 model material references the exact runtime table slot; '
                    'world_renderer.mm defines 0x42545830 as base texture. '
                    'This is color artwork, not NMAP or AMOC.'),
                'color_encoding_note': (
                    'color_srgb is an offline model-processing assumption for color '
                    'artwork, not proven PNG transfer metadata or original runtime '
                    'decode. Original represented runtime samples UNORM without '
                    'sRGB conversion (lighting-redesign-acceptance.md L24). '
                    'Preserve encoded values; no gamma conversion.'),
                'tiling_note': (
                    'Character UV atlas; no periodic seam wrapping requested. '
                    'Runtime wrap sampling does not establish seamless tiling.'),
            },
        })
    for rel, original_hash in before.items():
        require(sha256((game / rel).read_bytes()) == original_hash,
                f'Source changed during extraction: {rel}')
    return {'schema_version': 1, 'assets': assets}, copies


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    parser.add_argument('--preset', required=True, choices=['alicia-original'])
    args = parser.parse_args()
    game = args.game_root.expanduser().resolve(strict=True)
    output = args.output_dir.expanduser().resolve()
    if output == game or game in output.parents:
        parser.error('Output must remain outside the read-only game directory')
    if args.output_dir.expanduser().is_symlink() or output.exists():
        parser.error('Output directory must not exist')
    try:
        manifest, copies = prepare(game)
        output.mkdir(parents=True, exist_ok=False)
        for name, raw in copies.items():
            with (output / name).open('xb') as destination:
                destination.write(raw)
        with (output / 'manifest.json').open('x') as destination:
            json.dump(manifest, destination, indent=2)
            destination.write('\n')
    except (OSError, ValueError, AssertionError, struct.error) as error:
        parser.exit(1, f'Extraction failed: {error}\n')
    print(json.dumps({'manifest': str(output / 'manifest.json'),
                      'assets': [{'resource_id': a['resource_id'],
                                  'source_sha256': a['source_sha256'],
                                  'dimensions': a['provenance']['dimensions']}
                                 for a in manifest['assets']]}, indent=2))


if __name__ == '__main__':
    main()
