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

#  The preview pages embed pictures that tools/gen_previews.py renders
#  from the game's own files, which the repository does not carry.  The
#  lightbox extension refuses a picture that is not there, so it is
#  loaded only where the manifest a render writes exists.  Without it,
#  every `thumbnail` becomes a note that names the missing picture (see
#  setup below), and the pages and every reference to them still build
#  clean on a clone without the game.
PREVIEWS_PRESENT = os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                               "img", "preview", "manifest.json"))

extensions = ["sphinx.ext.githubpages", "sphinx_design",
              "m68k"]   # a Pygments lexer for the 68000 listings
if PREVIEWS_PRESENT:
    extensions.append("sphinxcontrib.images")
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

def setup(app):
    if PREVIEWS_PRESENT:
        return
    from docutils import nodes
    from docutils.parsers.rst import Directive

    class AnyOption(dict):
        """Every option the real directive takes, accepted as text."""

        def __missing__(self, key):
            return str

    class MissingThumbnail(Directive):
        required_arguments = 1
        final_argument_whitespace = True
        has_content = True
        option_spec = AnyOption()

        def run(self):
            text = ("Picture %s is rendered from the game's own files by "
                    "tools/gen_previews.py, and this build did not have it."
                    % self.arguments[0])
            return [nodes.note("", nodes.paragraph(text=text))]

    app.add_directive("thumbnail", MissingThumbnail, override=True)

    #  A plain image under img/preview/ is the same case.
    from docutils.parsers.rst.directives.images import Image

    class MissingImage(Image):
        def run(self):
            uri = self.arguments[0]
            if uri.startswith("img/preview/") and not os.path.exists(
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), uri)):
                text = ("Picture %s is rendered from the game's own files by "
                        "tools/gen_previews.py, and this build did not have it." % uri)
                return [nodes.note("", nodes.paragraph(text=text))]
            return super().run()

    app.add_directive("image", MissingImage, override=True)

    from docutils.parsers.rst.directives.images import Figure

    class MissingFigure(Figure):
        def run(self):
            uri = self.arguments[0]
            if uri.startswith("img/preview/") and not os.path.exists(
                    os.path.join(os.path.dirname(os.path.abspath(__file__)), uri)):
                text = ("Picture %s is rendered from the game's own files by "
                        "tools/gen_previews.py, and this build did not have it." % uri)
                return [nodes.note("", nodes.paragraph(text=text))]
            return super().run()

    app.add_directive("figure", MissingFigure, override=True)

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
