# Documentation site (mkdocs.yml, manual/; Zensical, see RELEASING.md). The firmware itself
# builds with idf.py, not make -- there is deliberately no `firmware` target here.
SHELL := /bin/bash
PY    := .venv/bin/python

.PHONY: help venv docs-setup docs-check docs-image docs-build docs-offline docs-serve docs-preview-versions

help:                  ## list targets
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*?## "}{printf "  %-22s %s\n", $$1, $$2}'

venv:                  ## python venv for the documentation tools
	@test -x $(PY) || python3 -m venv .venv

docs-setup: venv       ## install the pinned docs toolchain (requirements-docs.txt)
	@$(PY) -m pip install -q -r requirements-docs.txt

docs-check:            ## every registered console command is documented, and the pattern image is current (stdlib only, no venv needed)
	@python3 tools/check_command_docs.py
	@python3 tools/render_calibration.py --check

docs-image:            ## re-render manual/images/calibration.png from the firmware's own geometry
	@python3 tools/render_calibration.py

docs-build:            ## build the site strictly -> build/site/
	.venv/bin/zensical build --strict --clean

docs-offline:          ## build a copy that works from disk -> build/site-offline/
	@sed -e 's|^site_dir: .*|site_dir: build/site-offline|' -e 's|^strict: .*|strict: true\nuse_directory_urls: false|' \
	    mkdocs.yml > .mkdocs-offline.yml
	.venv/bin/zensical build --strict --clean -f .mkdocs-offline.yml; rc=$$?; rm -f .mkdocs-offline.yml; exit $$rc

docs-serve:            ## serve the site locally with live reload
	.venv/bin/zensical serve

docs-preview-versions: ## serve the local gh-pages branch with the version switcher (after mike deploy)
	.venv/bin/mike serve
