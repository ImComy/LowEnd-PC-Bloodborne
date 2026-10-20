from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'launcher'))
import bbport_pkg_setup as setup


class PackageSetupTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[1] / 'out' / 'test-tmp'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.extractor = self.root / 'pkg_extractor.exe'
        self.extractor.write_bytes(b'test double')

    def test_zip_install_applies_plain_update_without_replacing_an_existing_game(self):
        archive = self.root / 'cusa-03173.zip'
        with zipfile.ZipFile(archive, 'w') as out:
            out.writestr('cusa-03173/CUSA03173.pkg', b'base')
            out.writestr('cusa-03173/CUSA03173 Update v1.09.pkg', b'patch')
            out.writestr('cusa-03173/CUSA03173 Update v1.09 720p 60fps.pkg', b'wrong')

        inspected = []
        def check(_, package, kind):
            inspected.append((Path(package).read_bytes(), kind))

        def extract(_, package, output, folder, status):
            folder.mkdir()
            (folder / 'eboot.bin').write_bytes(Path(package).read_bytes())
            (folder / 'sce_sys').mkdir()
            (folder / 'sce_sys' / 'param.sfo').write_bytes(b'1.09')

        with patch.object(setup, '_check_type', side_effect=check), patch.object(setup, '_extract', side_effect=extract):
            game = setup.install_game(archive, self.root, self.extractor)
        self.assertEqual(game, self.root / 'CUSA03173')
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'patch')
        self.assertEqual(inspected, [(b'base', 101), (b'patch', 102)])
        self.assertFalse((self.root / 'CUSA03173-patch').exists())
        self.assertFalse(list(self.root.glob('.bbport-install-*')))

        with self.assertRaisesRegex(setup.PackageSetupError, 'already exists'):
            setup.install_game(archive, self.root, self.extractor)
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'patch')

    def test_missing_plain_update_is_rejected(self):
        archive = self.root / 'wrong.zip'
        with zipfile.ZipFile(archive, 'w') as out:
            out.writestr('CUSA03173.pkg', b'base')
            out.writestr('CUSA03173 Update v1.09 1080p 60fps.pkg', b'wrong')
        with self.assertRaisesRegex(setup.PackageSetupError, 'plain'):
            setup.install_game(archive, self.root, self.extractor)
        self.assertFalse((self.root / 'CUSA03173').exists())

    def test_direct_pkg_requires_plain_update(self):
        base = self.root / 'CUSA03173.pkg'
        other = self.root / 'CUSA03173 Update v1.09 720p 60fps.pkg'
        base.write_bytes(b'base')
        other.write_bytes(b'wrong')
        with self.assertRaisesRegex(setup.PackageSetupError, '720p or 1080p'):
            setup.install_game(base, self.root, self.extractor, other)

    def _install(self, base, update=None, kinds=None):
        """install_game with a fake extractor; kinds maps PKG contents to their --check-type code."""
        kinds = kinds or {b'base': 101, b'patch': 102}

        def check(_, package, expected):
            if kinds.get(Path(package).read_bytes()) != expected:
                raise setup.PackageSetupError(f'{Path(package).name} is not a valid 1.09 update PKG.')

        def extract(_, package, output, folder, status):
            folder.mkdir()
            (folder / 'eboot.bin').write_bytes(Path(package).read_bytes())

        with patch.object(setup, '_check_type', side_effect=check), patch.object(setup, '_extract', side_effect=extract):
            return setup.install_game(base, self.root, self.extractor, update)

    def test_direct_pkg_accepts_a_renamed_plain_update(self):
        base = self.root / 'CUSA03173.pkg'
        update = self.root / 'UP9000-CUSA03173_00-BLOODBORNE0000NA-A0109-V0100 (1).pkg'
        base.write_bytes(b'base')
        update.write_bytes(b'patch')
        game = self._install(base, update)
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'patch')

    def test_direct_pkgs_chosen_the_other_way_round(self):
        first = self.root / 'CUSA03173 Update v1.09.pkg'
        second = self.root / 'CUSA03173.pkg'
        first.write_bytes(b'patch')
        second.write_bytes(b'base')
        game = self._install(first, second)
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'patch')

    def test_direct_pkg_that_is_not_an_update_is_rejected(self):
        base = self.root / 'CUSA03173.pkg'
        other = self.root / 'something.pkg'
        base.write_bytes(b'base')
        other.write_bytes(b'junk')
        with self.assertRaisesRegex(setup.PackageSetupError, 'not a valid 1.09 update'):
            self._install(base, other)
        self.assertFalse((self.root / 'CUSA03173').exists())

    def test_single_pkg_already_109_installs_alone(self):
        # Game of the Year PKGs with the update merged in (#43): chosen in both steps, or alone.
        base = self.root / 'Bloodborne GOTY v1.09.pkg'
        base.write_bytes(b'base')
        with patch.object(setup, '_game_version', return_value='01.09'):
            game = self._install(base, base)
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'base')
        game.rename(self.root / 'first')
        with patch.object(setup, '_game_version', return_value='01.09'):
            self.assertEqual(self._install(base), self.root / 'CUSA03173')

    def test_single_pkg_older_than_109_asks_for_the_update(self):
        base = self.root / 'CUSA03173.pkg'
        base.write_bytes(b'base')
        with patch.object(setup, '_game_version', return_value='01.00'), \
                self.assertRaisesRegex(setup.PackageSetupError, 'version 01.00, not 1.09'):
            self._install(base, base)
        self.assertFalse((self.root / 'CUSA03173').exists())
        self.assertFalse(list(self.root.glob('.bbport-install-*')))

    def test_single_update_pkg_says_to_choose_the_base_first(self):
        update = self.root / 'CUSA03173 Update v1.09.pkg'
        update.write_bytes(b'patch')
        with self.assertRaisesRegex(setup.PackageSetupError, 'is the 1.09 update'):
            self._install(update, update)

    def test_zip_with_only_a_109_base(self):
        archive = self.root / 'goty.zip'
        with zipfile.ZipFile(archive, 'w') as out:
            out.writestr('CUSA03173.pkg', b'base')
        with patch.object(setup, '_game_version', return_value='01.09'):
            game = self._install(archive)
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'base')

    def test_wrong_files_name_what_was_chosen(self):
        base = self.root / 'CUSA03173.pkg'
        other = self.root / 'notes.pkg'
        base.write_bytes(b'junk')
        other.write_bytes(b'patch')
        with self.assertRaisesRegex(setup.PackageSetupError, 'You chose CUSA03173.pkg as the base game and notes.pkg'):
            self._install(base, other)

    def test_zip_with_a_renamed_plain_update(self):
        archive = self.root / 'dump.zip'
        with zipfile.ZipFile(archive, 'w') as out:
            out.writestr('CUSA03173.pkg', b'base')
            out.writestr('CUSA03173 Update v1.09 (1).pkg', b'patch')
            out.writestr('CUSA03173 Update v1.09 720p 60fps.pkg', b'wrong')
        game = self._install(archive)
        self.assertEqual((game / 'eboot.bin').read_bytes(), b'patch')


if __name__ == '__main__':
    unittest.main()
