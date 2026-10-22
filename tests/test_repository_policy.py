"""Exercise the staged-content and history guard in disposable Git repositories."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('policy', REPO/'tools/check_repository.py')
policy = importlib.util.module_from_spec(spec)
spec.loader.exec_module(policy)

class RepositoryPolicyTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bullet-witch-policy-')
        self.root = Path(self.temp.name)
        self.previous_root = policy.ROOT
        policy.ROOT = self.root
        self.git('init', '-q', '-b', 'main')
        (self.root/'.gitignore').write_bytes((REPO/'.gitignore').read_bytes())
        self.git('add', '.gitignore')

    def tearDown(self):
        policy.ROOT = self.previous_root
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.run(['git', *args], cwd=self.root, check=True,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE).stdout

    def add(self, path, content):
        p = self.root/path
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_bytes(content)
        self.git('add', '-f', '--', path)

    def test_source_and_reviewed_summary_allowed(self):
        self.add('src/main.cpp', b'int main() { return 0; }\n')
        self.add('research/evidence/layout/validation-summary.json', b'{"ok":true}\n')
        failures, count = policy.check(policy.staged())
        self.assertEqual(failures, [])
        self.assertEqual(count, 3)

    def test_force_added_assets_and_unreviewed_evidence_rejected(self):
        for path in ['src/model.gltf', 'research/extracted/model.dat',
                     'research/evidence/new-summary.json']:
            self.add(path, b'{}\n')
        failures, _ = policy.check(policy.staged())
        self.assertEqual(len(failures), 3)
        self.assertTrue(all('ignored/local-only' in failure for failure in failures))

    def test_checks_index_not_working_copy(self):
        self.add('payload.txt', b'raw\x00payload')
        (self.root/'payload.txt').write_text('now harmless\n')
        failures, _ = policy.check(policy.staged())
        self.assertEqual(len(failures), 1)
        self.assertIn('binary/control-byte', failures[0])

    def test_oversized_content_and_symlink_rejected(self):
        self.add('large.txt', b'x'*(policy.MAX_BYTES+1))
        (self.root/'link.txt').symlink_to('large.txt')
        self.git('add', 'link.txt')
        failures, _ = policy.check(policy.staged())
        self.assertEqual(len(failures), 2)
        self.assertTrue(any('larger than' in failure for failure in failures))
        self.assertTrue(any('link or embedded repository' in failure for failure in failures))

    def test_push_hook_rejects_asset_only_in_history(self):
        # The synthetic commits exist only in this temporary test repository.
        self.add('game-data/model.fbx', b'synthetic fixture\n')
        self.git('-c', 'user.name=Policy Test', '-c', 'user.email=test@example.invalid',
                 '-c', 'commit.gpgsign=false', 'commit', '-qm', 'Synthetic asset fixture')
        self.git('rm', 'game-data/model.fbx')
        self.git('-c', 'user.name=Policy Test', '-c', 'user.email=test@example.invalid',
                 '-c', 'commit.gpgsign=false', 'commit', '-qm', 'Remove fixture')
        self.assertEqual(policy.check(policy.staged())[0], [])
        self.assertTrue(any('game-data/model.fbx' in failure for failure in policy.check(policy.history('HEAD'))[0]))
        (self.root/'tools').mkdir()
        (self.root/'tools/check_repository.py').write_bytes((REPO/'tools/check_repository.py').read_bytes())
        oid = self.git('rev-parse', 'HEAD').decode().strip()
        result = subprocess.run(['sh', str(REPO/'.githooks/pre-push')], cwd=self.root,
            input=f'refs/heads/main {oid} refs/heads/main {"0"*40}\n'.encode(),
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'game-data/model.fbx', result.stdout)

if __name__ == '__main__':
    unittest.main()
