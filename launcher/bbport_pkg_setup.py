"""Install the user's CUSA03173 base and 1.09 update with the bundled PKG extractor.
A base PKG that already holds 1.09 (Game of the Year PKGs with the update merged) installs alone.

The extractor is ShadPs4Plus PKG Extractor 1.1 (GPL-2.0). Game data is staged beside
the chosen destination and moved into place only after both PKGs have been checked.
"""

from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile


TITLE_ID = 'CUSA03173'
BASE_NAME = 'CUSA03173.pkg'
UPDATE_NAME = 'CUSA03173 Update v1.09.pkg'
GIB = 1024 ** 3


class PackageSetupError(RuntimeError):
    pass


def _is_variant(name):
    """The 720p/1080p 60fps update PKGs shipped beside the plain one in common dumps."""
    name = name.casefold().replace(' ', '')
    return any(word in name for word in ('720p', '1080p', '60fps'))


def _package_members(archive):
    matches = {}
    others = []
    for member in archive.infolist():
        if member.is_dir():
            continue
        name = Path(member.filename).name.casefold()
        if name in (BASE_NAME.casefold(), UPDATE_NAME.casefold()):
            if name in matches:
                raise PackageSetupError(f'The ZIP has more than one {name}.')
            matches[name] = member
        elif name.endswith('.pkg') and not _is_variant(name):
            others.append(member)
    # A renamed plain update (a browser's "(1)", a store-style name) is fine when it is the only
    # other PKG; the extractor confirms it is an update before anything is extracted.
    if UPDATE_NAME.casefold() not in matches and len(others) == 1:
        matches[UPDATE_NAME.casefold()] = others[0]
    variants = any(not member.is_dir() and member.filename.casefold().endswith('.pkg')
                   and _is_variant(Path(member.filename).name) for member in archive.infolist())
    # Only a base PKG: it may already be 1.09, which is checked once it is extracted.
    if BASE_NAME.casefold() not in matches or (UPDATE_NAME.casefold() not in matches and variants):
        raise PackageSetupError('The ZIP must contain CUSA03173.pkg and the plain 1.09 update PKG '
                                '(not the 720p or 1080p 60fps one).')
    return matches[BASE_NAME.casefold()], matches.get(UPDATE_NAME.casefold())


def _game_version(game):
    """APP_VER of an extracted game; "01.09" for a Game of the Year eboot.bin with the 1.09 code."""
    try:
        from patches import game_app_version
    except ImportError:
        return None
    return game_app_version(game)


def _check_type(extractor, package, expected):
    try:
        result = subprocess.run([str(extractor), str(package), '--check-type'],
                                capture_output=True, text=True, errors='replace', timeout=90,
                                creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except (OSError, subprocess.TimeoutExpired) as error:
        raise PackageSetupError(f'Could not inspect {package.name}: {error}') from error
    if result.returncode != expected:
        kind = 'base game' if expected == 101 else '1.09 update'
        raise PackageSetupError(f'{package.name} is not a valid {kind} PKG. Check that you selected the right file.')


def _is_type(extractor, package, expected):
    try:
        _check_type(extractor, package, expected)
        return True
    except PackageSetupError:
        return False


def _extract(extractor, package, output, expected_folder, status):
    try:
        process = subprocess.Popen([str(extractor), str(package), str(output)],
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, errors='replace', bufsize=1,
                                   creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
    except OSError as error:
        raise PackageSetupError(f'Could not start the PKG extractor: {error}') from error
    reported = -1
    output_tail = []
    for line in process.stdout:
        output_tail.append(line.strip())
        output_tail = output_tail[-8:]
        progress = re.search(r'Extracting file (\d+) of (\d+)', line)
        if progress:
            number, total = map(int, progress.groups())
            percent = number * 100 // max(1, total)
            if percent // 10 > reported:
                reported = percent // 10
                status(f'Extracting {expected_folder.name}: {percent}%')
    result = process.wait()
    if result != 0 or not (expected_folder / 'eboot.bin').is_file():
        detail = ' '.join(output_tail[-3:])
        raise PackageSetupError(f'Could not extract {package.name}. {detail}'.strip())


def install_game(source, destination_parent, extractor, update=None, status=lambda message: None):
    """Return the completed game folder. Never overwrite an existing installation."""
    source = Path(source).resolve()
    destination_parent = Path(destination_parent).resolve()
    extractor = Path(extractor).resolve()
    if not source.is_file():
        raise PackageSetupError('Choose an existing game ZIP or base PKG.')
    if not destination_parent.is_dir():
        raise PackageSetupError('Choose an existing destination folder.')
    if not extractor.is_file():
        raise PackageSetupError('The PKG extractor is missing from the launcher package.')
    target = destination_parent / TITLE_ID
    if target.exists():
        raise PackageSetupError(f'{target} already exists. Choose another destination so your game files stay safe.')
    if source.suffix.casefold() not in ('.zip', '.pkg'):
        raise PackageSetupError('Choose cusa-03173.zip or CUSA03173.pkg.')
    update_path = Path(update).resolve() if update else None
    # The same PKG in both steps, or no update: a base PKG that should already be 1.09 (#43).
    if update_path == source:
        update_path = None
    if update_path and not update_path.is_file():
        raise PackageSetupError('Choose the plain 1.09 update PKG too.')
    # Only the name can tell the 60fps variants from the plain update (both are 1.09 update PKGs);
    # any other name is accepted, and the extractor checks the type.
    for chosen in (source, update_path) if source.suffix.casefold() == '.pkg' else ():
        if chosen and _is_variant(chosen.name):
            raise PackageSetupError(f'{chosen.name} looks like the 720p or 1080p 60fps update. '
                                    'Choose the plain 1.09 update PKG instead.')

    archive = None
    try:
        if source.suffix.casefold() == '.zip':
            archive = zipfile.ZipFile(source)
            base_member, update_member = _package_members(archive)
            package_bytes = base_member.file_size + (update_member.file_size if update_member else 0)
        else:
            package_bytes = source.stat().st_size + (update_path.stat().st_size if update_path else 0)
        # A ZIP install temporarily holds the PKGs and extracted data on the same drive.
        needed = int(package_bytes * (2.2 if archive else 1.2)) + 5 * GIB
        free = shutil.disk_usage(destination_parent).free
        if free < needed:
            raise PackageSetupError(f'Not enough free space here. Allow about {needed // GIB + 1} GB for setup.')
        with tempfile.TemporaryDirectory(prefix='.bbport-install-', dir=destination_parent) as temporary:
            staging = Path(temporary)
            if archive:
                status('Unpacking the base PKG from the ZIP…')
                base = staging / BASE_NAME
                with archive.open(base_member) as src, base.open('wb') as dst:
                    shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
                patch = None
                if update_member:
                    status('Unpacking the 1.09 update from the ZIP…')
                    patch = staging / UPDATE_NAME
                    with archive.open(update_member) as src, patch.open('wb') as dst:
                        shutil.copyfileobj(src, dst, 8 * 1024 * 1024)
            else:
                base, patch = source, update_path
            status('Checking the base game and 1.09 update…' if patch else 'Checking the game PKG…')
            try:
                _check_type(extractor, base, 101)
            except PackageSetupError:
                if not patch:
                    if _is_type(extractor, base, 102):
                        raise PackageSetupError(f'{base.name} is the 1.09 update. Choose the base game PKG '
                                                '(the large CUSA03173.pkg) first, then this update.') from None
                    raise
                # The two PKGs chosen the other way round: swap them.
                if archive:
                    raise
                if not (_is_type(extractor, base, 102) and _is_type(extractor, patch, 101)):
                    raise PackageSetupError(f'{base.name} is not a valid base game PKG. You chose {base.name} '
                                            f'as the base game and {patch.name} as the update; the base game '
                                            'is the large CUSA03173.pkg.') from None
                base, patch = patch, base
            try:
                if patch:
                    _check_type(extractor, patch, 102)
            except PackageSetupError as error:
                if archive:
                    raise
                raise PackageSetupError(f'{error} You chose {source.name} as the base game and '
                                        f'{update_path.name} as the update.') from error
            extracted = staging / 'extracted'
            extracted.mkdir()
            game = extracted / TITLE_ID
            status('Extracting the base game. This can take a while…')
            _extract(extractor, base, extracted, game, status)
            if patch:
                patch_folder = extracted / f'{TITLE_ID}-patch'
                status('Extracting the 1.09 update…')
                _extract(extractor, patch, extracted, patch_folder, status)
                status('Applying the update to the game folder…')
                shutil.copytree(patch_folder, game, dirs_exist_ok=True)
            else:
                version = _game_version(game)
                if version != '01.09':
                    raise PackageSetupError(f'{base.name} holds game version {version or "(unknown)"}, not 1.09. '
                                            'Choose the plain 1.09 update PKG as well.')
            if not (game / 'eboot.bin').is_file():
                raise PackageSetupError('The completed game folder is missing eboot.bin.')
            if target.exists():
                raise PackageSetupError(f'{target} appeared during setup. No files were replaced.')
            game.rename(target)
            status('Game files installed. Ready to play.')
            return target
    except (OSError, EOFError, zipfile.BadZipFile) as error:
        raise PackageSetupError(f'Game installation failed: {error}') from error
    finally:
        if archive:
            archive.close()
