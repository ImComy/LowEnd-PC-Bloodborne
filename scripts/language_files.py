"""The game's text and voice files for the chosen languages, as a layer under the mods (mods.py
--generated): files linked from the game under the names the game reads.

- Text: with Brazilian Portuguese (BB_LANGUAGE 17) the game reads the European Portuguese
  (porpt) and English (engus) folders; the layer puts the porbr files there (GitHub issue #35).
- Voice (BB_VOICE_LANGUAGE eng, ptb, ded, esa, ese, frf, iti, jaj; auto or empty: the game's
  own choice): every localized sound/*_<code>.fsb is that voice's file, so the voices follow
  the choice whatever the text language.

After Berleis, Supermedo/bloodborne_pc PR #20 (which redirected the paths in runtime_file.c);
missing translated files fall back to the game's own. BB_LANGUAGE_FILES=0: no layer.
"""
import argparse
import os
import shutil
import sys
from pathlib import Path

VOICES = ('eng', 'ptb', 'ded', 'esa', 'ese', 'frf', 'iti', 'jaj')
PTBR = '17'
PTBR_FOLDERS = {'porbr': ('porpt', 'engus')}  # the game's folder -> the folders it reads instead


def child(folder, name):
    """`folder`/`name` in the spelling the folder has (the game's files are lower case)."""
    if folder.is_dir():
        for entry in folder.iterdir():
            if entry.name.casefold() == name.casefold():
                return entry
    return folder / name


def text_links(dvdroot, language):
    """(path under dvdroot_ps4, game file) for the text language."""
    links = []
    if language != PTBR:
        return links
    for section in ('msg', 'menu'):
        for source_name, targets in PTBR_FOLDERS.items():
            source = child(child(dvdroot, section), source_name)
            if not source.is_dir():
                continue
            for target_name in targets:
                target = child(child(dvdroot, section), target_name)
                if not target.is_dir():
                    continue
                for file in sorted(source.rglob('*')):
                    relative = file.relative_to(source)
                    if file.is_file() and (target / relative).is_file():
                        links.append((Path(section) / target.name / relative, file))
    return links


def voice_links(dvdroot, voice):
    """(path under dvdroot_ps4, game file) for the voice language."""
    links = []
    sound = child(dvdroot, 'sound')
    if voice not in VOICES or not sound.is_dir():
        return links
    for file in sorted(sound.iterdir()):
        stem, _, code = file.stem.rpartition('_')
        if file.suffix.lower() != '.fsb' or code.lower() not in VOICES or code.lower() == voice:
            continue
        chosen = child(sound, f'{stem}_{voice}{file.suffix}')
        if chosen.is_file():
            links.append((Path('sound') / file.name, chosen))
    return links


def build(game, out, language, voice):
    """Makes the layer in `out` (replacing an older one); returns how many files it holds."""
    dvdroot = child(Path(game), 'dvdroot_ps4')
    links = text_links(dvdroot, language) + voice_links(dvdroot, voice)
    if out.exists():
        shutil.rmtree(out)  # plain files and hard links only: nothing it removes leads elsewhere
    copies = {}
    for relative, source in links:
        destination = out / 'dvdroot_ps4' / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        try:
            os.link(source, destination)
        except OSError:
            # Another drive: one copy per game file, linked from there for the other names.
            if source in copies:
                os.link(copies[source], destination)
            else:
                shutil.copyfile(source, destination)
                copies[source] = destination
    return len(links)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--language', default='')
    parser.add_argument('--voice', default='')
    args = parser.parse_args()
    voice = args.voice.lower()
    count = build(args.game, args.out, args.language, voice)
    if count:
        parts = (['Brazilian Portuguese text'] if args.language == PTBR else []) + \
                ([f'{voice} voices'] if voice in VOICES else [])
        print(f'Language files: {", ".join(parts)} ({count} files)', file=sys.stderr)


if __name__ == '__main__':
    main()
