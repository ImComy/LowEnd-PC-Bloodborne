from paths import ROOT  # noqa: F401 (scripts/ on the path)
import os
import tempfile
import unittest
from pathlib import Path

import language_files


def tree(root, files):
    for name, text in files.items():
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)


class LanguageFileTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.game = self.root / 'game'
        tree(self.game / 'dvdroot_ps4', {
            'msg/porbr/menu.msgbnd.dcx': 'br', 'msg/porpt/menu.msgbnd.dcx': 'pt',
            'msg/porpt/item.msgbnd.dcx': 'pt item', 'msg/engus/menu.msgbnd.dcx': 'us',
            'menu/porbr/font.gfx': 'br font', 'menu/porpt/font.gfx': 'pt font',
            'sound/sprj_c1020_eng.fsb': 'eng', 'sound/sprj_c1020_ptb.fsb': 'ptb',
            'sound/sprj_c1030_eng.fsb': 'eng only', 'sound/sprj_c1000.fsb': 'effects',
        })
        self.out = self.root / 'out' / 'language'

    def layer(self):
        return {p.relative_to(self.out / 'dvdroot_ps4').as_posix(): p.read_text()
                for p in sorted(self.out.rglob('*')) if p.is_file()}

    def test_brazilian_portuguese_text_where_the_game_reads(self):
        self.assertEqual(language_files.build(self.game, self.out, '17', 'auto'), 3)
        self.assertEqual(self.layer(), {'menu/porpt/font.gfx': 'br font', 'msg/engus/menu.msgbnd.dcx': 'br',
                                        'msg/porpt/menu.msgbnd.dcx': 'br'})

    def test_voice_apart_from_the_text(self):
        self.assertEqual(language_files.build(self.game, self.out, '1', 'ptb'), 1)
        self.assertEqual(self.layer(), {'sound/sprj_c1020_eng.fsb': 'ptb'})

    def test_nothing_chosen_leaves_no_layer(self):
        tree(self.out, {'dvdroot_ps4/old.txt': 'stale'})
        self.assertEqual(language_files.build(self.game, self.out, '1', 'auto'), 0)
        self.assertFalse(self.out.exists())

    def test_links_not_copies_on_the_same_drive(self):
        language_files.build(self.game, self.out, '1', 'ptb')
        linked = self.out / 'dvdroot_ps4/sound/sprj_c1020_eng.fsb'
        self.assertTrue(os.path.samefile(linked, self.game / 'dvdroot_ps4/sound/sprj_c1020_ptb.fsb'))


if __name__ == '__main__':
    unittest.main()
