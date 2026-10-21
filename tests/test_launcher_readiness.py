import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
from bbport_readiness import check_readiness
from bbport_readiness import save_title_id

TEMP_PARENT = Path(__file__).resolve().parents[1] / 'out'


class LauncherReadinessTests(unittest.TestCase):
    def test_fresh_install_then_game_and_save_are_detected(self):
        TEMP_PARENT.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_PARENT) as temporary:
            root = Path(temporary)
            game = root / 'CUSA03173'
            user = root / 'user'
            self.assertEqual([row['level'] for row in check_readiness(game, user, lambda _: None)],
                             ['error', 'warning', 'warning'])
            for directory in ('dvdroot_ps4', 'sce_module', 'sce_sys/trophy'):
                (game / directory).mkdir(parents=True)
            (game / 'eboot.bin').write_bytes(b'game')
            (game / 'sce_sys/trophy/trophy00.trp').write_bytes(bytes.fromhex('DCA24D00'))
            user.mkdir()
            (user / 'keys.json').write_text(json.dumps({'TrophyKeySet':
                {'ReleaseTrophyKey': 'AB' * 16}}), encoding='utf-8')
            save = user / 'savedata/1/CUSA03173/SPRJ0005/userdata0010'
            save.parent.mkdir(parents=True)
            save.write_bytes(b'saved game')
            rows = check_readiness(game, user, lambda _: ('Bloodborne', '01.09'))
            self.assertEqual(rows[0]['level'], 'ready')
            self.assertIn('verify decryption', rows[1]['detail'])
            self.assertEqual(rows[2]['level'], 'ready')

    def test_bad_pack_and_key_do_not_appear_ready(self):
        TEMP_PARENT.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_PARENT) as temporary:
            root = Path(temporary)
            game = root / 'CUSA03173'
            user = root / 'user'
            (game / 'sce_sys/trophy').mkdir(parents=True)
            (game / 'sce_sys/trophy/TROPHY.TRP').write_bytes(b'bad pack')
            user.mkdir()
            (user / 'keys.json').write_text('{"TrophyKeySet":{"ReleaseTrophyKey":"wrong"}}',
                                            encoding='utf-8')
            rows = check_readiness(game, user, lambda _: None)
            self.assertEqual(rows[1]['level'], 'error')
            self.assertEqual(rows[2]['level'], 'warning')

    def test_shadps4_savedata_folder_is_explained(self):
        # shadPS4's ...\home\1000\savedata as the Saves folder: <title>\SPRJ* lies directly in it.
        TEMP_PARENT.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_PARENT) as temporary:
            root = Path(temporary)
            game = root / 'CUSA03173'
            user = root / 'savedata'
            save = user / 'CUSA03173/SPRJ0005/userdata0000'
            save.parent.mkdir(parents=True)
            save.write_bytes(b'saved game')
            rows = check_readiness(game, user, lambda _: None)
            self.assertEqual(rows[2]['level'], 'warning')
            self.assertIn("shadPS4's savedata folder", rows[2]['detail'])
            # Its parent (home\1000), as the old "user folder" is chosen.
            rows = check_readiness(game, root, lambda _: None)
            self.assertIn("shadPS4's savedata folder", rows[2]['detail'])

    def test_save_id_comes_from_game_metadata(self):
        TEMP_PARENT.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=TEMP_PARENT) as temporary:
            game = Path(temporary) / 'CUSA03173'
            (game / 'sce_sys').mkdir(parents=True)
            (game / 'sce_sys/param.sfo').write_bytes(b'metadata')
            with patch.dict(sys.modules, {'prepare': SimpleNamespace(sfo=lambda _: {
                'TITLE_ID': 'CUSA03173', 'INSTALL_DIR_SAVEDATA': 'CUSA00207'})}):
                self.assertEqual(save_title_id(game), 'CUSA00207')


if __name__ == '__main__':
    unittest.main()
