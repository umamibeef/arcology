#  Sphinx build.  The .rst files are the source; edit them directly.
SPHINXBUILD ?= .venv/bin/sphinx-build
SOURCEDIR    = .
BUILDDIR     = _build

.PHONY: help html clean linkcheck

help:
	@echo "make html   build the site into $(BUILDDIR)/html"
	@echo "make clean  remove the build tree"

html:
	@$(SPHINXBUILD) -b html "$(SOURCEDIR)" "$(BUILDDIR)/html" $(O)

linkcheck:
	@$(SPHINXBUILD) -b linkcheck "$(SOURCEDIR)" "$(BUILDDIR)/linkcheck"

clean:
	rm -rf "$(BUILDDIR)"
