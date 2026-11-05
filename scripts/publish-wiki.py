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


def replace_contents(source, live):
    """Empty the web root and copy the site into it. Not atomic (a visitor can meet it half done), but it needs to write only inside the web
    root, which is all the runner may be able to do: it is what the deploy did before the exchange."""
    live.mkdir(parents=True, exist_ok=True)
    for entry in live.iterdir():
        if entry.is_dir() and not entry.is_symlink():
            shutil.rmtree(entry)
        else:
            entry.unlink()
    shutil.copytree(source, live, dirs_exist_ok=True)


def publish(source, live):
    if not (source / "index.html").is_file():
        raise ValueError(f"No index.html in the built site at {source}")
    if live.is_symlink() or (live.exists() and not live.is_dir()):
        raise ValueError(f"The web root must be a directory at {live}")
    # Siblings guarantee the same filesystem, so the new site is staged next to the web root and swapped in at once. That needs the runner
    # to write to the parent of the web root (/var/www) and the filesystem to support the exchange; when either is not so, the contents of
    # the web root are replaced in place instead (the deploy failed with "Permission denied" on /var/www before this).
    try:
        staging_directory = tempfile.TemporaryDirectory(prefix=f".{live.name}-", dir=live.parent)
    except OSError as error:
        print(f"Cannot stage next to {live} ({error}); replacing the contents in place.", file=sys.stderr)
        replace_contents(source, live)
        return
    with staging_directory as staging:
        replacement = Path(staging)
        shutil.copytree(source, replacement, dirs_exist_ok=True)
        if live.exists():
            try:
                exchange(replacement, live)
            except OSError as error:
                print(f"Cannot exchange the directories ({error}); replacing the contents in place.", file=sys.stderr)
                replace_contents(source, live)
        else:
            replacement.rename(live)


if __name__ == "__main__":
    publish(Path(sys.argv[1]).absolute(), Path(sys.argv[2]).absolute())
