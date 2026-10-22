import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
import bbport_launcher_win as core
from bbport_launcher_backend import LauncherBackend


class InGameSettingsKeptTests(unittest.TestCase):
    """The in-game menu writes bbport.ini and the in-game Fullscreen switch settings.json while the
    launcher is open; the launcher's next write keeps them (GitHub issues #124, #100)."""

    def setUp(self):
        parent = Path(__file__).resolve().parents[1] / 'out'
        parent.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=parent)
        self.root = Path(self.temporary.name)
        self.patches = [patch.object(core, 'CONFIG_DIR', self.root),
                        patch.object(core, 'CONFIG_FILE', self.root / 'settings.json'),
                        patch.object(core, 'DATA_DIR', self.root),
                        patch.dict(os.environ, {'BB_CONFIG': str(self.root / 'bbport.ini')}),
                        patch.dict(core.APP_DEFAULTS, {'check_updates': False,
                                                       'game_dir': str(self.root / 'missing')}),
                        patch.object(LauncherBackend, '_detect_gpu', lambda self: None)]
        for active in self.patches:
            active.start()
        self.backend = LauncherBackend()
        self.backend.setValue('fullscreen', True)
        self.backend.setValue('output_res', '1920x1080')

    def tearDown(self):
        for active in reversed(self.patches):
            active.stop()
        self.temporary.cleanup()

    def game_writes(self, ini=None, app=None):
        """As the engine does: bbport.ini's line rewritten, settings.json's key replaced."""
        path = self.root / 'bbport.ini'
        lines = path.read_text(encoding='utf-8').splitlines()
        for key, value in (ini or {}).items():
            lines = [f'{key}={value}' if line.split('=', 1)[0] == key else line for line in lines]
        path.write_text('\n'.join(lines) + '\n', encoding='utf-8')
        settings = json.loads((self.root / 'settings.json').read_text(encoding='utf-8'))
        settings.update(app or {})
        (self.root / 'settings.json').write_text(json.dumps(settings), encoding='utf-8')

    def saved(self):
        return (json.loads((self.root / 'settings.json').read_text(encoding='utf-8')),
                core.load_ini()[0])

    def test_launcher_change_keeps_the_games(self):
        self.game_writes(ini={'output_res': '1280x720'}, app={'fullscreen': False})
        self.backend.setValue('player_name', 'Hunter')
        app, ini = self.saved()
        self.assertFalse(app['fullscreen'])
        self.assertEqual(ini['output_res'], '1280x720')
        self.assertEqual(app['player_name'], 'Hunter')

    def test_launcher_own_change_wins(self):
        self.game_writes(ini={'output_res': '1280x720'}, app={'fullscreen': False})
        self.backend.setValue('output_res', '2560x1440')
        self.backend.setValue('fullscreen', True)
        app, ini = self.saved()
        self.assertTrue(app['fullscreen'])
        self.assertEqual(ini['output_res'], '2560x1440')

    def test_shown_after_the_game_exits(self):
        self.game_writes(ini={'output_res': '1280x720'}, app={'fullscreen': False})
        self.backend._receive_exit(0)
        self.assertEqual(self.backend.value('output_res'), '1280x720')
        self.assertFalse(self.backend.value('fullscreen'))


if __name__ == '__main__':
    unittest.main()
