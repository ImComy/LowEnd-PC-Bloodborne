"""Overlapping buffer cache arenas on the real GPU (tests/test_arena_overlap.c; no game, no window)."""
from paths import ROOT
import os
import shutil
import subprocess
import unittest

SOURCE = ROOT/'tests/test_arena_overlap.c'
EXE = ROOT/'out/arena-overlap-test'


def compiler():
    found = shutil.which('clang') or shutil.which('cc')
    if not found and os.name == 'nt' and os.path.exists('C:/msys64/clang64/bin/clang.exe'):
        found = 'C:/msys64/clang64/bin/clang.exe'
    return found


@unittest.skipUnless(compiler(), 'needs clang and the Vulkan loader')
class ArenaOverlapTests(unittest.TestCase):
    def test_memory_bound_in_both_arenas_reads_the_same(self):
        EXE.parent.mkdir(exist_ok=True)
        lib = '-lvulkan-1' if os.name == 'nt' else '-lvulkan'
        build = subprocess.run([compiler(), '-O1', str(SOURCE), lib, '-o', str(EXE)],
                               capture_output=True, text=True, timeout=120)
        if build.returncode != 0:
            self.skipTest('cannot build against Vulkan: ' + build.stderr[-300:])
        r = subprocess.run([str(EXE.resolve())], capture_output=True, text=True, timeout=60)
        if r.stdout.startswith('SKIP'):
            self.skipTest(r.stdout.strip())
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn('all passed', r.stdout)


if __name__ == '__main__':
    unittest.main()
