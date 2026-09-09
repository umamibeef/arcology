import os
import sys

sys.path.insert(0, os.path.abspath("_ext"))

"""Sphinx build for the Arcology documents.

The .rst sources are generated from the hand-written HTML in ../report by
tools/html2rst.py.  Re-run that after changing a source page; edit the
.rst directly once the HTML is retired.
"""
project = "Arcology"
author = "Recovered from the retail Macintosh binary, 1.2 (22 June 1995)"
copyright = "SimCity 2000 is a trademark of its rights holders; this is an independent study"
release = ""

extensions = ["sphinx.ext.githubpages", "sphinx_design",
              "sphinxcontrib.images",
              "m68k"]   # a Pygments lexer for the 68000 listings

#  the terrain previews are whole maps at 4224x2468.  They are shown
#  scaled to the column; clicking one opens it full size in a lightbox
#  rather than navigating away from the page.
images_config = {
    "override_image_directive": False,
    "default_image_width": "100%",
    "default_group": "previews",
    "show_caption": True,
}
templates_path = ["_templates"]
exclude_patterns = ["_build", ".venv", "Thumbs.db", ".DS_Store"]

#  the pages carry inline SVG figures through `raw:: html`
html_theme = "furo"
html_title = "SimCity 2000, Reconstructed"
html_static_path = ["_static"]
html_css_files = ["custom.css"]
html_copy_source = False
html_show_sphinx = False

#  ``$21EDE`` and friends are addresses, not references
#  Sphinx numbers the figures, so captions do not carry their own
numfig = True
numfig_format = {"figure": "Figure %s"}

default_role = "literal"
rst_prolog = """
.. role:: raw-html(raw)
   :format: html
"""

html_theme_options = {
    "light_css_variables": {
        "color-brand-primary": "#236C7C",
        "color-brand-content": "#236C7C",
        "font-stack": "Helvetica Neue, Helvetica, Arial, sans-serif",
        "font-stack--headings": "Helvetica Neue, Helvetica, Arial, sans-serif",
        #  code still needs a monospace or nothing lines up
        "font-stack--monospace": "Menlo, Consolas, 'DejaVu Sans Mono', monospace",
    },
    "dark_css_variables": {
        "color-brand-primary": "#5AB2C4",
        "color-brand-content": "#5AB2C4",
    },
    "source_repository": "",
    "footer_icons": [],
}
pygments_style = "friendly"
pygments_dark_style = "native"
