from paths import ROOT
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('run', ROOT / 'run.py')
run = importlib.util.module_from_spec(spec)
spec.loader.exec_module(run)


class RunScriptTests(unittest.TestCase):
    def setUp(self):
        folder = tempfile.TemporaryDirectory()
        self.addCleanup(folder.cleanup)
        port = Path(folder.name)
        (port / 'scripts').mkdir()
        (port / 'scripts' / 'ok.py').write_text("print('made')\n")
        (port / 'scripts' / 'fails.py').write_text("raise SystemExit(3)\n")
        for patch in (mock.patch.object(run, 'PORT', port), mock.patch.object(run, 'no_console', lambda: 0)):
            patch.start()
            self.addCleanup(patch.stop)

    def test_success_gives_the_output_or_an_empty_string(self):
        self.assertEqual(run.run_script('ok.py', capture=True), 'made\n')
        self.assertEqual(run.run_script('ok.py', capture=True, optional=True), 'made\n')

    def test_a_failure_stops_the_start_unless_the_script_is_optional(self):
        # Issue #8: the PC enhancements' failure stopped the game.
        with self.assertRaises(SystemExit) as stop:
            run.run_script('fails.py', capture=True)
        self.assertEqual(stop.exception.code, 3)
        self.assertIsNone(run.run_script('fails.py', capture=True, optional=True))

    def test_an_incomplete_port_says_what_is_missing(self):
        # Issue #118: a repack without scripts/game_check.py stopped with a traceback.
        self.assertEqual(run.missing_port_files(ROOT), [])
        missing = run.missing_port_files()
        self.assertIn('scripts\\game_check.py', missing)
        text = run.explain_missing_port_files(missing)
        self.assertIn('scripts\\game_check.py', text)
        self.assertIn(f'{len(missing) - 1} more', text)
        self.assertIn(run.RELEASES, text)


if __name__ == '__main__':
    unittest.main()
