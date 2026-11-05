#!/usr/bin/env python3
"""Stage a complete MkDocs site and exchange it with the live Linux directory."""

import ctypes
import os
from pathlib import Path
import shutil
import sys
import tempfile


def exchange(first, second):
    renameat2 = ctypes.CDLL(None, use_errno=True).renameat2
    renameat2.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
    renameat2.restype = ctypes.c_int
    at_fdcwd, rename_exchange = -100, 2
    if renameat2(at_fdcwd, os.fsencode(first), at_fdcwd, os.fsencode(second), rename_exchange):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(second))


def publish(source, live):
    if not (source / "index.html").is_file():
        raise ValueError(f"No index.html in the built site at {source}")
    if live.is_symlink() or (live.exists() and not live.is_dir()):
        raise ValueError(f"The web root must be a directory at {live}")
    # Siblings guarantee the same filesystem. The runner must be able to write to the parent.
    with tempfile.TemporaryDirectory(prefix=f".{live.name}-", dir=live.parent) as staging:
        replacement = Path(staging)
        shutil.copytree(source, replacement, dirs_exist_ok=True)
        if live.exists():
            exchange(replacement, live)
        else:
            replacement.rename(live)


if __name__ == "__main__":
    publish(Path(sys.argv[1]).absolute(), Path(sys.argv[2]).absolute())
