#!/usr/bin/env python3
"""Move the inline SVG figures out of `raw:: html` and into real files.

Inline SVG had two costs.  Its caption had to be a <figcaption> written
in HTML, so it was invisible to Sphinx -- not searchable, not styled by
the theme, and it had to be escaped by hand.  And an SVG that reads
`var(--surface)` only resolves those when it is inlined.

Each figure becomes `img/fig-<page>-<n>.svg` carrying its own stylesheet,
including a `prefers-color-scheme` block, so it themes itself as a
standalone file.  The document then uses a normal `.. figure::` and a
normal reStructuredText caption.
"""
import os
import re
import sys

LIGHT = {
    "surface": "#FBFBF9", "ground": "#F2F3F0", "sunk": "#E9EBE6",
    "ink": "#15191A", "body": "#2C3335", "muted": "#5D6663", "faint": "#8C9490",
    "rule": "#D9DCD6", "rule-soft": "#E6E8E3",
    "gold": "#9A6712", "gold-bg": "#F0E4CC",
    "water": "#236C7C", "water-bg": "#D7E6EA",
    "moss": "#5C7530", "moss-bg": "#E2EAD3",
    "rust": "#9C462C", "rust-bg": "#F4DED6",
    "accent": "#9A6712", "good": "#5C7530",
}
DARK = {
    "surface": "#171B1C", "ground": "#101314", "sunk": "#1E2324",
    "ink": "#E7EBE8", "body": "#C6CDC9", "muted": "#8B9491", "faint": "#6B7472",
    "rule": "#272E2F", "rule-soft": "#202626",
    "gold": "#DDA43E", "gold-bg": "#2A2318",
    "water": "#5AB2C4", "water-bg": "#16262A",
    "moss": "#9AB962", "moss-bg": "#1B2417",
    "rust": "#D07C5E", "rust-bg": "#2B1C16",
    "accent": "#DDA43E", "good": "#9AB962",
}


def stylesheet(used):
    def block(pal):
        return "".join("--%s:%s;" % (k, pal[k]) for k in sorted(used) if k in pal)
    return ("<style>:root{%s color-scheme:light dark}"
            "@media(prefers-color-scheme:dark){:root{%s}}"
            "text{font-family:Helvetica Neue,Helvetica,Arial,sans-serif}"
            "</style>" % (block(LIGHT), block(DARK)))


def main(docdir):
    imgdir = os.path.join(docdir, "img")
    total = 0
    for path in sorted(f for f in os.listdir(docdir) if f.endswith(".rst")):
        full = os.path.join(docdir, path)
        s = open(full, encoding="utf8").read()
        stem, k = path[:-4], 0
        out, pos = [], 0
        #  a raw:: html block that holds one <figure class="fig">
        pat = re.compile(
            r'( *)\.\. raw:: html\n\n((?:\1   .*\n|\n)*?)(?=\n*(?:\S|\Z))', re.M)
        for m in pat.finditer(s):
            pad, body = m.group(1), m.group(2)
            if "<svg" not in body:
                continue
            raw = "\n".join(l[len(pad) + 3:] if l.startswith(pad + "   ") else l
                            for l in body.split("\n"))
            svg = re.search(r'<svg .*?</svg>', raw, re.S)
            if not svg:
                continue
            k += 1
            cap = re.search(r'<figcaption>(.*?)</figcaption>', raw, re.S)
            captxt = re.sub(r'\s+', ' ', re.sub(r'<[^>]+>', '', cap.group(1))).strip() if cap else ""
            alt = re.search(r'aria-label="([^"]*)"', svg.group(0))
            body_svg = svg.group(0)
            used = set(re.findall(r'var\(--([a-z-]+)\)', body_svg))
            i = body_svg.index('>') + 1
            body_svg = (body_svg[:i] + stylesheet(used) + body_svg[i:])
            body_svg = body_svg.replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" ', 1)
            name = "fig-%s-%d.svg" % (stem, k)
            open(os.path.join(imgdir, name), "w", encoding="utf8").write(
                '<?xml version="1.0" encoding="UTF-8"?>\n' + body_svg + "\n")
            rep = ["%s.. figure:: img/%s" % (pad, name)]
            if alt:
                rep.append("%s   :alt: %s" % (pad, alt.group(1)))
            rep.append("")
            if captxt:
                rep.append("%s   %s" % (pad, captxt))
                rep.append("")
            out.append(s[pos:m.start()])
            out.append("\n".join(rep) + "\n")
            pos = m.end()
            total += 1
        out.append(s[pos:])
        if k:
            open(full, "w", encoding="utf8").write("".join(out))
            print("  %-24s %d figures extracted" % (path, k))
    print("total:", total)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "docs")
