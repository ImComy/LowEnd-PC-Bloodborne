# SPDX-License-Identifier: GPL-2.0-or-later
"""Output that survives its reader going away.

The launcher reads run.py's output (and the preparation scripts', which inherit it) through a
pipe. When the launcher closes while the game is still being prepared ("close the launcher
when the game starts"), that pipe has no reader any more and every print() fails: Windows
reports OSError [Errno 22] Invalid argument, other systems BrokenPipeError. A failed message
must not stop the game from starting, so the streams fall back to discarding output.
"""
import os
import sys


class QuietStream:
    """Wraps sys.stdout or sys.stderr: the first failed write or flush switches it to os.devnull."""

    def __init__(self, stream):
        self._stream = stream
        self._closed_reader = False

    def _fall_back(self):
        self._closed_reader = True
        try:
            self._stream = open(os.devnull, 'w', encoding='utf-8')
        except OSError:
            self._stream = None

    def write(self, text):
        if self._stream is None:
            return len(text)
        try:
            return self._stream.write(text)
        except (OSError, ValueError):
            self._fall_back()
            return len(text)

    def flush(self):
        if self._stream is None:
            return
        try:
            self._stream.flush()
        except (OSError, ValueError):
            self._fall_back()

    def __getattr__(self, name):
        return getattr(self._stream, name)


def guard():
    """Wraps the process's stdout and stderr once."""
    for name in ('stdout', 'stderr'):
        stream = getattr(sys, name)
        if stream is not None and not isinstance(stream, QuietStream):
            setattr(sys, name, QuietStream(stream))
