"""Small, non-destructive checks for the launcher's Game page."""

import json
from pathlib import Path
import re
import tempfile


def valid_trophy_key(value):
    return isinstance(value, str) and re.fullmatch(r'[0-9a-fA-F]{32}', value) is not None


def _item(label, level, detail):
    return {'label': label, 'level': level, 'detail': detail}


def save_title_id(game):
    """Use the save title declared by the game, which can differ from TITLE_ID."""
    if game:
        try:
            from prepare import sfo
            values = sfo((game / 'sce_sys' / 'param.sfo').read_bytes())
            title = values.get('INSTALL_DIR_SAVEDATA') or values.get('TITLE_ID')
            if isinstance(title, str) and re.fullmatch(r'CUSA[0-9]{5}', title):
                return title
        except (OSError, ValueError, ImportError):
            pass
    return game.name if game and re.fullmatch(r'CUSA[0-9]{5}', game.name) else 'CUSA03173'


def check_readiness(game_dir, user_dir, game_info):
    """Return Game, Trophies and Saves checks without touching game or save data."""
    game = Path(game_dir) if game_dir else None
    user = Path(user_dir)
    result = []

    info = game_info(str(game)) if game and (game / 'eboot.bin').is_file() else None
    if not info:
        result.append(_item('Game files', 'error', 'Choose the extracted CUSA03173 folder containing eboot.bin.'))
    else:
        missing = [name for name in ('dvdroot_ps4', 'sce_module', 'sce_sys') if not (game / name).is_dir()]
        if missing:
            result.append(_item('Game files', 'error', 'Missing: ' + ', '.join(missing)))
        elif info[1] != '01.09':
            result.append(_item('Game files', 'warning', f'Version {info[1]} found; update 1.09 is recommended.'))
        else:
            result.append(_item('Game files', 'ready', 'Bloodborne 1.09 and required folders found.'))

    pack = next((game / 'sce_sys' / 'trophy' / name for name in ('trophy00.trp', 'TROPHY.TRP')
                 if game and (game / 'sce_sys' / 'trophy' / name).is_file()), None)
    if pack is None:
        result.append(_item('Trophies', 'warning', 'No trophy pack found; achievements can show IDs only.'))
    else:
        try:
            with pack.open('rb') as source:
                valid_pack = source.read(4) == bytes.fromhex('DCA24D00')
        except OSError:
            valid_pack = False
        if not valid_pack:
            result.append(_item('Trophies', 'error', 'Trophy pack cannot be read or has an invalid header.'))
        else:
            key_file = user / 'keys.json'
            try:
                data = json.loads(key_file.read_text(encoding='utf-8-sig'))
                has_key = valid_trophy_key(data['TrophyKeySet']['ReleaseTrophyKey'])
            except (OSError, ValueError, KeyError, TypeError):
                has_key = False
            cache_file = user / 'trophy_cache' / 'metadata.json'
            try:
                cached = json.loads(cache_file.read_text(encoding='utf-8'))
                cached_complete = (cached.get('complete') is True and bool(cached.get('entries'))
                                   and cached.get('source_size') == pack.stat().st_size)
            except (OSError, ValueError, TypeError):
                cached_complete = False
            if cached_complete:
                detail = 'Trophy names have been loaded by the game.'
                level = 'ready'
            elif has_key:
                detail = 'Pack and key format found; launch the game to verify decryption.'
                level = 'warning'
            else:
                detail = 'Pack found; import a valid keys.json to show trophy names.'
                level = 'warning'
            history = user / 'trophies.txt'
            if history.is_file():
                try:
                    unlocked = {int(parts[0]) for line in history.read_text(encoding='utf-8').splitlines()
                                if len(parts := line.split()) == 2 and parts[0].isdigit()
                                and parts[1].isdigit() and 0 <= int(parts[0]) < 128}
                    detail += f' {len(unlocked)} local unlocks recorded.'
                except OSError:
                    detail += ' Unlock history cannot be read.'
            result.append(_item('Trophies', level, detail))

    if not user.exists():
        result.append(_item('Saves', 'warning', 'Save folder will be created on first launch.'))
    elif not user.is_dir():
        result.append(_item('Saves', 'error', 'Save path is a file, not a folder.'))
    else:
        try:
            with tempfile.NamedTemporaryFile(prefix='.bbport-check-', dir=user, delete=True):
                pass
        except OSError:
            result.append(_item('Saves', 'error', 'Save folder is not writable.'))
        else:
            save_root = user / 'savedata'
            save_id = save_title_id(game)
            saves = list(save_root.glob(f'*/{save_id}/SPRJ*/userdata*')) if save_root.is_dir() else []
            try:
                found_save = any(path.is_file() and path.stat().st_size > 0 for path in saves)
            except OSError:
                found_save = False
            # shadPS4's own savedata folder (…\home\1000\savedata) holds <title>\SPRJ* directly;
            # chosen as the Saves folder (or its parent, home\1000), the game misses that save and
            # starts a new, empty one. Only looked for when no save is where the port reads them.
            shadps4_save = False
            if not found_save:
                try:
                    shadps4_save = any(path.is_file() and path.stat().st_size > 0
                                       for pattern in (f'{save_id}/SPRJ*/userdata*',
                                                       f'savedata/{save_id}/SPRJ*/userdata*')
                                       for path in user.glob(pattern))
                except OSError:
                    pass
            if found_save:
                result.append(_item('Saves', 'ready', f'Save folder is writable; {save_id} save data found.'))
            elif shadps4_save:
                result.append(_item('Saves', 'warning',
                                    f'This looks like shadPS4\'s savedata folder: its {save_id} save is not '
                                    f'where this port reads saves, so the game would start a new one. Choose '
                                    f'another Saves folder and copy {save_id} into its savedata\\1 folder.'))
            else:
                result.append(_item('Saves', 'warning', f'Save folder is writable; no {save_id} save yet.'))
    return result
