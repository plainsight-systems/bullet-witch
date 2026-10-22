"""End-to-end CLI tests with authored bytes, never game assets."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())


def cpr():
    data = bytearray(160)
    data[:4] = b'CPR1'
    for offset, value in {4: 1, 8: 0x11223344, 16: 48, 24: 16, 44: 64,
                          100: 1, 104: 8, 108: 0xffffffff}.items():
        struct.pack_into('<I', data, offset, value)
    data[96:100] = b'USER'
    data[145:151] = b'member'
    data[152:156] = b'root'
    return data


def rat():
    data = bytearray(132)
    data[:4] = b'RAT '
    for offset, value in {4: 132, 16: 64, 20: 20, 32: 84, 36: 20,
                          48: 104, 52: 8, 56: 112, 60: 20,
                          68: 0xffffffff, 76: 1, 84: 1, 100: 0xffffffff, 108: 6}.items():
        struct.pack_into('<I', data, offset, value)
    data[113:117] = b'root'
    data[118:126] = b'data.cpr'
    return data


class ResourceCLI(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / 'data.cpr').write_bytes(cpr())
        (self.root / 'catalog.rat').write_bytes(rat())

    def run_cli(self, *args, code=0):
        result = subprocess.run([BINARY, *map(str, args)], capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, code, result.stderr)
        return result

    def test_resolve_uses_publication_name_and_case_fold(self):
        hit = json.loads(self.run_cli('resolve', 'ROOT', '--catalog', 30, self.root / 'catalog.rat').stdout)
        self.assertEqual(hit['publication_record_indices'], [0])
        self.assertEqual(hit['archive']['records'][0]['name'], 'member')
        self.assertEqual(Path(hit['archive_path']), self.root.resolve() / 'data.cpr')
        self.assertEqual(hit['dependencies'], [])

    def test_missing_ambiguous_invalid_priority(self):
        mount = ['--catalog', 30, self.root / 'catalog.rat']
        self.assertEqual(json.loads(self.run_cli('resolve', 'absent', *mount, code=3).stderr)['error'], 'not_found')
        self.assertEqual(json.loads(self.run_cli('resolve', 'root', *mount, *mount, code=4).stderr)['error'], 'ambiguous')
        self.run_cli('resolve', 'root', '--catalog', '30x', self.root / 'catalog.rat', code=1)

    def test_inventory_continues_after_bad_archive(self):
        (self.root / 'broken.cpr').write_bytes(b'bad')
        rows = [json.loads(line) for line in self.run_cli('inventory', self.root, code=2).stdout.splitlines()]
        self.assertEqual(len(rows), 3)
        self.assertEqual(sum('error' in row for row in rows), 1)
        self.assertEqual({row.get('kind') for row in rows}, {None, 'cpr', 'rat'})

    @unittest.skipIf(sys.platform == 'win32', 'Symlink creation requires separate privileges on Windows')
    def test_archive_symlink_cannot_escape_catalog_directory(self):
        sub = self.root / 'sub'
        sub.mkdir()
        (sub / 'catalog.rat').write_bytes(rat())
        (sub / 'data.cpr').symlink_to(self.root / 'data.cpr')
        result = self.run_cli('resolve', 'root', '--catalog', 30, sub / 'catalog.rat', code=2)
        self.assertEqual(json.loads(result.stderr)['error'], 'malformed')


if __name__ == '__main__':
    unittest.main()
