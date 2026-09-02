# Docs

Sphinx build of the SimCity 2000 reconstruction write-ups. **The `.rst`
files here are the source** — edit them directly.

    python3 -m venv .venv
    .venv/bin/pip install sphinx furo sphinx-design
    make html          # -> _build/html/index.html

Read `conventions.rst` before editing. It is short, and it exists so the
same kind of statement always looks the same: which admonition means
what, when to use a badge, where a card's heading goes.

These pages began as hand-written HTML. That has been retired and the
originals are gone: nothing built from them, and a frozen copy of a
generated artefact is not worth carrying. `cmake --build build --target
docs` (or `sphinx-build docs docs/_build/html`) makes the current
HTML.
