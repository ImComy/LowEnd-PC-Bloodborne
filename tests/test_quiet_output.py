"""Scripts keep running when the launcher reading their output has closed (issues #84, #91)."""
from paths import ROOT
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

CHILD = '''
import sys
sys.path.insert(0, {scripts!r})
if {guarded}:
    import quiet_output
    quiet_output.guard()
sys.stdin.readline()  # the reader has gone by now
for n in range(50):
    print('shader code: programs', n)
    print('Mods: files replaced', n, file=sys.stderr)
print('done', file=open({marker!r}, 'w'))
'''


class QuietOutputTests(unittest.TestCase):
    def run_child(self, guarded, marker):
        code = CHILD.format(scripts=str(ROOT / 'scripts'), guarded=guarded, marker=str(marker))
        child = subprocess.Popen([sys.executable, '-c', code], stdin=subprocess.PIPE,
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        child.stdout.close()  # the launcher closed
        child.stdin.write(b'go\n')
        child.stdin.close()
        return child.wait(timeout=30)

    def test_closed_reader_fails_unguarded_print(self):
        with tempfile.TemporaryDirectory() as folder:
            marker = Path(folder) / 'done'
            self.assertNotEqual(self.run_child(False, marker), 0)
            self.assertFalse(marker.exists())

    def test_guarded_script_finishes(self):
        with tempfile.TemporaryDirectory() as folder:
            marker = Path(folder) / 'done'
            self.assertEqual(self.run_child(True, marker), 0)
            self.assertTrue(marker.exists())


if __name__ == '__main__':
    unittest.main()
