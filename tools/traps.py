"""Mac OS A-line trap decoding, backed by the Universal Interfaces trap table."""
import os, re
_HERE = os.path.dirname(os.path.abspath(__file__))
TRAPS = {}
for line in open(os.path.join(_HERE, "universal_header_traps.txt")):
    m = re.match(r"0x([0-9A-Fa-f]{4})\s+(\S+)", line)
    if m:
        w = int(m.group(1), 16)
        TRAPS.setdefault(w, m.group(2))   # keep first (canonical) name

def trap_name(word):
    if word in TRAPS:
        return TRAPS[word]
    if word & 0x0800:                       # Toolbox trap
        base = word & 0xF7FF if False else (word & ~0x0400)
        nm = TRAPS.get(base)
        if nm: return nm + (" {autoPop}" if word & 0x0400 else "")
        return "ToolBox_%03X" % (word & 0x03FF)
    # OS trap: bits 8..10 are flag bits (clear / sys heap / don't-save-A0)
    base = word & ~0x0600
    nm = TRAPS.get(base)
    fl = []
    if word & 0x0200: fl.append("sys")
    if word & 0x0400: fl.append("clr")
    if nm: return nm + (" {%s}" % ",".join(fl) if fl else "")
    return "OSTrap_%02X" % (word & 0xFF)
