# Documentation site (mkdocs.yml, manual/; Zensical, see RELEASING.md). The firmware itself
# builds with idf.py, not make -- there is deliberately no `firmware` target here.
SHELL := /bin/bash
PY    := .venv/bin/python

.PHONY: help venv docs-setup docs-check api-validate api-smoke docs-image docs-vendor docs-stage-firmware docs-build docs-offline docs-serve docs-preview-versions unit-test can-setup can-test yakut

# The hardware tests' Python (tests/hardware): its own venv, made by `make can-setup`, or another
# with requirements-can.txt installed: make can-test CAN_PY=/path/to/python
CAN_PY ?= .venv-can/bin/python
# A Python with yakut installed, for `make yakut` (not in requirements-can.txt)
YAKUT_PY ?= python3
# cJSON for the host tests of the board's happenings: the copy the IDF component manager fetches into
# managed_components at the first build (or reconfigure), or another: make unit-test CJSON_DIR=/path/to/cJSON
CJSON_DIR ?= managed_components/espressif__cjson/cJSON

help:                  ## list targets
	@grep -E '^[a-zA-Z_-]+:.*?## ' $(MAKEFILE_LIST) | awk 'BEGIN{FS=":.*?## "}{printf "  %-22s %s\n", $$1, $$2}'

venv:                  ## python venv for the documentation tools
	@test -x $(PY) || python3 -m venv .venv

docs-setup: venv       ## install the pinned docs toolchain (requirements-docs.txt)
	@$(PY) -m pip install -q -r requirements-docs.txt

docs-check:            ## every registered console command is documented, the HTTP API's description matches the firmware, the pattern image is current, the installer's and board pages' JavaScript is intact and passes its tests (stdlib only, no venv needed)
	@python3 tools/check_command_docs.py
	@python3 tools/check_api_docs.py
	@python3 tools/render_calibration.py --check
	@python3 tools/vendor_js.py --check
	@python3 tools/web_install_manifest.py --fixtures build/installer-fixtures >/dev/null
	@if command -v node >/dev/null; then node --test --test-reporter=dot tools/test_installer.mjs tools/test_board.mjs; \
	 else echo "node not found: skipping the JavaScript tests (the docs workflow runs them)"; fi

api-validate:          ## validate manual/reference/openapi.json against the OpenAPI 3.1 schema, and its examples against theirs (docs venv)
	@$(PY) -m openapi_spec_validator manual/reference/openapi.json
	@$(PY) tools/check_api_examples.py

api-smoke:             ## call a board's API and check each reply against the description: HOST=holo-xxxx.local [WRITE=1]
	@$(PY) tools/api_smoke.py --host $(or $(HOST),holo-2db0.local) $(if $(WRITE),--write)

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

unit-test:             ## the host-built C tests (tests/host): the Cyphal node's unique ID, pseudo-ID, name rules, frame and version parsing, settings comparison, the end of its listen, plug-and-play's request, reply, timing and in-use check, the bus-off back-off, and its register table; the holo's Cyphal requests as the engine's motion and the ring's change, Scene and Screen requests and their paths, the board's happenings as Event, Status's parts and sizes, its refusals (with the routes' codes), positions and rates, the file services' paths, errors and entries, MediaInfo's and ListScenes' responses, and the calibration registers' values (cc only, no ESP-IDF); the board's happenings read back from their JSON and as the host link's native records (with cJSON from managed_components: CJSON_DIR); and tools/yakut_can.py's refusals (python3, stdlib only)
	@mkdir -p build/host-tests
	@$(CC) -std=c11 -Wall -Wextra -Werror -O1 -Icomponents/cyphal/include -Icomponents/cyphal/dsdl -o build/host-tests/test_cyphal_util \
	    tests/host/test_cyphal_util.c components/cyphal/cyphal_util.c
	@$(CC) -std=c11 -Wall -Wextra -Werror -O1 -Icomponents/cyphal/include -Icomponents/cyphal/dsdl -o build/host-tests/test_cyphal_registers \
	    tests/host/test_cyphal_registers.c components/cyphal/cyphal_registers.c components/cyphal/cyphal_util.c -lm
	@$(CC) -std=c11 -Wall -Wextra -Werror -O1 -Imain -Icomponents/cyphal/include -Icomponents/cyphal/dsdl \
	    -Icomponents/board_api/include -Icomponents/scenes/include -o build/host-tests/test_holo_cyphal_util \
	    tests/host/test_holo_cyphal_util.c main/holo_cyphal_util.c components/cyphal/cyphal_util.c \
	    components/board_api/api_codes.c -lm
	@test -f $(CJSON_DIR)/cJSON.c || { echo "unit-test: no cJSON at $(CJSON_DIR): build once (idf.py reconfigure fetches it), or give CJSON_DIR"; exit 1; }
	@$(CC) -std=c11 -Wall -Wextra -Werror -O1 -Icomponents/scenes/include -Icomponents/hostlink -I$(CJSON_DIR) \
	    -o build/host-tests/test_happening tests/host/test_happening.c components/scenes/happening.c \
	    components/hostlink/hl_record.c $(CJSON_DIR)/cJSON.c -lm
	@build/host-tests/test_cyphal_util
	@build/host-tests/test_cyphal_registers
	@build/host-tests/test_holo_cyphal_util
	@build/host-tests/test_happening
	@python3 tools/test_yakut_can.py

can-setup:             ## a venv for the hardware tests (.venv-can, requirements-can.txt)
	@test -x .venv-can/bin/python || python3 -m venv .venv-can
	@.venv-can/bin/python -m pip install -q -r requirements-can.txt

can-test:              ## the Cyphal node's hardware tests (tests/hardware) against a board and a gs_usb adapter; stop cyphal_service first. ARGS=... goes to pytest: --board-host <ip> adds the HTTP comparisons and the OTA test, --holo-minutes <n> sets the holo's load test (3; the phase gates run 10)
	@$(CAN_PY) -m pytest tests/hardware $(ARGS)

yakut:                 ## Yakut on the board through the gs_usb adapter, under the adapter's rules (tools/yakut_can.py): ARGS="register-access 52 uavcan.node.id" YAKUT_PY=<a Python with yakut>; then tools/can_check.py flush before cyphal_service, or ARGS="--flush ..."
	@$(YAKUT_PY) tools/yakut_can.py $(ARGS)
