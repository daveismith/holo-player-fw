# Documentation site (mkdocs.yml, manual/; Zensical, see RELEASING.md). The firmware itself
# builds with idf.py, not make -- there is deliberately no `firmware` target here.
SHELL := /bin/bash
PY    := .venv/bin/python

.PHONY: help venv docs-setup docs-check docs-image docs-vendor docs-stage-firmware docs-build docs-offline docs-serve docs-preview-versions

help:                  ## list targets
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*?## "}{printf "  %-22s %s\n", $$1, $$2}'

venv:                  ## python venv for the documentation tools
	@test -x $(PY) || python3 -m venv .venv

docs-setup: venv       ## install the pinned docs toolchain (requirements-docs.txt)
	@$(PY) -m pip install -q -r requirements-docs.txt

docs-check:            ## every registered console command is documented, the pattern image is current, the installer's and board pages' JavaScript is intact and passes its tests (stdlib only, no venv needed)
	@python3 tools/check_command_docs.py
	@python3 tools/render_calibration.py --check
	@python3 tools/vendor_js.py --check
	@python3 tools/web_install_manifest.py --fixtures build/installer-fixtures >/dev/null
	@if command -v node >/dev/null; then node --test --test-reporter=dot tools/test_installer.mjs tools/test_board.mjs; \
	 else echo "node not found: skipping the JavaScript tests (the docs workflow runs them)"; fi

docs-image:            ## re-render manual/images/calibration.png from the firmware's own geometry
	@python3 tools/render_calibration.py

docs-vendor:           ## re-vendor the pinned JavaScript libraries (tools/vendor_js.py; needs the network)
	@python3 tools/vendor_js.py update

docs-stage-firmware:   ## stage the current idf.py build as the installer's firmware (manual/firmware/), to try it locally
	@python3 tools/web_install_manifest.py --dist build/dist
	@python3 tools/web_install_manifest.py --stage manual/firmware --from build/dist

docs-build:            ## build the site strictly -> build/site/
	.venv/bin/zensical build --strict --clean

docs-offline:          ## build a copy that works from disk, and installs with serve.py -> build/site-offline/
	@sed -e 's|^site_dir: .*|site_dir: build/site-offline|' -e 's|^strict: .*|strict: true\nuse_directory_urls: false|' \
	    -e 's|^  name: material$$|  name: material\n  font: false|' \
	    mkdocs.yml > .mkdocs-offline.yml
	.venv/bin/zensical build --strict --clean -f .mkdocs-offline.yml; rc=$$?; rm -f .mkdocs-offline.yml; exit $$rc
	@cp tools/offline_serve.py build/site-offline/serve.py

docs-serve:            ## serve the site locally with live reload
	.venv/bin/zensical serve

docs-preview-versions: ## serve the local gh-pages branch with the version switcher (after mike deploy)
	.venv/bin/mike serve
