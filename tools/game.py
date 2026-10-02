#!/usr/bin/env python3
"""THE GAME FOLDER, and the one name it is read by.

Nothing in this repository can stand in for the game's own files, so a
tool that needs them reads `ARC_CITIES`: the same name the build takes
(`cmake -B build -DARC_CITIES="/path/to/SimCity 2000 Collection"`) and
the same name the program reads.  ONE name for one thing.

Where it is unset, one GUESS is made, and the guess says so.  The guess
lives here and nowhere else: the same path typed into five tools is five
places to change and five different answers when one of them is missed.

    from game import folder
    GAME = folder()
"""
import os
import sys
from pathlib import Path

NAME = "ARC_CITIES"

#  Where the collection often lands when it is unpacked.  A guess, and
#  said out loud as one.
GUESS = Path.home() / "Downloads" / "SimCity 2000® Collection"

_said = False


def folder(what=None):
    """The game folder as a Path.  Stops with a message that names the
    setting where there is nothing to read and nothing to guess."""
    global _said
    at = os.environ.get(NAME)
    if at:
        p = Path(at).expanduser()
        if not p.is_dir():
            raise SystemExit("%s: %s names %s, which is not a folder"
                             % (Path(sys.argv[0]).name, NAME, p))
        return p
    if GUESS.is_dir():
        if not _said:
            _said = True
            print("%s: %s is unset, so the game folder is a guess: %s"
                  % (Path(sys.argv[0]).name, NAME, GUESS), file=sys.stderr)
        return GUESS
    raise SystemExit(
        "%s: this needs the game's own files%s, and nothing here can stand in\n"
        "for them.  Set %s to your copy of the collection:\n"
        "    export %s=\"$HOME/path/to/SimCity 2000 Collection\""
        % (Path(sys.argv[0]).name, " (%s)" % what if what else "", NAME, NAME))
