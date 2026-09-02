"""The shape table out of the game's TSET resource.

The simulation reads it too, not just the renderer: $C2DA looks up the
height of the building under an aeroplane and crashes the aeroplane into
anything taller than it is flying.  So the descriptor table at
A5+0x1226 has to be built before the aeroplane can be trusted under the
oracle -- left NULL, $C2DA reads whatever sits at address 0.
"""
import os
import struct

from rezfork import load as rez_load

GAME = os.path.expanduser("~/Downloads/SimCity 2000® Collection")
_CACHE = None


def shapes():
    """id -> (width, height, raw span stream), straight out of TSET."""
    global _CACHE
    if _CACHE is None:
        rs = rez_load(os.path.join(GAME, "SimCity 2000® 1.2/..namedfork/rsrc"))
        data = [e for e in rs["TSET"]][0].data
        out, off = {}, 12
        while off + 8 <= len(data):
            tag = data[off:off + 4]
            n = struct.unpack(">I", data[off + 4:off + 8])[0]
            if tag == b"SHAP" and n >= 10:
                sid, w, h, _f, _dl = struct.unpack(">HHHHH",
                                                   data[off + 8:off + 18])
                out[sid] = (w, h, data[off + 18:off + 8 + n])
            off += 8 + n + (n & 1)
        _CACHE = out
    return _CACHE


def heights():
    """id -> sprite height in pixels, for ids 0..255."""
    sh = shapes()
    return [sh[i][1] if i in sh else 0 for i in range(256)]
