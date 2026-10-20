"""The launcher's FSR 4 v07 download list covers every tier the provider can ask for."""
import importlib.util
import re
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('fsr4_launcher', ROOT / 'launcher/bbport_launcher_win.py')
launcher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(launcher)

MODELS = ('native', 'quality', 'balanced', 'performance', 'ultraperf', 'drs')


def provider_tier(width, height):
    """ffxFsr4V07SelectAssetTier of gpu/third_party/fsr-vulkan (src/ffx_vk_fsr4_v07_assets.c)."""
    if width > 3840 or height > 2160:
        return '4320'
    if width > 1920 or height > 1080:
        return '2160'
    return '1080'


def tier_files(model, tier):
    stem = f'fsr4_model_v07_i8_{model}_{tier}'
    return [f'{stem}_pre.spv', f'{stem}_post.spv'] + [f'{stem}_pass{n}.spv' for n in range(1, 13)]


class Fsr4AssetsTests(unittest.TestCase):
    def test_every_output_tier_is_downloaded(self):
        files = set(launcher.fsr4_files())
        for output in ('1280x720', '1920x1080', '2560x1080', '2560x1440', '3440x1440', '3840x2160',
                       '5120x1440', '5120x2160', '7680x4320'):
            width, height = map(int, output.split('x'))
            for model in MODELS:
                with self.subTest(output=output, model=model):
                    self.assertLessEqual(set(tier_files(model, provider_tier(width, height))), files)

    def test_list_has_no_duplicates(self):
        files = launcher.fsr4_files()
        self.assertEqual(len(files), len(set(files)))
        self.assertEqual(len(files), 3 + len(MODELS) * (3 + 14 * len(launcher.FSR4_TIERS)))

    def test_script_downloads_the_same_tiers(self):
        script = (ROOT / 'tools/fetch_fsr4_assets.sh').read_text(encoding='utf-8')
        tiers = re.search(r'\$\{BB_FSR4_TIERS:-([^}]*)\}', script).group(1).split()
        self.assertEqual(tuple(tiers), launcher.FSR4_TIERS)

    def test_install_without_4320_reports_only_4320_missing(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp) / 'fsr4_shaders'
            folder.mkdir()
            for name in launcher.fsr4_files():
                if '_4320_' not in name:
                    (folder / name).write_bytes(b'\0')
            saved, launcher.PORT_DIR = launcher.PORT_DIR, Path(tmp)
            try:
                missing = launcher.fsr4_missing()
            finally:
                launcher.PORT_DIR = saved
        self.assertEqual(len(missing), 14 * len(MODELS))
        self.assertTrue(all('_4320_' in name for name in missing))


if __name__ == '__main__':
    unittest.main()
