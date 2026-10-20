import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
import bbport_launcher_win as core
import bbport_startup
from bbport_launcher_backend import LauncherBackend


class FirstRunWizardTests(unittest.TestCase):
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

    def tearDown(self):
        for active in reversed(self.patches):
            active.stop()
        self.temporary.cleanup()

    def settings(self):
        return {**core.APP_DEFAULTS, **json.loads((self.root / 'settings.json').read_text(encoding='utf-8'))}

    def route(self, args=()):
        return bbport_startup.route(self.settings(), list(args), self.root, False)

    def test_wizard_survives_partial_setup_and_stops_after_finish(self):
        first = LauncherBackend()
        first.setValue('game_dir', str(self.root / 'game'))
        self.assertEqual(self.route()['page'], 'Setup')
        first.completeSetup()
        saved = self.settings()
        self.assertTrue(saved['setupCompleted'])
        self.assertEqual(saved['setupVersion'], bbport_startup.SETUP_VERSION)
        self.assertNotEqual(self.route()['page'], 'Setup')

    def test_finished_setup_with_game_launches_directly(self):
        game = self.root / 'CUSA03173'
        game.mkdir()
        (game / 'eboot.bin').write_bytes(b'\0')
        backend = LauncherBackend()
        backend.setValue('game_dir', str(game))
        backend.completeSetup()
        self.assertEqual(self.route()['mode'], 'direct')
        self.assertEqual(self.route(['--launcher'])['page'], 'Home')
        self.assertEqual(self.route(['--setup'])['page'], 'Setup')
        backend.setValue('launchDirectly', False)
        self.assertEqual(self.route()['mode'], 'launcher')

    def test_skip_opens_game_page_with_fix_banner(self):
        backend = LauncherBackend()
        backend.skipSetup()
        self.assertTrue(self.settings()['setupSkipped'])
        self.assertEqual(backend.startupProblem, 'Game files not found')
        route = self.route()
        self.assertEqual((route['page'], route['problem'], route['step']), ('Game', 'game', 1))
        backend.dismissProblem()
        self.assertEqual(backend.startupProblem, '')

    def test_fix_banner_clears_once_game_folder_is_valid(self):
        backend = LauncherBackend({'mode': 'launcher', 'page': 'Game', 'step': 1, 'problem': 'game'})
        self.assertEqual(backend.startPage, 'Game')
        self.assertEqual(backend.fixStep, 1)
        self.assertEqual(backend.startupProblem, 'Game files not found')
        game = self.root / 'game'
        game.mkdir()
        (game / 'eboot.bin').write_bytes(b'\0')
        backend.setValue('game_dir', str(game))
        self.assertEqual(backend.startupProblem, '')

    def download(self, members):
        """A Nexus-style download: a zip of {member path: bytes}."""
        import zipfile
        archive = self.root / f'download-{len(list(self.root.glob("download-*")))}.zip'
        with zipfile.ZipFile(archive, 'w') as package:
            for name, data in members.items():
                package.writestr(name, data)
        return archive

    def test_prompt_mods_come_from_the_players_own_download(self):
        backend = LauncherBackend()
        self.assertEqual(backend.promptChoice, 'PlayStation')  # the port ships no prompt mods
        texture = b'DCX\0' + b'keys' * 8
        # Cancelled: nothing changes and the notice says where to get it.
        with patch.object(LauncherBackend, '_choose_prompt_download', lambda self, choice: ''):
            backend.setPrompt('Keyboard')
        self.assertEqual(backend.promptChoice, 'PlayStation')
        self.assertIn('nexusmods.com/bloodborne/mods/460', backend.notice)
        keyboard = self.download({'PCBButtons/menu/common.tpf.dcx': texture, 'PCBButtons/readme.txt': b'hi'})
        with patch.object(LauncherBackend, '_choose_prompt_download', lambda self, choice: str(keyboard)):
            backend.setPrompt('Keyboard')
        self.assertEqual(backend.promptChoice, 'Keyboard')
        installed = self.root / 'mods' / core.KEYBOARD_PROMPTS
        self.assertEqual((installed / 'menu' / 'common.tpf.dcx').read_bytes(), texture)
        self.assertIn('JinkiJinn', (installed / 'SOURCE.txt').read_text(encoding='utf-8'))
        ini = (self.root / 'bbport.ini').read_text(encoding='utf-8')
        self.assertIn('key_roll=Left Shift', ini)
        self.assertIn('key_lock_on=Mouse3', ini)
        self.assertNotIn(core.KEYBOARD_PROMPTS, [m['name'] for m in backend.contentMods])
        # A download with several variants: the player picks the variant's own file.
        variants = self.download({'A/menu/common.tpf.dcx': texture, 'B/menu/common.tpf.dcx': texture})
        with patch.object(LauncherBackend, '_choose_prompt_download', lambda self, choice: str(variants)):
            backend.setPrompt('Xbox')
        self.assertIn('2 prompt variants', backend.notice)
        self.assertEqual(backend.promptChoice, 'Keyboard')
        chosen = self.root / 'unpacked' / 'common.tpf.dcx'
        chosen.parent.mkdir()
        chosen.write_bytes(b'DCX\0xbox')
        with patch.object(LauncherBackend, '_choose_prompt_download', lambda self, choice: str(chosen)):
            backend.setPrompt('Xbox')
        self.assertEqual(backend.promptChoice, 'Xbox')
        self.assertTrue((self.root / 'mods' / core.XBOX_PROMPTS / 'menu' / 'common.tpf.dcx').is_file())
        # What run.py reads at every start (BB_BUTTON_PROMPTS): the Xbox images, not the keys.
        sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
        import mods
        in_use = mods.selected(self.root / 'mods', self.root / 'mods.json')
        self.assertIn(core.XBOX_PROMPTS, in_use)
        self.assertNotIn(core.KEYBOARD_PROMPTS, in_use)
        # Both added: switching asks for nothing.
        with patch.object(LauncherBackend, '_choose_prompt_download', lambda self, choice: self.fail('asked')):
            backend.setPrompt('Keyboard')
            self.assertEqual(backend.promptChoice, 'Keyboard')
            # A choice made in the game's overlay (mods.json only) shows in the launcher too.
            profile = json.loads((self.root / 'mods.json').read_text())
            profile['disabled'] = sorted(set(profile['disabled']) - {core.XBOX_PROMPTS} | {core.KEYBOARD_PROMPTS})
            (self.root / 'mods.json').write_text(json.dumps(profile))
            backend.refresh_addons()
            self.assertEqual(backend.promptChoice, 'Xbox')
            backend.setPrompt('PlayStation')
            self.assertEqual(backend.promptChoice, 'PlayStation')

    def test_prompt_download_without_a_game_texture_is_refused(self):
        backend = LauncherBackend()
        for members in ({'readme.txt': b'no texture'}, {'menu/common.tpf.dcx': b'not a dcx'}):
            archive = self.download(members)
            with patch.object(LauncherBackend, '_choose_prompt_download', lambda self, choice: str(archive)):
                backend.setPrompt('Keyboard')
            with self.subTest(members=list(members)):
                self.assertEqual(backend.promptChoice, 'PlayStation')
                self.assertFalse((self.root / 'mods' / core.KEYBOARD_PROMPTS).exists())

    def test_launch_options_moved_out_of_advanced(self):
        backend = LauncherBackend()
        keys = {field['key'] for page in ('Advanced', 'Diagnostics') for field in backend.fields(page)}
        self.assertFalse(keys & {'close_on_play', 'launchDirectly', 'check_updates'})
        self.assertEqual([f['key'] for f in backend.fields('Diagnostics')],
                         ['frame_stats', 'gpu_profile', 'vk_validation'])
        pages = ('Graphics', 'Display', 'Game', 'Controller', 'Cheats', 'Online', 'Mods', 'Advanced', 'Diagnostics')
        every = [field['key'] for page in pages for field in backend.fields(page)]
        self.assertEqual(len(every), len(set(every)), 'a setting appears on two pages')


if __name__ == '__main__':
    unittest.main()
